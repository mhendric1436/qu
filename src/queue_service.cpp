#include "qu/queue_service.hpp"

#include "mt/errors.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace qu
{
namespace
{

const mt::Json& required_field(
    const mt::Json& object,
    const std::string& field
)
{
    if (!object.is_object())
    {
        throw QueueError("request body must be a JSON object");
    }

    const auto& values = object.as_object();
    auto it = values.find(field);
    if (it == values.end())
    {
        throw QueueError("missing required field: " + field);
    }
    return it->second;
}

std::string required_string(
    const mt::Json& object,
    const std::string& field
)
{
    const auto& value = required_field(object, field);
    if (!value.is_string() || value.as_string().empty())
    {
        throw QueueError("field must be a non-empty string: " + field);
    }
    return value.as_string();
}

std::int64_t required_int64(
    const mt::Json& object,
    const std::string& field
)
{
    const auto& value = required_field(object, field);
    if (!value.is_int64())
    {
        throw QueueError("field must be an int64: " + field);
    }
    return value.as_int64();
}

mt::Json required_payload(const mt::Json& object)
{
    return required_field(object, "payload");
}

mt::Json string_array_json(const std::vector<std::string>& values)
{
    mt::Json::Array items;
    items.reserve(values.size());
    for (const auto& value : values)
    {
        items.emplace_back(value);
    }
    return mt::Json::array(std::move(items));
}

std::string status_json(MessageStatus status)
{
    switch (status)
    {
    case MessageStatus::Pending:
        return "pending";
    case MessageStatus::Claimed:
        return "claimed";
    case MessageStatus::Processed:
        return "processed";
    }

    throw QueueError("unknown message status");
}

mt::Json optional_string_json(const std::optional<std::string>& value)
{
    return value ? mt::Json(*value) : mt::Json::null();
}

mt::Json optional_int64_json(const std::optional<std::int64_t>& value)
{
    return value ? mt::Json(*value) : mt::Json::null();
}

mt::Json queued_message_json(const QueuedMessage& message)
{
    return mt::Json::object(
        {{"namespaceName", message.namespace_name},
         {"channelName", message.channel_name},
         {"id", message.id},
         {"status", status_json(message.status)},
         {"payload", message.payload},
         {"consumerId", optional_string_json(message.consumer_id)},
         {"sequence", message.sequence},
         {"createdAtMs", message.created_at_ms},
         {"claimedAtMs", optional_int64_json(message.claimed_at_ms)},
         {"claimedUntilMs", optional_int64_json(message.claimed_until_ms)},
         {"processedAtMs", optional_int64_json(message.processed_at_ms)},
         {"attempt", message.attempt}}
    );
}

mt::Json claimed_message_json(const ClaimedMessage& message)
{
    return mt::Json::object(
        {{"namespaceName", message.namespace_name},
         {"channelName", message.channel_name},
         {"id", message.id},
         {"payload", message.payload},
         {"consumerId", message.consumer_id},
         {"sequence", message.sequence},
         {"attempt", message.attempt},
         {"claimedUntilMs", message.claimed_until_ms}}
    );
}

QueueServiceResult bad_request(const std::exception& error)
{
    return QueueServiceResult{
        .status = QueueServiceStatus::BadRequest,
        .body = queue_error_json("invalid_request", error.what())
    };
}

QueueServiceResult conflict(
    std::string code,
    const std::exception& error
)
{
    return QueueServiceResult{
        .status = QueueServiceStatus::Conflict,
        .body = queue_error_json(std::move(code), error.what())
    };
}

} // namespace

QueueService::QueueService(Queue& queue)
    : queue_(&queue)
{
}

QueueServiceResult QueueService::list_namespaces() const
{
    return QueueServiceResult{
        .status = QueueServiceStatus::Ok,
        .body = mt::Json::object({{"namespaces", string_array_json(queue_->list_namespaces())}})
    };
}

QueueServiceResult QueueService::list_channels(const std::string& namespace_name) const
{
    return QueueServiceResult{
        .status = QueueServiceStatus::Ok,
        .body = mt::Json::object(
            {{"namespaceName", namespace_name},
             {"channels", string_array_json(queue_->list_channels(namespace_name))}}
        )
    };
}

QueueServiceResult QueueService::enqueue_message(
    const std::string& namespace_name,
    const std::string& channel_name,
    const mt::Json& request
) const
{
    try
    {
        auto id = required_string(request, "id");
        auto payload = required_payload(request);
        auto now_ms = required_int64(request, "nowMs");

        queue_->enqueue(namespace_name, channel_name, id, std::move(payload), now_ms);
        auto message = queue_->get(namespace_name, channel_name, id);
        if (!message)
        {
            return QueueServiceResult{
                .status = QueueServiceStatus::InternalError,
                .body = queue_error_json("internal_error", "enqueued message was not readable")
            };
        }

        return QueueServiceResult{
            .status = QueueServiceStatus::Created, .body = queued_message_json(*message)
        };
    }
    catch (const DuplicateMessage& error)
    {
        return conflict("duplicate_message", error);
    }
    catch (const QueueError& error)
    {
        return bad_request(error);
    }
}

QueueServiceResult QueueService::get_message(
    const std::string& namespace_name,
    const std::string& channel_name,
    const std::string& message_id
) const
{
    auto message = queue_->get(namespace_name, channel_name, message_id);
    if (!message)
    {
        return QueueServiceResult{
            .status = QueueServiceStatus::NotFound,
            .body = queue_error_json("not_found", "message was not found")
        };
    }

    return QueueServiceResult{
        .status = QueueServiceStatus::Ok, .body = queued_message_json(*message)
    };
}

QueueServiceResult QueueService::claim_next(
    const std::string& namespace_name,
    const std::string& channel_name,
    const mt::Json& request
) const
{
    try
    {
        auto consumer_id = required_string(request, "consumerId");
        auto now_ms = required_int64(request, "nowMs");

        auto claimed = queue_->claim_next(namespace_name, channel_name, consumer_id, now_ms);
        if (!claimed)
        {
            return QueueServiceResult{.status = QueueServiceStatus::NoContent};
        }

        return QueueServiceResult{
            .status = QueueServiceStatus::Ok, .body = claimed_message_json(*claimed)
        };
    }
    catch (const QueueError& error)
    {
        return bad_request(error);
    }
}

QueueServiceResult QueueService::ack_message(
    const std::string& namespace_name,
    const std::string& channel_name,
    const std::string& message_id,
    const mt::Json& request
) const
{
    try
    {
        auto consumer_id = required_string(request, "consumerId");
        auto now_ms = required_int64(request, "nowMs");
        queue_->ack(namespace_name, channel_name, message_id, consumer_id, now_ms);
        return QueueServiceResult{.status = QueueServiceStatus::NoContent};
    }
    catch (const mt::DocumentNotFound& error)
    {
        return QueueServiceResult{
            .status = QueueServiceStatus::NotFound,
            .body = queue_error_json("not_found", error.what())
        };
    }
    catch (const InvalidMessageState& error)
    {
        return conflict("invalid_message_state", error);
    }
    catch (const QueueError& error)
    {
        return bad_request(error);
    }
}

QueueServiceResult QueueService::fail_message(
    const std::string& namespace_name,
    const std::string& channel_name,
    const std::string& message_id,
    const mt::Json& request
) const
{
    try
    {
        auto consumer_id = required_string(request, "consumerId");
        (void)required_int64(request, "nowMs");
        queue_->fail(namespace_name, channel_name, message_id, consumer_id);
        return QueueServiceResult{.status = QueueServiceStatus::NoContent};
    }
    catch (const mt::DocumentNotFound& error)
    {
        return QueueServiceResult{
            .status = QueueServiceStatus::NotFound,
            .body = queue_error_json("not_found", error.what())
        };
    }
    catch (const InvalidMessageState& error)
    {
        return conflict("invalid_message_state", error);
    }
    catch (const QueueError& error)
    {
        return bad_request(error);
    }
}

QueueServiceResult QueueService::reap_expired(
    const std::string& namespace_name,
    const std::string& channel_name,
    const mt::Json& request
) const
{
    try
    {
        auto now_ms = required_int64(request, "nowMs");
        auto count =
            static_cast<std::int64_t>(queue_->reap_expired(namespace_name, channel_name, now_ms));

        return QueueServiceResult{
            .status = QueueServiceStatus::Ok, .body = mt::Json::object({{"reapedCount", count}})
        };
    }
    catch (const QueueError& error)
    {
        return bad_request(error);
    }
}

int http_status(QueueServiceStatus status)
{
    switch (status)
    {
    case QueueServiceStatus::Ok:
        return 200;
    case QueueServiceStatus::Created:
        return 201;
    case QueueServiceStatus::NoContent:
        return 204;
    case QueueServiceStatus::BadRequest:
        return 400;
    case QueueServiceStatus::NotFound:
        return 404;
    case QueueServiceStatus::Conflict:
        return 409;
    case QueueServiceStatus::InternalError:
        return 500;
    }

    return 500;
}

bool has_response_body(QueueServiceStatus status)
{
    return status != QueueServiceStatus::NoContent;
}

mt::Json queue_error_json(
    std::string code,
    std::string message
)
{
    return mt::Json::object({{"code", std::move(code)}, {"message", std::move(message)}});
}

} // namespace qu
