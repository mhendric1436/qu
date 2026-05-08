#pragma once

#include "qu/queue_service.hpp"

#include <memory>

namespace qu
{

class HttpQueueServer
{
  public:
    HttpQueueServer(
        QueueService& service,
        int port
    );
    ~HttpQueueServer();

    int bind();
    void start();
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace qu
