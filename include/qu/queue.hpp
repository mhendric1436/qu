#pragma once

#include "mt/database.hpp"
#include "mt/json.hpp"
#include "mt/transaction.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qu
{

enum class MessageStatus
{
    Pending,
    Claimed,
    Processed
};

struct QueueConfig
{
    std::int64_t visibility_timeout_ms = 30000;
};

struct QueuedMessage
{
    std::string namespace_name;
    std::string channel_name;
    std::string id;
    MessageStatus status = MessageStatus::Pending;
    mt::Json payload = mt::Json::object({});
    std::optional<std::string> worker_id;
    std::int64_t created_at_ms = 0;
    std::optional<std::int64_t> claimed_at_ms;
    std::optional<std::int64_t> claimed_until_ms;
    std::optional<std::int64_t> processed_at_ms;
    std::int64_t attempt = 0;
};

struct ClaimedMessage
{
    std::string namespace_name;
    std::string channel_name;
    std::string id;
    mt::Json payload = mt::Json::object({});
    std::string worker_id;
    std::int64_t attempt = 0;
    std::int64_t claimed_until_ms = 0;
};

class QueueError : public std::runtime_error
{
  public:
    explicit QueueError(const std::string& message);
};

class DuplicateMessage : public QueueError
{
  public:
    explicit DuplicateMessage(const std::string& id);
};

class InvalidMessageState : public QueueError
{
  public:
    explicit InvalidMessageState(const std::string& message);
};

class Queue
{
  public:
    explicit Queue(
        mt::Database& database,
        QueueConfig config = {}
    );

    void enqueue(
        std::string id,
        mt::Json payload,
        std::int64_t now_ms
    ) const;

    void enqueue(
        std::string namespace_name,
        std::string channel_name,
        std::string id,
        mt::Json payload,
        std::int64_t now_ms
    ) const;

    void enqueue(
        mt::Transaction& tx,
        std::string id,
        mt::Json payload,
        std::int64_t now_ms
    ) const;

    void enqueue(
        mt::Transaction& tx,
        std::string namespace_name,
        std::string channel_name,
        std::string id,
        mt::Json payload,
        std::int64_t now_ms
    ) const;

    std::optional<ClaimedMessage> claim_next(
        std::string worker_id,
        std::int64_t now_ms
    ) const;

    std::optional<ClaimedMessage> claim_next(
        std::string namespace_name,
        std::string channel_name,
        std::string worker_id,
        std::int64_t now_ms
    ) const;

    std::optional<ClaimedMessage> claim_next(
        mt::Transaction& tx,
        std::string worker_id,
        std::int64_t now_ms
    ) const;

    std::optional<ClaimedMessage> claim_next(
        mt::Transaction& tx,
        std::string namespace_name,
        std::string channel_name,
        std::string worker_id,
        std::int64_t now_ms
    ) const;

    void
    ack(std::string id,
        std::string worker_id,
        std::int64_t now_ms) const;

    void
    ack(std::string namespace_name,
        std::string channel_name,
        std::string id,
        std::string worker_id,
        std::int64_t now_ms) const;

    void
    ack(mt::Transaction& tx,
        std::string id,
        std::string worker_id,
        std::int64_t now_ms) const;

    void
    ack(mt::Transaction& tx,
        std::string namespace_name,
        std::string channel_name,
        std::string id,
        std::string worker_id,
        std::int64_t now_ms) const;

    void fail(
        std::string id,
        std::string worker_id
    ) const;

    void fail(
        std::string namespace_name,
        std::string channel_name,
        std::string id,
        std::string worker_id
    ) const;

    void fail(
        mt::Transaction& tx,
        std::string id,
        std::string worker_id
    ) const;

    void fail(
        mt::Transaction& tx,
        std::string namespace_name,
        std::string channel_name,
        std::string id,
        std::string worker_id
    ) const;

    std::size_t reap_expired(std::int64_t now_ms) const;

    std::size_t reap_expired(
        std::string namespace_name,
        std::string channel_name,
        std::int64_t now_ms
    ) const;

    std::size_t reap_expired(
        mt::Transaction& tx,
        std::int64_t now_ms
    ) const;

    std::size_t reap_expired(
        mt::Transaction& tx,
        std::string namespace_name,
        std::string channel_name,
        std::int64_t now_ms
    ) const;

    std::optional<QueuedMessage> get(const std::string& id) const;

    std::optional<QueuedMessage>
    get(const std::string& namespace_name,
        const std::string& channel_name,
        const std::string& id) const;

    std::optional<QueuedMessage>
    get(mt::Transaction& tx,
        const std::string& id) const;

    std::optional<QueuedMessage>
    get(mt::Transaction& tx,
        const std::string& namespace_name,
        const std::string& channel_name,
        const std::string& id) const;

    std::vector<std::string> list_namespaces() const;

    std::vector<std::string> list_namespaces(mt::Transaction& tx) const;

    std::vector<std::string> list_channels(const std::string& namespace_name) const;

    std::vector<std::string> list_channels(
        mt::Transaction& tx,
        const std::string& namespace_name
    ) const;

  private:
    mt::Database* database_ = nullptr;
    QueueConfig config_;
};

} // namespace qu
