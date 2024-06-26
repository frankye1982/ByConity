/*
 * Copyright 2016-2023 ClickHouse, Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


/*
 * This file may have been modified by Bytedance Ltd. and/or its affiliates (“ Bytedance's Modifications”).
 * All Bytedance's Modifications are Copyright (2023) Bytedance Ltd. and/or its affiliates.
 */

#include <cstddef>
#include <Columns/ColumnConst.h>

#include <Common/Exception.h>
#include <Common/escapeForFileName.h>
#include <Common/SipHash.h>

#include <IO/WriteHelpers.h>
#include <IO/Operators.h>

#include <DataTypes/IDataType.h>
#include <DataTypes/DataTypeCustom.h>
#include <DataTypes/NestedUtils.h>
#include <DataTypes/Serializations/SerializationTupleElement.h>
#include <DataTypes/Serializations/SerializationInfo.h>
#include <Storages/MergeTree/MergeTreeSuffix.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
    extern const int DATA_TYPE_CANNOT_BE_PROMOTED;
    extern const int ILLEGAL_COLUMN;
    extern const int UNSUPPORTED_PARAMETER;
}

static bool default_use_kv_map_type = false;

void setDefaultUseMapType(bool default_use_kv_map_type_)
{
    default_use_kv_map_type = default_use_kv_map_type_;
}

bool getDefaultUseMapType()
{
    return default_use_kv_map_type;
}

IDataType::~IDataType() = default;

String IDataType::getName() const
{
    if (custom_name)
    {
        return custom_name->getName();
    }
    else
    {
        return doGetName();
    }
}

String IDataType::doGetName() const
{
    return getFamilyName();
}

void IDataType::updateAvgValueSizeHint(const IColumn & column, double & avg_value_size_hint)
{
    /// Update the average value size hint if amount of read rows isn't too small
    size_t column_size = column.size();
    if (column_size > 10)
    {
        double current_avg_value_size = static_cast<double>(column.byteSize()) / column_size;

        /// Heuristic is chosen so that avg_value_size_hint increases rapidly but decreases slowly.
        if (current_avg_value_size > avg_value_size_hint)
            avg_value_size_hint = std::min(1024., current_avg_value_size); /// avoid overestimation
        else if (current_avg_value_size * 2 < avg_value_size_hint)
            avg_value_size_hint = (current_avg_value_size + avg_value_size_hint * 3) / 4;
    }
}

ColumnPtr IDataType::createColumnConst(size_t size, const Field & field) const
{
    auto column = createColumn();
    column->insert(field);
    return ColumnConst::create(std::move(column), size);
}


ColumnPtr IDataType::createColumnConstWithDefaultValue(size_t size) const
{
    return createColumnConst(size, getDefault());
}

DataTypePtr IDataType::promoteNumericType() const
{
    throw Exception("Data type " + getName() + " can't be promoted.", ErrorCodes::DATA_TYPE_CANNOT_BE_PROMOTED);
}

size_t IDataType::getSizeOfValueInMemory() const
{
    throw Exception("Value of type " + getName() + " in memory is not of fixed size.", ErrorCodes::LOGICAL_ERROR);
}

void IDataType::forEachSubcolumn(
    const SubcolumnCallback & callback,
    const SubstreamData & data)
{
    ISerialization::StreamCallback callback_with_data = [&](const auto & subpath)
    {
        for (size_t i = 0; i < subpath.size(); ++i)
        {
            size_t prefix_len = i + 1;
            if (!subpath[i].visited && ISerialization::hasSubcolumnForPath(subpath, prefix_len))
            {
                auto name = ISerialization::getSubcolumnNameForStream(subpath, prefix_len);
                auto subdata = ISerialization::createFromPath(subpath, prefix_len);
                callback(subpath, name, subdata);
            }
            subpath[i].visited = true;
        }
    };

    ISerialization::EnumerateStreamsSettings settings;
    settings.position_independent_encoding = false;
    data.serialization->enumerateStreams(settings, callback_with_data, data);
}

template <typename Ptr>
Ptr IDataType::getForSubcolumn(
    const String & subcolumn_name,
    const SubstreamData & data,
    Ptr SubstreamData::*member,
    bool throw_if_null) const
{
    Ptr res;
    forEachSubcolumn([&](const auto &, const auto & name, const auto & subdata)
    {
        if (name == subcolumn_name)
            res = subdata.*member;
    }, data);

    if (!res && throw_if_null)
        throw Exception(ErrorCodes::ILLEGAL_COLUMN, "There is no subcolumn {} in type {}", subcolumn_name, getName());

    return res;
}

DataTypePtr IDataType::tryGetSubcolumnType(const String & subcolumn_name) const
{
    if (subcolumn_name == MAIN_SUBCOLUMN_NAME)
        return shared_from_this();

    return nullptr;
}

DataTypePtr IDataType::getSubcolumnType(const String & subcolumn_name) const
{
    if (subcolumn_name == MAIN_SUBCOLUMN_NAME)
        return shared_from_this();

    auto data = SubstreamData(getDefaultSerialization()).withType(getPtr());
    return getForSubcolumn<DataTypePtr>(subcolumn_name, data, &SubstreamData::type, true);
}

