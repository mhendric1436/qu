#pragma once

#include "qu/queue_service.hpp"

#include "httplib/httplib.h"

#include <string>

namespace qu
{

class HttpQueueServer
{
  public:
    explicit HttpQueueServer(QueueService& service);

    bool listen(
        const std::string& host,
        int port
    );

    void stop();

    httplib::Server& server();

  private:
    void register_routes();

    QueueService* service_ = nullptr;
    httplib::Server server_;
};

} // namespace qu
