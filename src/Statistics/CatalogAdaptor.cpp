/*
 * Copyright (2022) Bytedance Ltd. and/or its affiliates
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <DataTypes/DataTypeMap.h>
#include <Interpreters/Context.h>
#include <Statistics/CatalogAdaptor.h>
#include <Statistics/SubqueryHelper.h>
#include <Storages/StorageCnchMergeTree.h>
#include "DataTypes/MapHelpers.h"
#include "Parsers/formatTenantDatabaseName.h"
#include <boost/regex.hpp>
namespace DB::Statistics
{
CatalogAdaptorPtr createCatalogAdaptorMemory(ContextPtr context);
CatalogAdaptorPtr createCatalogAdaptorCnch(ContextPtr context);

CatalogAdaptorPtr createCatalogAdaptor(ContextPtr context)
{
    if (context->getSettingsRef().enable_memory_catalog)
    {
        return createCatalogAdaptorMemory(context);
    }
    return createCatalogAdaptorCnch(context);
}

ColumnDescVector CatalogAdaptor::filterCollectableColumns(
    const StatsTableIdentifier & table, const std::vector<String> & target_columns, bool exception_on_unsupported)
{
    std::unordered_map<String, DataTypePtr> name_to_type;
    ColumnDescVector result;
    std::vector<String> unsupported_columns;

    auto storage = getStorageByTableId(table);

    auto snapshot = storage->getInMemoryMetadataPtr();
    for (const auto & col_name : target_columns)
    {
        auto col_opt = snapshot->getColumns().tryGetColumn(GetColumnsOptions::All, col_name);
        if (!col_opt)
        {
            unsupported_columns.emplace_back(col_name);
            continue;
        }
        const auto & col = col_opt.value();
        if (Statistics::isCollectableType(col.type))
        {
            result.emplace_back(std::move(col));
        }
        else
        {
            unsupported_columns.emplace_back(col_name);
        }
    }

    if (exception_on_unsupported && !unsupported_columns.empty())
    {
        auto err_msg = fmt::format(FMT_STRING("columns ({}) is not collectable"), fmt::join(unsupported_columns, ", "));
        throw Exception(err_msg, ErrorCodes::BAD_ARGUMENTS);
    }

    return result;
}

CatalogAdaptor::TableOptions CatalogAdaptor::getTableOptions(const StatsTableIdentifier & table)
{
    const auto unsupported = TableOptions{false, false};
    const auto full_supported = TableOptions{true, true};
    const auto only_manual = TableOptions{true, false};

    if (table.getDatabaseName() == "system" || table.getDatabaseName() == "cnch_system")
    {
        return unsupported;
    }

    auto storage = getStorageByTableId(table);

    auto options = [&] {
        auto engine = storage->getName();
        if (dynamic_cast<StorageCnchMergeTree*>(storage.get()))
        {
            return full_supported;
        }
        else if (engine == "Memory")
            return only_manual;
        else if (engine == "CnchHive")
            return only_manual;
        else
            return unsupported;
    }();

    if (!options.is_collectable)
        return options;

    const auto & pattern = context->getSettingsRef().statistics_exclude_tables_regex.value;
    if (!pattern.empty())
    {
        try
        {
            boost::regex re(pattern);
            boost::cmatch tmp;
            if (boost::regex_match(table.getTableName().data(), tmp, re))
            {
                return unsupported;
            }
        }
        catch (boost::wrapexcept<boost::regex_error> & e)
        {
            auto err_msg = std::string("statistics_exclude_tables_regex match error: ") + e.what();
            throw Exception(err_msg, ErrorCodes::BAD_ARGUMENTS);
        }
    }
    return options;
}

std::optional<UInt64> CatalogAdaptor::queryRowCount(const StatsTableIdentifier & table_id)
{
    auto storage = getStorageByTableId(table_id);
    const auto * cnch_merge_tree = dynamic_cast<const StorageCnchMergeTree *>(storage.get());
    if (!cnch_merge_tree)
        return std::nullopt;

    auto sql = fmt::format(
        FMT_STRING("select sum(rows) from system.cnch_parts where database='{}' and table = '{}'"),
        getOriginalDatabaseName(table_id.getDatabaseName()),
        table_id.getTableName());
    auto helper = SubqueryHelper::create(context, sql);
    Block block = getOnlyRowFrom(helper);
    if (block.columns() != 1)
    {
        throw Exception("wrong column", ErrorCodes::LOGICAL_ERROR);
    }
    auto col = block.getColumns().at(0);
    auto row_count = col->getInt(0);
    return row_count;
}

ColumnDescVector CatalogAdaptor::getAllCollectableColumns(const StatsTableIdentifier & identifier)
{
    auto storage = getStorageByTableId(identifier);
    auto snapshot = storage->getInMemoryMetadataPtr();
    auto col_names = snapshot->getColumns().getNamesOfOrdinary();

    auto exists_col_names = this->readStatsColumnsKey(identifier);
    std::sort(exists_col_names.begin(), exists_col_names.end());

    for (const auto & name : exists_col_names)
    {
        if (isMapImplicitKey(name))
        {
            col_names.emplace_back(name);
        }
    }

    auto result = this->filterCollectableColumns(identifier, col_names);
    return result;
}
}