ColumnPtr IDataType::tryGetSubcolumn(const String & subcolumn_name, const ColumnPtr & column) const
{
    auto data = SubstreamData(getDefaultSerialization()).withColumn(column);
    return getForSubcolumn<ColumnPtr>(subcolumn_name, data, &SubstreamData::column, false);
}

ColumnPtr IDataType::getSubcolumn(const String & subcolumn_name, const IColumn &) const
{
    throw Exception(ErrorCodes::ILLEGAL_COLUMN, "There is no subcolumn {} in type {}", subcolumn_name, getName());
}

Names IDataType::getSubcolumnNames() const
{
    Names res;
    forEachSubcolumn([&](const auto &, const auto & name, const auto &)
    {
        res.push_back(name);
    }, SubstreamData(getDefaultSerialization()));
    return res;
}

void IDataType::insertDefaultInto(IColumn & column) const
{
    column.insertDefault();
}

void IDataType::insertManyDefaultsInto(IColumn & column, size_t n) const
{
    for (size_t i = 0; i < n; ++i)
        insertDefaultInto(column);
}

void IDataType::setCustomization(DataTypeCustomDescPtr custom_desc_) const
{
    /// replace only if not null
    if (custom_desc_->name)
        custom_name = std::move(custom_desc_->name);

    if (custom_desc_->serialization)
        custom_serialization = std::move(custom_desc_->serialization);
}

SerializationInfoPtr IDataType::getSerializationInfo(const IColumn & column) const
{
    if (const auto * column_const = checkAndGetColumn<ColumnConst>(&column))
        return getSerializationInfo(column_const->getDataColumn());

    return std::make_shared<SerializationInfo>(ISerialization::getKind(column), SerializationInfo::Settings{});
}

SerializationPtr IDataType::getDefaultSerialization() const
{
    if (custom_serialization)
        return custom_serialization;

    return doGetDefaultSerialization();
}

SerializationPtr IDataType::getSubcolumnSerialization(const String & subcolumn_name, const SerializationPtr & serialization) const
{
    auto data = SubstreamData(serialization);
    return getForSubcolumn<SerializationPtr>(subcolumn_name, data, &SubstreamData::serialization, true);
}

SerializationPtr IDataType::getSerialization(ISerialization::Kind  /*kind*/) const
{
    // if (supportsSparseSerialization() && kind == ISerialization::Kind::SPARSE)
    //     return getSparseSerialization();

    return getDefaultSerialization();
}

SerializationPtr IDataType::getSerialization(const SerializationInfo & info) const
{
    return getSerialization(info.getKind());
}

// static
SerializationPtr IDataType::getSerialization(const NameAndTypePair & column, const IDataType::StreamExistenceCallback & callback)
{
    if (column.isSubcolumn())
    {
        const auto & type_in_storage = column.getTypeInStorage();
        auto default_serialization = type_in_storage->getDefaultSerialization();
        return type_in_storage->getSubcolumnSerialization(column.getSubcolumnName(), default_serialization);
    }

    return column.type->getSerialization(column.name, callback);
}

SerializationPtr IDataType::getSerialization(const String &, const StreamExistenceCallback &) const
{
    return getDefaultSerialization();
}

DataTypePtr IDataType::getTypeForSubstream(const ISerialization::SubstreamPath & substream_path) const
{
    auto type = tryGetSubcolumnType(ISerialization::getSubcolumnNameForStream(substream_path));
    if (type)
        return type->getSubcolumnType(MAIN_SUBCOLUMN_NAME);

    return getSubcolumnType(MAIN_SUBCOLUMN_NAME);
}

void IDataType::enumerateStreams(const SerializationPtr & serialization, const StreamCallbackWithType & callback, ISerialization::SubstreamPath & path) const
{
    serialization->enumerateStreams([&](const ISerialization::SubstreamPath & substream_path)
    {
        callback(substream_path, *getTypeForSubstream(substream_path));
    }, path);
}

Field IDataType::stringToVisitorField(const String &) const
{
    throw Exception("stringToVisitorField not implemented for data type " + getName(), ErrorCodes::NOT_IMPLEMENTED);
}

Names IDataType::getSpecialColumnFiles(const String & prefix, bool throw_exception) const
{
    Names files;

    if (isCompression())
    {
        files.push_back(prefix + COMPRESSION_DATA_FILE_EXTENSION);
        files.push_back(prefix + COMPRESSION_MARKS_FILE_EXTENSION);
    }
    if (isBitmapIndex())
    {
        files.push_back(prefix + BITMAP_IDX_EXTENSION);
        files.push_back(prefix + BITMAP_IRK_EXTENSION);
    }
    if (isSegmentBitmapIndex())
    {
        files.push_back(prefix + SEGMENT_BITMAP_IDX_EXTENSION);
        files.push_back(prefix + SEGMENT_BITMAP_TABLE_EXTENSION);
        files.push_back(prefix + SEGMENT_BITMAP_DIRECTORY_EXTENSION);
    }
    if (throw_exception && (lowCardinality() || isMap()))
    {
        // not support , throw exception instead.
        throw Exception(
            "Mutate (FastDelete) " + getName() + " (with special attribution) is not support", ErrorCodes::UNSUPPORTED_PARAMETER);
    }

    return files;
}

String IDataType::stringToVisitorString(const String &) const
{
    throw Exception("stringToVisitorString not implemented for Data type" + getName(), ErrorCodes::NOT_IMPLEMENTED);
}
}
