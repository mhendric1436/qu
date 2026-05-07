#include "catch2/catch_amalgamated.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "mt/errors.hpp"
#include "qu/queue.hpp"

#include <memory>
#include <string>

namespace
{

struct TestContext
{
    std::shared_ptr<mt::backends::memory::MemoryBackend> backend =
        std::make_shared<mt::backends::memory::MemoryBackend>();
    mt::Database database{backend};
    qu::Queue queue{database, qu::QueueConfig{.visibility_timeout_ms = 100}};
};

} // namespace

TEST_CASE("enqueue stores a pending message")
{
    TestContext ctx;

    ctx.queue.enqueue("msg-1", mt::Json::object({{"kind", "created"}}), 1000);

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->id == "msg-1");
    CHECK(message->status == qu::MessageStatus::Pending);
    CHECK(message->payload["kind"].as_string() == "created");
    CHECK(message->created_at_ms == 1000);
    CHECK(message->attempt == 0);
}

TEST_CASE("duplicate enqueue is rejected")
{
    TestContext ctx;

    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);

    CHECK_THROWS_AS(ctx.queue.enqueue("msg-1", mt::Json::object({}), 1001), qu::DuplicateMessage);
}

TEST_CASE("claim transitions one pending message to claimed")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({{"kind", "created"}}), 1000);

    auto claimed = ctx.queue.claim_next("worker-1", 1100);

    REQUIRE(claimed.has_value());
    CHECK(claimed->id == "msg-1");
    CHECK(claimed->worker_id == "worker-1");
    CHECK(claimed->claimed_until_ms == 1200);
    CHECK(claimed->attempt == 1);

    auto stored = ctx.queue.get("msg-1");
    REQUIRE(stored.has_value());
    CHECK(stored->status == qu::MessageStatus::Claimed);
    REQUIRE(stored->worker_id.has_value());
    CHECK(*stored->worker_id == "worker-1");
}

TEST_CASE("two consumers do not claim the same pending message")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);

    auto first = ctx.queue.claim_next("worker-1", 1100);
    auto second = ctx.queue.claim_next("worker-2", 1101);

    REQUIRE(first.has_value());
    CHECK_FALSE(second.has_value());
}

TEST_CASE("ack marks a claimed message as processed")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("worker-1", 1100).has_value());

    ctx.queue.ack("msg-1", "worker-1", 1150);

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Processed);
    REQUIRE(message->worker_id.has_value());
    CHECK(*message->worker_id == "worker-1");
    REQUIRE(message->processed_at_ms.has_value());
    CHECK(*message->processed_at_ms == 1150);
}

TEST_CASE("ack by another worker is rejected")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("worker-1", 1100).has_value());

    CHECK_THROWS_AS(ctx.queue.ack("msg-1", "worker-2", 1150), qu::InvalidMessageState);
}

TEST_CASE("fail returns a claimed message to pending")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("worker-1", 1100).has_value());

    ctx.queue.fail("msg-1", "worker-1");

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Pending);
    CHECK_FALSE(message->worker_id.has_value());
    CHECK_FALSE(message->claimed_until_ms.has_value());

    auto reclaimed = ctx.queue.claim_next("worker-2", 1200);
    REQUIRE(reclaimed.has_value());
    CHECK(reclaimed->attempt == 2);
}

TEST_CASE("expired claims are reaped back to pending")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("worker-1", 1100).has_value());

    CHECK(ctx.queue.reap_expired(1200) == 1);

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Pending);
    CHECK_FALSE(message->worker_id.has_value());
}

TEST_CASE("non-expired claims are not reaped")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("worker-1", 1100).has_value());

    CHECK(ctx.queue.reap_expired(1199) == 0);

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Claimed);
}
