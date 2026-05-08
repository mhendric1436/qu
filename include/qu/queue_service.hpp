#pragma once

#include "mt/json.hpp"
#include "qu/queue.hpp"

#include <optional>
#include <string>

namespace qu
{

enum class QueueServiceStatus
{
    Ok,
    Created,
    NoContent,
    BadRequest,
    NotFound,
    Conflict,
    InternalError
};

struct QueueServiceResult
{
    QueueServiceStatus status = QueueServiceStatus::Ok;
    mt::Json body = mt::Json::null();
};

class QueueService
{
  public:
    explicit QueueService(Queue& queue);

    QueueServiceResult list_namespaces() const;
    QueueServiceResult list_channels(const std::string& namespace_name) const;

    QueueServiceResult enqueue_message(
        const std::string& namespace_name,
        const std::string& channel_name,
        const mt::Json& request
    ) const;

    QueueServiceResult get_message(
        const std::string& namespace_name,
        const std::string& channel_name,
        const std::string& message_id
    ) const;

    QueueServiceResult claim_next(
        const std::string& namespace_name,
        const std::string& channel_name,
        const mt::Json& request
    ) const;

    QueueServiceResult ack_message(
        const std::string& namespace_name,
        const std::string& channel_name,
        const std::string& message_id,
        const mt::Json& request
    ) const;

    QueueServiceResult fail_message(
        const std::string& namespace_name,
        const std::string& channel_name,
        const std::string& message_id,
        const mt::Json& request
    ) const;

    QueueServiceResult reap_expired(
        const std::string& namespace_name,
        const std::string& channel_name,
        const mt::Json& request
    ) const;

  private:
    Queue* queue_ = nullptr;
};

int http_status(QueueServiceStatus status);
bool has_response_body(QueueServiceStatus status);
mt::Json queue_error_json(
    std::string code,
    std::string message
);

} // namespace qu
