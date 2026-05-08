#include "qu/queue.hpp"

#include "mt/query.hpp"
#include "mt/table.hpp"
#include "mt/transaction.hpp"
#include "tables/generated/queue_channel_counter_row.hpp"
#include "tables/generated/queue_message_row.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace qu
{
namespace
{

constexpr std::string_view STATUS_PENDING = "pending";
constexpr std::string_view STATUS_CLAIMED = "claimed";
constexpr std::string_view STATUS_PROCESSED = "processed";
constexpr std::string_view DEFAULT_NAMESPACE = "default";
constexpr std::string_view DEFAULT_CHANNEL = "default";

using QueueMessageTable = mt::Table<tables::QueueMessageRow, tables::QueueMessageRowMapping>;
using QueueChannelCounterTable =
    mt::Table<tables::QueueChannelCounterRow, tables::QueueChannelCounterRowMapping>;

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
        .namespace_name = row.namespaceName,
        .channel_name = row.channelName,
        .id = row.id,
        .status = status_from_string(row.status),
        .payload = row.payload,
        .consumer_id = row.consumerId,
        .sequence = row.sequence,
        .created_at_ms = row.createdAtMs,
        .claimed_at_ms = row.claimedAtMs,
        .claimed_until_ms = row.claimedUntilMs,
        .processed_at_ms = row.processedAtMs,
        .attempt = row.attempt
    };
}

ClaimedMessage to_claimed_message(const tables::QueueMessageRow& row)
{
    if (!row.consumerId || !row.claimedUntilMs)
    {
        throw InvalidMessageState("claimed message is missing claim metadata");
    }

    return ClaimedMessage{
        .namespace_name = row.namespaceName,
        .channel_name = row.channelName,
        .id = row.id,
        .payload = row.payload,
        .consumer_id = *row.consumerId,
        .sequence = row.sequence,
        .attempt = row.attempt,
        .claimed_until_ms = *row.claimedUntilMs
    };
}

QueueMessageTable queue_table(mt::Database& database)
{
    mt::TableProvider tables{database};
    return tables.table<tables::QueueMessageRow, tables::QueueMessageRowMapping>();
}

QueueChannelCounterTable queue_channel_counter_table(mt::Database& database)
{
    mt::TableProvider tables{database};
    return tables.table<tables::QueueChannelCounterRow, tables::QueueChannelCounterRowMapping>();
}

std::string message_key(
    const std::string& namespace_name,
    const std::string& channel_name,
    const std::string& id
)
{
    return tables::QueueMessageRowMapping::key(
        tables::QueueMessageRow{
            .namespaceName = namespace_name,
            .channelName = channel_name,
            .id = id,
            .status = std::string(STATUS_PENDING),
            .sequence = 0,
            .createdAtMs = 0
        }
    );
}

std::string counter_key(
    const std::string& namespace_name,
    const std::string& channel_name
)
{
    return tables::QueueChannelCounterRowMapping::key(
        tables::QueueChannelCounterRow{
            .namespaceName = namespace_name, .channelName = channel_name, .nextSequence = 0
        }
    );
}

std::int64_t allocate_sequence(
    mt::Transaction& tx,
    const QueueChannelCounterTable& counters,
    const std::string& namespace_name,
    const std::string& channel_name
)
{
    auto key = counter_key(namespace_name, channel_name);
    auto counter = counters.get(tx, key).value_or(
        tables::QueueChannelCounterRow{
            .namespaceName = namespace_name, .channelName = channel_name, .nextSequence = 1
        }
    );

    auto sequence = counter.nextSequence;
    counter.nextSequence += 1;
    counters.put(tx, counter);
    return sequence;
}

mt::QuerySpec scoped_status_query(
    const std::string& namespace_name,
    const std::string& channel_name,
    MessageStatus status
)
{
    auto query = mt::QuerySpec::where_json_eq("$.namespaceName", mt::Json(namespace_name));
    query.predicates.push_back(
        mt::QueryPredicate{
            .op = mt::QueryOp::JsonEquals, .path = "$.channelName", .value = mt::Json(channel_name)
        }
    );
    query.predicates.push_back(
        mt::QueryPredicate{
            .op = mt::QueryOp::JsonEquals,
            .path = "$.status",
            .value = mt::Json(status_to_string(status))
        }
    );
    return query;
}

mt::QuerySpec namespace_query(const std::string& namespace_name)
{
    return mt::QuerySpec::where_json_eq("$.namespaceName", mt::Json(namespace_name));
}

