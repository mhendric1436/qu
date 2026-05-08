#pragma once

#include "qu/queue_service.hpp"

#include <memory>

namespace qu
{

class QueueHttpServer
{
  public:
    QueueHttpServer(
        QueueService& service,
        int port
    );
    ~QueueHttpServer();

    int bind();
    void start();
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace qu
