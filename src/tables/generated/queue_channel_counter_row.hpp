#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qu::tables
{

struct QueueChannelCounterRow
{
    std::string namespaceName;
    std::string channelName;
    std::int64_t nextSequence;

    friend bool operator==(
        const QueueChannelCounterRow&,
        const QueueChannelCounterRow&
    ) = default;
};

struct QueueChannelCounterRowMapping
{
    static constexpr std::string_view table_name = "queue_channel_counters";
    static constexpr int schema_version = 1;
    static constexpr std::string_view key_separator = "\u001f";
    static constexpr std::string_view field_namespaceName = "namespaceName";
    static constexpr std::string_view field_channelName = "channelName";
    static constexpr std::string_view field_nextSequence = "nextSequence";
    static constexpr std::string_view key_field = "namespaceName\u001fchannelName";
    static constexpr std::string_view index_0_name = "idx_queue_channel_counter_namespace";
    static constexpr std::string_view index_0_path = "$.namespaceName";

    static std::string key(const QueueChannelCounterRow& row)
    {
        return row.namespaceName + std::string(key_separator) + row.channelName;
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string(std::string(field_namespaceName)).mark_required(true),
            mt::FieldSpec::string(std::string(field_channelName)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_nextSequence)).mark_required(true)
        };
    }

    static mt::Json to_json(const QueueChannelCounterRow& row)
    {
        return mt::Json::object(
            {{std::string(field_namespaceName), row.namespaceName},
             {std::string(field_channelName), row.channelName},
             {std::string(field_nextSequence), row.nextSequence}}
        );
    }

    static QueueChannelCounterRow from_json(const mt::Json& json)
    {
        return QueueChannelCounterRow{
            .namespaceName = json[std::string(field_namespaceName)].as_string(),
            .channelName = json[std::string(field_channelName)].as_string(),
            .nextSequence = json[std::string(field_nextSequence)].as_int64()
        };
    }

    static std::vector<mt::IndexSpec> indexes()
    {
        return {
            mt::IndexSpec::json_path_index(std::string(index_0_name), std::string(index_0_path))
        };
    }
};

} // namespace qu::tables