void require_claimed_by(
    const tables::QueueMessageRow& row,
    const std::string& consumer_id
)
{
    if (row.status != STATUS_CLAIMED || !row.consumerId || *row.consumerId != consumer_id)
    {
        throw InvalidMessageState("message is not claimed by consumer");
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
    (void)queue_channel_counter_table(*database_);
}

void Queue::enqueue(
    std::string id,
    mt::Json payload,
    std::int64_t now_ms
) const
{
    enqueue(
        std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(id),
        std::move(payload), now_ms
    );
}

void Queue::enqueue(
    std::string namespace_name,
    std::string channel_name,
    std::string id,
    mt::Json payload,
    std::int64_t now_ms
) const
{
    mt::TransactionProvider txs{*database_};

    txs.run(
        [&](mt::Transaction& tx)
        {
            enqueue(
                tx, std::move(namespace_name), std::move(channel_name), std::move(id),
                std::move(payload), now_ms
            );
        }
    );
}

void Queue::enqueue(
    mt::Transaction& tx,
    std::string id,
    mt::Json payload,
    std::int64_t now_ms
) const
{
    enqueue(
        tx, std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(id),
        std::move(payload), now_ms
    );
}

void Queue::enqueue(
    mt::Transaction& tx,
    std::string namespace_name,
    std::string channel_name,
    std::string id,
    mt::Json payload,
    std::int64_t now_ms
) const
{
    auto messages = queue_table(*database_);
    auto counters = queue_channel_counter_table(*database_);
    auto key = message_key(namespace_name, channel_name, id);

    if (messages.get(tx, key))
    {
        throw DuplicateMessage(id);
    }

    auto sequence = allocate_sequence(tx, counters, namespace_name, channel_name);

    messages.put(
        tx, tables::QueueMessageRow{
                .namespaceName = std::move(namespace_name),
                .channelName = std::move(channel_name),
                .id = std::move(id),
                .status = status_to_string(MessageStatus::Pending),
                .payload = std::move(payload),
                .sequence = sequence,
                .createdAtMs = now_ms
            }
    );
}

std::optional<ClaimedMessage> Queue::claim_next(
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    return claim_next(
        std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(consumer_id), now_ms
    );
}

std::optional<ClaimedMessage> Queue::claim_next(
    std::string namespace_name,
    std::string channel_name,
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    mt::TransactionProvider txs{*database_};

    return txs.run(
        [&](mt::Transaction& tx) -> std::optional<ClaimedMessage>
        {
            return claim_next(
                tx, std::move(namespace_name), std::move(channel_name), std::move(consumer_id),
                now_ms
            );
        }
    );
}

std::optional<ClaimedMessage> Queue::claim_next(
    mt::Transaction& tx,
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    return claim_next(
        tx, std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(consumer_id),
        now_ms
    );
}

std::optional<ClaimedMessage> Queue::claim_next(
    mt::Transaction& tx,
    std::string namespace_name,
    std::string channel_name,
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    auto messages = queue_table(*database_);
    auto query = scoped_status_query(namespace_name, channel_name, MessageStatus::Pending);
    auto pending = messages.query(tx, query);
    if (pending.empty())
    {
        return std::nullopt;
    }

    std::sort(
        pending.begin(), pending.end(),
        [](const tables::QueueMessageRow& lhs, const tables::QueueMessageRow& rhs)
        {
            if (lhs.sequence != rhs.sequence)
            {
                return lhs.sequence < rhs.sequence;
            }
            return lhs.id < rhs.id;
        }
    );

    auto row = std::move(pending.front());
    row.status = status_to_string(MessageStatus::Claimed);
    row.consumerId = std::move(consumer_id);
    row.claimedAtMs = now_ms;
    row.claimedUntilMs = now_ms + config_.visibility_timeout_ms;
    row.processedAtMs = std::nullopt;
    row.attempt += 1;
    messages.put(tx, row);

    return to_claimed_message(row);
}

void Queue::ack(
    std::string id,
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    ack(std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(id),
        std::move(consumer_id), now_ms);
}

void Queue::ack(
    std::string namespace_name,
    std::string channel_name,
    std::string id,
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    mt::TransactionProvider txs{*database_};

    txs.run(
        [&](mt::Transaction& tx)
        {
            ack(tx, std::move(namespace_name), std::move(channel_name), std::move(id),
                std::move(consumer_id), now_ms);
        }
    );
}

void Queue::ack(
    mt::Transaction& tx,
    std::string id,
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    ack(tx, std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(id),
        std::move(consumer_id), now_ms);
}

void Queue::ack(
    mt::Transaction& tx,
    std::string namespace_name,
    std::string channel_name,
    std::string id,
    std::string consumer_id,
    std::int64_t now_ms
) const
{
    auto messages = queue_table(*database_);
    auto row = messages.require(tx, message_key(namespace_name, channel_name, id));
    require_claimed_by(row, consumer_id);

    row.status = status_to_string(MessageStatus::Processed);
    row.processedAtMs = now_ms;
    row.claimedAtMs = std::nullopt;
    row.claimedUntilMs = std::nullopt;
    messages.put(tx, row);
}

void Queue::fail(
    std::string id,
    std::string consumer_id
) const
{
    fail(
        std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(id),
        std::move(consumer_id)
    );
}

void Queue::fail(
    std::string namespace_name,
    std::string channel_name,
    std::string id,
    std::string consumer_id
) const
{
    mt::TransactionProvider txs{*database_};

    txs.run(
        [&](mt::Transaction& tx)
        {
            fail(
                tx, std::move(namespace_name), std::move(channel_name), std::move(id),
                std::move(consumer_id)
            );
        }
    );
}

void Queue::fail(
    mt::Transaction& tx,
    std::string id,
    std::string consumer_id
) const
{
    fail(
        tx, std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), std::move(id),
        std::move(consumer_id)
    );
}

