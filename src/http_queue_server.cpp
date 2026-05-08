#include "qu/http_queue_server.hpp"

#include "httplib/httplib.h"
#include "mt/json_parser.hpp"

#include <exception>
#include <functional>
#include <stdexcept>
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

struct HttpQueueServer::Impl
{
    QueueService& service;
    int port;
    bool bound = false;
    httplib::Server server;

    Impl(
        QueueService& service,
        int port
    )
        : service(service),
          port(port)
    {
        register_routes();
    }

    int bind()
    {
        int actual = -1;
        if (port == 0)
        {
            actual = server.bind_to_any_port("0.0.0.0");
        }
        else
        {
            actual = server.bind_to_port("0.0.0.0", port) ? port : -1;
        }

        if (actual < 0)
        {
            throw std::runtime_error("failed to bind server on port " + std::to_string(port));
        }

        bound = true;
        return actual;
    }

    void start()
    {
        if (!bound)
        {
            (void)bind();
        }
        if (!server.listen_after_bind())
        {
            throw std::runtime_error("failed to start server on port " + std::to_string(port));
        }
    }

    void stop()
    {
        server.stop();
    }

    void register_routes()
    {
        server.Get(
            R"(/v1/namespaces)", [this](const httplib::Request&, httplib::Response& response)
            { write_result(response, service.list_namespaces()); }
        );

        server.Get(
            R"(/v1/namespaces/([^/]+)/channels)",
            [this](const httplib::Request& request, httplib::Response& response)
            { write_result(response, service.list_channels(request.matches[1])); }
        );

        server.Post(
            R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto namespace_name = request.matches[1].str();
                auto channel_name = request.matches[2].str();
                handle_json_request(
                    request, response, [&](const mt::Json& body)
                    { return service.enqueue_message(namespace_name, channel_name, body); }
                );
            }
        );

        server.Get(
            R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages/([^/]+))",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                write_result(
                    response,
                    service.get_message(request.matches[1], request.matches[2], request.matches[3])
                );
            }
        );

        server.Post(
            R"(/v1/namespaces/([^/]+)/channels/([^/]+)/claims)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto namespace_name = request.matches[1].str();
                auto channel_name = request.matches[2].str();
                handle_json_request(
                    request, response, [&](const mt::Json& body)
                    { return service.claim_next(namespace_name, channel_name, body); }
                );
            }
        );

        server.Post(
            R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages/([^/]+)/ack)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto namespace_name = request.matches[1].str();
                auto channel_name = request.matches[2].str();
                auto message_id = request.matches[3].str();
                handle_json_request(
                    request, response, [&](const mt::Json& body)
                    { return service.ack_message(namespace_name, channel_name, message_id, body); }
                );
            }
        );

        server.Post(
            R"(/v1/namespaces/([^/]+)/channels/([^/]+)/messages/([^/]+)/fail)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto namespace_name = request.matches[1].str();
                auto channel_name = request.matches[2].str();
                auto message_id = request.matches[3].str();
                handle_json_request(
                    request, response, [&](const mt::Json& body)
                    { return service.fail_message(namespace_name, channel_name, message_id, body); }
                );
            }
        );

        server.Post(
            R"(/v1/namespaces/([^/]+)/channels/([^/]+)/reap-expired)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto namespace_name = request.matches[1].str();
                auto channel_name = request.matches[2].str();
                handle_json_request(
                    request, response, [&](const mt::Json& body)
                    { return service.reap_expired(namespace_name, channel_name, body); }
                );
            }
        );
    }
};

HttpQueueServer::HttpQueueServer(
    QueueService& service,
    int port
)
    : impl_(
          std::make_unique<Impl>(
              service,
              port
          )
      )
{
}

HttpQueueServer::~HttpQueueServer() = default;

int HttpQueueServer::bind()
{
    return impl_->bind();
}

void HttpQueueServer::start()
{
    impl_->start();
}

void HttpQueueServer::stop()
{
    impl_->stop();
}

} // namespace qu
