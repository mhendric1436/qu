#include "qu/queue.hpp"

#include "mt/query.hpp"
#include "mt/table.hpp"
#include "mt/transaction.hpp"
#include "tables/generated/queue_message_row.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace qu
{
namespace
{

constexpr std::string_view STATUS_PENDING = "pending";
constexpr std::string_view STATUS_CLAIMED = "claimed";
constexpr std::string_view STATUS_PROCESSED = "processed";

using QueueMessageTable = mt::Table<tables::QueueMessageRow, tables::QueueMessageRowMapping>;

std::string status_to_string(MessageStatus status)
{
    switch (status)
    {
    case MessageStatus::Pending:
        return std::string(STATUS_PENDING);
    case MessageStatus::Claimed:
        return std::string(STATUS_CLAIMED);
    case MessageStatus::Processed:
        return std::string(STATUS_PROCESSED);
    }

    throw InvalidMessageState("unknown queue message status");
}

MessageStatus status_from_string(const std::string& status)
{
    if (status == STATUS_PENDING)
    {
        return MessageStatus::Pending;
    }
    if (status == STATUS_CLAIMED)
    {
        return MessageStatus::Claimed;
    }
    if (status == STATUS_PROCESSED)
    {
        return MessageStatus::Processed;
    }

    throw InvalidMessageState("unknown queue message status: " + status);
}

QueuedMessage to_public_message(const tables::QueueMessageRow& row)
{
    return QueuedMessage{
        .id = row.id,
        .status = status_from_string(row.status),
        .payload = row.payload,
        .worker_id = row.workerId,
        .created_at_ms = row.createdAtMs,
        .claimed_at_ms = row.claimedAtMs,
        .claimed_until_ms = row.claimedUntilMs,
        .processed_at_ms = row.processedAtMs,
        .attempt = row.attempt
    };
}

ClaimedMessage to_claimed_message(const tables::QueueMessageRow& row)
{
    if (!row.workerId || !row.claimedUntilMs)
    {
        throw InvalidMessageState("claimed message is missing claim metadata");
    }

    return ClaimedMessage{
        .id = row.id,
        .payload = row.payload,
        .worker_id = *row.workerId,
        .attempt = row.attempt,
        .claimed_until_ms = *row.claimedUntilMs
    };
}

QueueMessageTable queue_table(mt::Database& database)
{
    mt::TableProvider tables{database};
    return tables.table<tables::QueueMessageRow, tables::QueueMessageRowMapping>();
}

void require_claimed_by(
    const tables::QueueMessageRow& row,
    const std::string& worker_id
)
{
    if (row.status != STATUS_CLAIMED || !row.workerId || *row.workerId != worker_id)
    {
        throw InvalidMessageState("message is not claimed by worker");
    }
}

} // namespace

QueueError::QueueError(const std::string& message)
    : std::runtime_error(message)
{
}

DuplicateMessage::DuplicateMessage(const std::string& id)
    : QueueError("duplicate queue message: " + id)
{
}

InvalidMessageState::InvalidMessageState(const std::string& message)
    : QueueError(message)
{
}

Queue::Queue(
    mt::Database& database,
    QueueConfig config
)
    : database_(&database),
      config_(config)
{
    (void)queue_table(*database_);
}

void Queue::enqueue(
    std::string id,
    mt::Json payload,
    std::int64_t now_ms
) const
{
    auto messages = queue_table(*database_);
    mt::TransactionProvider txs{*database_};

    txs.run(
        [&](mt::Transaction& tx)
        {
            if (messages.get(tx, id))
            {
                throw DuplicateMessage(id);
            }

            messages.put(
                tx, tables::QueueMessageRow{
                        .id = std::move(id),
                        .status = status_to_string(MessageStatus::Pending),
                        .payload = std::move(payload),
                        .createdAtMs = now_ms
                    }
            );
        }
    );
}

std::optional<ClaimedMessage> Queue::claim_next(
    std::string worker_id,
    std::int64_t now_ms
) const
{
    auto messages = queue_table(*database_);
    mt::TransactionProvider txs{*database_};

    return txs.run(
        [&](mt::Transaction& tx) -> std::optional<ClaimedMessage>
        {
            auto query = mt::QuerySpec::where_json_eq(
                "$.status", mt::Json(status_to_string(MessageStatus::Pending))
            );
            query.limit = std::size_t{1};

            auto pending = messages.query(tx, query);
            if (pending.empty())
            {
                return std::nullopt;
            }

            auto row = std::move(pending.front());
            row.status = status_to_string(MessageStatus::Claimed);
            row.workerId = worker_id;
            row.claimedAtMs = now_ms;
            row.claimedUntilMs = now_ms + config_.visibility_timeout_ms;
            row.processedAtMs = std::nullopt;
            row.attempt += 1;
            messages.put(tx, row);

            return to_claimed_message(row);
        }
    );
}

void Queue::ack(
    std::string id,
    std::string worker_id,
    std::int64_t now_ms
) const
{
    auto messages = queue_table(*database_);
    mt::TransactionProvider txs{*database_};

    txs.run(
        [&](mt::Transaction& tx)
        {
            auto row = messages.require(tx, id);
            require_claimed_by(row, worker_id);

            row.status = status_to_string(MessageStatus::Processed);
            row.processedAtMs = now_ms;
            row.claimedAtMs = std::nullopt;
            row.claimedUntilMs = std::nullopt;
            messages.put(tx, row);
        }
    );
}

void Queue::fail(
    std::string id,
    std::string worker_id
) const
{
    auto messages = queue_table(*database_);
    mt::TransactionProvider txs{*database_};

    txs.run(
        [&](mt::Transaction& tx)
        {
            auto row = messages.require(tx, id);
            require_claimed_by(row, worker_id);

            row.status = status_to_string(MessageStatus::Pending);
            row.workerId = std::nullopt;
            row.claimedAtMs = std::nullopt;
            row.claimedUntilMs = std::nullopt;
            messages.put(tx, row);
        }
    );
}

std::size_t Queue::reap_expired(std::int64_t now_ms) const
{
    auto messages = queue_table(*database_);
    mt::TransactionProvider txs{*database_};

    return txs.run(
        [&](mt::Transaction& tx) -> std::size_t
        {
            auto claimed = messages.query(
                tx, mt::QuerySpec::where_json_eq(
                        "$.status", mt::Json(status_to_string(MessageStatus::Claimed))
                    )
            );

            std::size_t reaped = 0;
            for (auto& row : claimed)
            {
                if (!row.claimedUntilMs || *row.claimedUntilMs > now_ms)
                {
                    continue;
                }

                row.status = status_to_string(MessageStatus::Pending);
                row.workerId = std::nullopt;
                row.claimedAtMs = std::nullopt;
                row.claimedUntilMs = std::nullopt;
                messages.put(tx, row);
                ++reaped;
            }

            return reaped;
        }
    );
}

std::optional<QueuedMessage> Queue::get(const std::string& id) const
{
    auto messages = queue_table(*database_);
    auto row = messages.get(id);
    if (!row)
    {
        return std::nullopt;
    }

    return to_public_message(*row);
}

} // namespace qu
