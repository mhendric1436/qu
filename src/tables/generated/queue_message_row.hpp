#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qu::tables
{

struct QueueMessageRow
{
    std::string id;
    std::string status;
    mt::Json payload = mt::Json::object({});
    std::optional<std::string> workerId;
    std::int64_t createdAtMs;
    std::optional<std::int64_t> claimedAtMs;
    std::optional<std::int64_t> claimedUntilMs;
    std::optional<std::int64_t> processedAtMs;
    std::int64_t attempt = 0;

    friend bool operator==(
        const QueueMessageRow&,
        const QueueMessageRow&
    ) = default;
};

struct QueueMessageRowMapping
{
    static constexpr std::string_view table_name = "queue_messages";
    static constexpr int schema_version = 1;
    static constexpr std::string_view field_id = "id";
    static constexpr std::string_view field_status = "status";
    static constexpr std::string_view field_payload = "payload";
    static constexpr std::string_view field_workerId = "workerId";
    static constexpr std::string_view field_createdAtMs = "createdAtMs";
    static constexpr std::string_view field_claimedAtMs = "claimedAtMs";
    static constexpr std::string_view field_claimedUntilMs = "claimedUntilMs";
    static constexpr std::string_view field_processedAtMs = "processedAtMs";
    static constexpr std::string_view field_attempt = "attempt";
    static constexpr std::string_view key_field = field_id;
    static constexpr std::string_view index_0_name = "idx_queue_message_status";
    static constexpr std::string_view index_0_path = "$.status";

    static std::string key(const QueueMessageRow& row)
    {
        return row.id;
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string(std::string(field_id)).mark_required(true),
            mt::FieldSpec::string(std::string(field_status)).mark_required(true),
            mt::FieldSpec::json(std::string(field_payload))
                .mark_required(false)
                .with_default(mt::Json::object({})),
            mt::FieldSpec::optional(std::string(field_workerId), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::int64(std::string(field_createdAtMs)).mark_required(true),
            mt::FieldSpec::optional(std::string(field_claimedAtMs), mt::FieldType::Int64)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_claimedUntilMs), mt::FieldType::Int64)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_processedAtMs), mt::FieldType::Int64)
                .mark_required(true),
            mt::FieldSpec::int64(std::string(field_attempt))
                .mark_required(false)
                .with_default(mt::Json(std::int64_t{0}))
        };
    }

    static mt::Json to_json(const QueueMessageRow& row)
    {
        return mt::Json::object(
            {{std::string(field_id), row.id},
             {std::string(field_status), row.status},
             {std::string(field_payload), row.payload},
             {std::string(field_workerId),
              row.workerId ? mt::Json(*row.workerId) : mt::Json::null()},
             {std::string(field_createdAtMs), row.createdAtMs},
             {std::string(field_claimedAtMs),
              row.claimedAtMs ? mt::Json(*row.claimedAtMs) : mt::Json::null()},
             {std::string(field_claimedUntilMs),
              row.claimedUntilMs ? mt::Json(*row.claimedUntilMs) : mt::Json::null()},
             {std::string(field_processedAtMs),
              row.processedAtMs ? mt::Json(*row.processedAtMs) : mt::Json::null()},
             {std::string(field_attempt), row.attempt}}
        );
    }

    static QueueMessageRow from_json(const mt::Json& json)
    {
        return QueueMessageRow{
            .id = json[std::string(field_id)].as_string(),
            .status = json[std::string(field_status)].as_string(),
            .payload = json[std::string(field_payload)],
            .workerId =
                json[std::string(field_workerId)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_workerId)].as_string()),
            .createdAtMs = json[std::string(field_createdAtMs)].as_int64(),
            .claimedAtMs =
                json[std::string(field_claimedAtMs)].is_null()
                    ? std::nullopt
                    : std::optional<std::int64_t>(json[std::string(field_claimedAtMs)].as_int64()),
            .claimedUntilMs = json[std::string(field_claimedUntilMs)].is_null()
                                  ? std::nullopt
                                  : std::optional<std::int64_t>(
                                        json[std::string(field_claimedUntilMs)].as_int64()
                                    ),
            .processedAtMs = json[std::string(field_processedAtMs)].is_null()
                                 ? std::nullopt
                                 : std::optional<std::int64_t>(
                                       json[std::string(field_processedAtMs)].as_int64()
                                   ),
            .attempt = json[std::string(field_attempt)].as_int64()
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
