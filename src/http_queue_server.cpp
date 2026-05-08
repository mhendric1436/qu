#include "qu/http_queue_server.hpp"

#include "mt/json_parser.hpp"

#include <exception>
#include <functional>
#include <string>

namespace qu
{
namespace
{

constexpr const char* JSON_CONTENT_TYPE = "application/json";

void write_result(
    httplib::Response& response,
    const QueueServiceResult& result
)
{
    response.status = http_status(result.status);
    if (has_response_body(result.status))
    {
        response.set_content(result.body.canonical_string(), JSON_CONTENT_TYPE);
    }
}

mt::Json parse_body(const httplib::Request& request)
{
    if (request.body.empty())
    {
        return mt::Json::object({});
    }
    return mt::parse_json(request.body);
}

void write_invalid_json(
    httplib::Response& response,
    const std::exception& error
)
{
    response.status = 400;
    response.set_content(
        queue_error_json("invalid_json", error.what()).canonical_string(), JSON_CONTENT_TYPE
    );
}

using JsonHandler = std::function<QueueServiceResult(const mt::Json&)>;

void handle_json_request(
    const httplib::Request& request,
    httplib::Response& response,
    const JsonHandler& handler
)
{
    try
    {
        write_result(response, handler(parse_body(request)));
    }
    catch (const std::exception& error)
    {
        write_invalid_json(response, error);
    }
}

} // namespace

HttpQueueServer::HttpQueueServer(QueueService& service)
    : service_(&service)
{
    register_routes();
}

bool HttpQueueServer::listen(
    const std::string& host,
    int port
)
{
    return server_.listen(host, port);
}

void HttpQueueServer::stop()
{
    server_.stop();
}

httplib::Server& HttpQueueServer::server()
{
    return server_;
}

void HttpQueueServer::register_routes()
{
    server_.Get(
        R"(/v1/namespaces)", [this](const httplib::Request&, httplib::Response& response)
        { write_result(response, service_->list_namespaces()); }
    );

    server_.Get(
        R"(/v1/namespaces/([^/]+)/channels)",
        [this](const httplib::Request& request, httplib::Response& response)
        { write_result(response, service_->list_channels(request.matches[1])); }
    );

    server_.Post(
        R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages)",
        [this](const httplib::Request& request, httplib::Response& response)
        {
            auto namespace_name = request.matches[1].str();
            auto channel_name = request.matches[2].str();
            handle_json_request(
                request, response, [&](const mt::Json& body)
                { return service_->enqueue_message(namespace_name, channel_name, body); }
            );
        }
    );

    server_.Get(
        R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages/([^/]+))",
        [this](const httplib::Request& request, httplib::Response& response)
        {
            write_result(
                response,
                service_->get_message(request.matches[1], request.matches[2], request.matches[3])
            );
        }
    );

    server_.Post(
        R"(/v1/namespaces/([^/]+)/channels/([^/]+)/claims)",
        [this](const httplib::Request& request, httplib::Response& response)
        {
            auto namespace_name = request.matches[1].str();
            auto channel_name = request.matches[2].str();
            handle_json_request(
                request, response, [&](const mt::Json& body)
                { return service_->claim_next(namespace_name, channel_name, body); }
            );
        }
    );

    server_.Post(
        R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages/([^/]+)/ack)",
        [this](const httplib::Request& request, httplib::Response& response)
        {
            auto namespace_name = request.matches[1].str();
            auto channel_name = request.matches[2].str();
            auto message_id = request.matches[3].str();
            handle_json_request(
                request, response, [&](const mt::Json& body)
                { return service_->ack_message(namespace_name, channel_name, message_id, body); }
            );
        }
    );

    server_.Post(
        R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages/([^/]+)/fail)",
        [this](const httplib::Request& request, httplib::Response& response)
        {
            auto namespace_name = request.matches[1].str();
            auto channel_name = request.matches[2].str();
            auto message_id = request.matches[3].str();
            handle_json_request(
                request, response, [&](const mt::Json& body)
                { return service_->fail_message(namespace_name, channel_name, message_id, body); }
            );
        }
    );

    server_.Post(
        R"(/v1/namespaces/([^/]+)/channels/([^/]+)/reap-expired)",
        [this](const httplib::Request& request, httplib::Response& response)
        {
            auto namespace_name = request.matches[1].str();
            auto channel_name = request.matches[2].str();
            handle_json_request(
                request, response, [&](const mt::Json& body)
                { return service_->reap_expired(namespace_name, channel_name, body); }
            );
        }
    );
}

} // namespace qu