void Queue::fail(
    mt::Transaction& tx,
    std::string namespace_name,
    std::string channel_name,
    std::string id,
    std::string consumer_id
) const
{
    auto messages = queue_table(*database_);
    auto row = messages.require(tx, message_key(namespace_name, channel_name, id));
    require_claimed_by(row, consumer_id);

    row.status = status_to_string(MessageStatus::Pending);
    row.consumerId = std::nullopt;
    row.claimedAtMs = std::nullopt;
    row.claimedUntilMs = std::nullopt;
    messages.put(tx, row);
}

std::size_t Queue::reap_expired(std::int64_t now_ms) const
{
    return reap_expired(std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), now_ms);
}

std::size_t Queue::reap_expired(
    std::string namespace_name,
    std::string channel_name,
    std::int64_t now_ms
) const
{
    mt::TransactionProvider txs{*database_};

    return txs.run(
        [&](mt::Transaction& tx) -> std::size_t
        { return reap_expired(tx, std::move(namespace_name), std::move(channel_name), now_ms); }
    );
}

std::size_t Queue::reap_expired(
    mt::Transaction& tx,
    std::int64_t now_ms
) const
{
    return reap_expired(tx, std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), now_ms);
}

std::size_t Queue::reap_expired(
    mt::Transaction& tx,
    std::string namespace_name,
    std::string channel_name,
    std::int64_t now_ms
) const
{
    auto messages = queue_table(*database_);
    auto claimed = messages.query(
        tx, scoped_status_query(namespace_name, channel_name, MessageStatus::Claimed)
    );

    std::size_t reaped = 0;
    for (auto& row : claimed)
    {
        if (!row.claimedUntilMs || *row.claimedUntilMs > now_ms)
        {
            continue;
        }

        row.status = status_to_string(MessageStatus::Pending);
        row.consumerId = std::nullopt;
        row.claimedAtMs = std::nullopt;
        row.claimedUntilMs = std::nullopt;
        messages.put(tx, row);
        ++reaped;
    }

    return reaped;
}

std::optional<QueuedMessage> Queue::get(const std::string& id) const
{
    return get(std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), id);
}

std::optional<QueuedMessage> Queue::get(
    const std::string& namespace_name,
    const std::string& channel_name,
    const std::string& id
) const
{
    auto messages = queue_table(*database_);
    auto row = messages.get(message_key(namespace_name, channel_name, id));
    if (!row)
    {
        return std::nullopt;
    }

    return to_public_message(*row);
}

std::optional<QueuedMessage> Queue::get(
    mt::Transaction& tx,
    const std::string& id
) const
{
    return get(tx, std::string(DEFAULT_NAMESPACE), std::string(DEFAULT_CHANNEL), id);
}

std::optional<QueuedMessage> Queue::get(
    mt::Transaction& tx,
    const std::string& namespace_name,
    const std::string& channel_name,
    const std::string& id
) const
{
    auto messages = queue_table(*database_);
    auto row = messages.get(tx, message_key(namespace_name, channel_name, id));
    if (!row)
    {
        return std::nullopt;
    }

    return to_public_message(*row);
}

std::vector<std::string> Queue::list_namespaces() const
{
    auto messages = queue_table(*database_);
    std::set<std::string> namespaces;
    for (const auto& row : messages.list())
    {
        namespaces.insert(row.namespaceName);
    }

    return {namespaces.begin(), namespaces.end()};
}

std::vector<std::string> Queue::list_namespaces(mt::Transaction& tx) const
{
    auto messages = queue_table(*database_);
    std::set<std::string> namespaces;
    for (const auto& row : messages.list(tx))
    {
        namespaces.insert(row.namespaceName);
    }

    return {namespaces.begin(), namespaces.end()};
}

std::vector<std::string> Queue::list_channels(const std::string& namespace_name) const
{
    auto messages = queue_table(*database_);
    std::set<std::string> channels;
    for (const auto& row : messages.query(namespace_query(namespace_name)))
    {
        channels.insert(row.channelName);
    }

    return {channels.begin(), channels.end()};
}

std::vector<std::string> Queue::list_channels(
    mt::Transaction& tx,
    const std::string& namespace_name
) const
{
    auto messages = queue_table(*database_);
    std::set<std::string> channels;
    for (const auto& row : messages.query(tx, namespace_query(namespace_name)))
    {
        channels.insert(row.channelName);
    }

    return {channels.begin(), channels.end()};
}

} // namespace qu
