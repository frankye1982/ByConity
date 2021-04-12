#pragma once

#if !defined(ARCADIA_BUILD)
#include <Common/config.h>
#endif

#if USE_AWS_S3

#include "Client/Connection.h"
#include <Interpreters/Cluster.h>
#include <IO/S3Common.h>
#include <Storages/StorageS3.h>

#include <memory>
#include <optional>
#include "ext/shared_ptr_helper.h"

namespace DB
{

class Context;

struct ClientAuthentificationBuilder
{
    String access_key_id;
    String secret_access_key;
    UInt64 max_connections;
};

class StorageS3Distributed : public ext::shared_ptr_helper<StorageS3Distributed>, public IStorage
{
    friend struct ext::shared_ptr_helper<StorageS3Distributed>;
public:
    std::string getName() const override { return "S3Distributed"; }

    Pipe read(const Names &, const StorageMetadataPtr &, SelectQueryInfo &,
        ContextPtr, QueryProcessingStage::Enum, size_t /*max_block_size*/, unsigned /*num_streams*/) override;

    QueryProcessingStage::Enum getQueryProcessingStage(ContextPtr, QueryProcessingStage::Enum, SelectQueryInfo &) const override;

    NamesAndTypesList getVirtuals() const override;

protected:
    StorageS3Distributed(
        const String & filename_, const String & access_key_id_, const String & secret_access_key_, const StorageID & table_id_,
        String cluster_name_, const String & format_name_, UInt64 max_connections_, const ColumnsDescription & columns_,
        const ConstraintsDescription & constraints_, ContextPtr context_, const String & compression_method_);

private:
    /// Connections from initiator to other nodes
    std::vector<std::shared_ptr<Connection>> connections;
    StorageS3::ClientAuthentificaiton client_auth;

    String filename;
    String cluster_name;
    ClusterPtr cluster;

    String format_name;
    String compression_method;
};


}

#endif
