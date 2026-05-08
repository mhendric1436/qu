#include "catch2/catch_amalgamated.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "mt/errors.hpp"
#include "qu/queue.hpp"

#include <memory>
#include <string>
#include <vector>

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

    auto claimed = ctx.queue.claim_next("consumer-1", 1100);

    REQUIRE(claimed.has_value());
    CHECK(claimed->id == "msg-1");
    CHECK(claimed->consumer_id == "consumer-1");
    CHECK(claimed->claimed_until_ms == 1200);
    CHECK(claimed->attempt == 1);

    auto stored = ctx.queue.get("msg-1");
    REQUIRE(stored.has_value());
    CHECK(stored->status == qu::MessageStatus::Claimed);
    REQUIRE(stored->consumer_id.has_value());
    CHECK(*stored->consumer_id == "consumer-1");
}

TEST_CASE("two consumers do not claim the same pending message")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);

    auto first = ctx.queue.claim_next("consumer-1", 1100);
    auto second = ctx.queue.claim_next("consumer-2", 1101);

    REQUIRE(first.has_value());
    CHECK_FALSE(second.has_value());
}

TEST_CASE("ack marks a claimed message as processed")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("consumer-1", 1100).has_value());

    ctx.queue.ack("msg-1", "consumer-1", 1150);

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Processed);
    REQUIRE(message->consumer_id.has_value());
    CHECK(*message->consumer_id == "consumer-1");
    REQUIRE(message->processed_at_ms.has_value());
    CHECK(*message->processed_at_ms == 1150);
}

TEST_CASE("ack by another consumer is rejected")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("consumer-1", 1100).has_value());

    CHECK_THROWS_AS(ctx.queue.ack("msg-1", "consumer-2", 1150), qu::InvalidMessageState);
}

TEST_CASE("fail returns a claimed message to pending")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("consumer-1", 1100).has_value());

    ctx.queue.fail("msg-1", "consumer-1");

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Pending);
    CHECK_FALSE(message->consumer_id.has_value());
    CHECK_FALSE(message->claimed_until_ms.has_value());

    auto reclaimed = ctx.queue.claim_next("consumer-2", 1200);
    REQUIRE(reclaimed.has_value());
    CHECK(reclaimed->attempt == 2);
}

TEST_CASE("expired claims are reaped back to pending")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("consumer-1", 1100).has_value());

    CHECK(ctx.queue.reap_expired(1200) == 1);

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Pending);
    CHECK_FALSE(message->consumer_id.has_value());
}

TEST_CASE("non-expired claims are not reaped")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("consumer-1", 1100).has_value());

    CHECK(ctx.queue.reap_expired(1199) == 0);

    auto message = ctx.queue.get("msg-1");
    REQUIRE(message.has_value());
    CHECK(message->status == qu::MessageStatus::Claimed);
}

TEST_CASE("queue operations can use caller-owned transactions")
{
    TestContext ctx;
    mt::TransactionProvider txs{ctx.database};

    txs.run(
        [&](mt::Transaction& tx)
        {
            ctx.queue.enqueue(tx, "msg-1", mt::Json::object({{"kind", "created"}}), 1000);

            auto pending = ctx.queue.get(tx, "msg-1");
            REQUIRE(pending.has_value());
            CHECK(pending->status == qu::MessageStatus::Pending);

            auto claimed = ctx.queue.claim_next(tx, "consumer-1", 1100);
            REQUIRE(claimed.has_value());
            CHECK(claimed->id == "msg-1");

            ctx.queue.ack(tx, claimed->id, claimed->consumer_id, 1200);

            auto processed = ctx.queue.get(tx, "msg-1");
            REQUIRE(processed.has_value());
            CHECK(processed->status == qu::MessageStatus::Processed);
        }
    );

    auto stored = ctx.queue.get("msg-1");
    REQUIRE(stored.has_value());
    CHECK(stored->status == qu::MessageStatus::Processed);
}

TEST_CASE("reap_expired can use a caller-owned transaction")
{
    TestContext ctx;
    ctx.queue.enqueue("msg-1", mt::Json::object({}), 1000);
    REQUIRE(ctx.queue.claim_next("consumer-1", 1100).has_value());

    mt::TransactionProvider txs{ctx.database};
    auto reaped = txs.run([&](mt::Transaction& tx) { return ctx.queue.reap_expired(tx, 1200); });

    CHECK(reaped == 1);
    auto stored = ctx.queue.get("msg-1");
    REQUIRE(stored.has_value());
    CHECK(stored->status == qu::MessageStatus::Pending);
}

TEST_CASE("namespaces and channels scope message ids")
{
    TestContext ctx;

    ctx.queue.enqueue(
        "team-a", "notifications", "msg-1", mt::Json::object({{"tenant", "a"}}), 1000
    );
    ctx.queue.enqueue(
        "team-b", "notifications", "msg-1", mt::Json::object({{"tenant", "b"}}), 1000
    );
    ctx.queue.enqueue("team-a", "billing", "msg-1", mt::Json::object({{"tenant", "a"}}), 1000);

    CHECK_THROWS_AS(
        ctx.queue.enqueue("team-a", "notifications", "msg-1", mt::Json::object({}), 1001),
        qu::DuplicateMessage
    );

    auto team_a_notification = ctx.queue.claim_next("team-a", "notifications", "consumer-1", 1100);
    REQUIRE(team_a_notification.has_value());
    CHECK(team_a_notification->namespace_name == "team-a");
    CHECK(team_a_notification->channel_name == "notifications");

    auto team_b_notification = ctx.queue.claim_next("team-b", "notifications", "consumer-2", 1100);
    REQUIRE(team_b_notification.has_value());
    CHECK(team_b_notification->namespace_name == "team-b");
    CHECK(team_b_notification->channel_name == "notifications");

    auto team_a_billing = ctx.queue.get("team-a", "billing", "msg-1");
    REQUIRE(team_a_billing.has_value());
    CHECK(team_a_billing->status == qu::MessageStatus::Pending);
}

TEST_CASE("namespaces and channels can be listed")
{
    TestContext ctx;
    ctx.queue.enqueue("team-b", "notifications", "msg-1", mt::Json::object({}), 1000);
    ctx.queue.enqueue("team-a", "billing", "msg-2", mt::Json::object({}), 1000);
    ctx.queue.enqueue("team-a", "notifications", "msg-3", mt::Json::object({}), 1000);
    ctx.queue.enqueue("team-a", "billing", "msg-4", mt::Json::object({}), 1000);

    CHECK(ctx.queue.list_namespaces() == std::vector<std::string>{"team-a", "team-b"});
    CHECK(
        ctx.queue.list_channels("team-a") == std::vector<std::string>{"billing", "notifications"}
    );
    CHECK(ctx.queue.list_channels("missing").empty());
}

TEST_CASE("namespace and channel listing can use caller-owned transactions")
{
    TestContext ctx;
    mt::TransactionProvider txs{ctx.database};

    txs.run(
        [&](mt::Transaction& tx)
        {
            ctx.queue.enqueue(tx, "team-a", "billing", "msg-1", mt::Json::object({}), 1000);
            ctx.queue.enqueue(tx, "team-b", "notifications", "msg-2", mt::Json::object({}), 1000);

            CHECK(ctx.queue.list_namespaces(tx) == std::vector<std::string>{"team-a", "team-b"});
            CHECK(ctx.queue.list_channels(tx, "team-a") == std::vector<std::string>{"billing"});
        }
    );
}
