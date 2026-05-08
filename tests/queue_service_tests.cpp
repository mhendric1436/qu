#include "catch2/catch_amalgamated.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "qu/queue.hpp"
#include "qu/queue_service.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{

struct ServiceContext
{
    std::shared_ptr<mt::backends::memory::MemoryBackend> backend =
        std::make_shared<mt::backends::memory::MemoryBackend>();
    mt::Database database{backend};
    qu::Queue queue{database, qu::QueueConfig{.visibility_timeout_ms = 100}};
    qu::QueueService service{queue};
};

mt::Json enqueue_request(
    std::string id,
    std::int64_t now_ms
)
{
    return mt::Json::object(
        {{"id", std::move(id)},
         {"payload", mt::Json::object({{"kind", "created"}})},
         {"nowMs", now_ms}}
    );
}

mt::Json consumer_request(
    std::string consumer_id,
    std::int64_t now_ms
)
{
    return mt::Json::object({{"consumerId", std::move(consumer_id)}, {"nowMs", now_ms}});
}

} // namespace

TEST_CASE("queue service enqueues and gets messages")
{
    ServiceContext ctx;

    auto created = ctx.service.enqueue_message("orders", "created", enqueue_request("msg-1", 1000));

    REQUIRE(created.status == qu::QueueServiceStatus::Created);
    CHECK(created.body["namespaceName"].as_string() == "orders");
    CHECK(created.body["channelName"].as_string() == "created");
    CHECK(created.body["id"].as_string() == "msg-1");
    CHECK(created.body["sequence"].as_int64() == 1);

    auto found = ctx.service.get_message("orders", "created", "msg-1");
    REQUIRE(found.status == qu::QueueServiceStatus::Ok);
    CHECK(found.body["payload"]["kind"].as_string() == "created");
}

TEST_CASE("queue service maps duplicate enqueue to conflict")
{
    ServiceContext ctx;

    REQUIRE(
        ctx.service.enqueue_message("orders", "created", enqueue_request("msg-1", 1000)).status ==
        qu::QueueServiceStatus::Created
    );

    auto duplicate =
        ctx.service.enqueue_message("orders", "created", enqueue_request("msg-1", 1001));

    CHECK(duplicate.status == qu::QueueServiceStatus::Conflict);
    CHECK(duplicate.body["code"].as_string() == "duplicate_message");
}

TEST_CASE("queue service claims in FIFO sequence order")
{
    ServiceContext ctx;

    REQUIRE(
        ctx.service.enqueue_message("orders", "created", enqueue_request("msg-c", 3000)).status ==
        qu::QueueServiceStatus::Created
    );
    REQUIRE(
        ctx.service.enqueue_message("orders", "created", enqueue_request("msg-a", 1000)).status ==
        qu::QueueServiceStatus::Created
    );

    auto first = ctx.service.claim_next("orders", "created", consumer_request("consumer-1", 4000));
    auto second = ctx.service.claim_next("orders", "created", consumer_request("consumer-1", 4001));

    REQUIRE(first.status == qu::QueueServiceStatus::Ok);
    REQUIRE(second.status == qu::QueueServiceStatus::Ok);
    CHECK(first.body["id"].as_string() == "msg-c");
    CHECK(second.body["id"].as_string() == "msg-a");
}

TEST_CASE("queue service returns no content when claim is empty")
{
    ServiceContext ctx;

    auto result = ctx.service.claim_next("orders", "created", consumer_request("consumer-1", 1000));

    CHECK(result.status == qu::QueueServiceStatus::NoContent);
}

TEST_CASE("queue service acknowledges claimed messages")
{
    ServiceContext ctx;
    REQUIRE(
        ctx.service.enqueue_message("orders", "created", enqueue_request("msg-1", 1000)).status ==
        qu::QueueServiceStatus::Created
    );
    REQUIRE(
        ctx.service.claim_next("orders", "created", consumer_request("consumer-1", 1100)).status ==
        qu::QueueServiceStatus::Ok
    );

    auto ack =
        ctx.service.ack_message("orders", "created", "msg-1", consumer_request("consumer-1", 1200));

    CHECK(ack.status == qu::QueueServiceStatus::NoContent);
    auto stored = ctx.service.get_message("orders", "created", "msg-1");
    REQUIRE(stored.status == qu::QueueServiceStatus::Ok);
    CHECK(stored.body["status"].as_string() == "processed");
}

TEST_CASE("queue service rejects malformed requests")
{
    ServiceContext ctx;

    auto result = ctx.service.enqueue_message("orders", "created", mt::Json::object({}));

    CHECK(result.status == qu::QueueServiceStatus::BadRequest);
    CHECK(result.body["code"].as_string() == "invalid_request");
}
