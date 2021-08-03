
#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <span>
#include <thread>

#include <scq/slotted_cart_queue.hpp>

#include "../concurrent_cross_off_list.hpp"

static constexpr std::chrono::milliseconds wait_time(10);

TEST(multiple_item_cart_concurrent, single_producer_single_consumer)
{
    using value_type = int;

    scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{2}};

    // expected set contains all (expected) results; after the test which set should be empty (each matching result will
    // be crossed out)
    concurrent_cross_off_list<std::pair<std::size_t, value_type>> expected
    {
        {1, value_type{100}},
        {1, value_type{101}},
        {1, value_type{102}},
        {1, value_type{103}},
        {2, value_type{200}},
        {2, value_type{201}}
    };

    std::thread enqueue_thread{[&queue]()
    {
        // all enqueues are guaranteed to be non-blocking
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{1}, value_type{100});
        queue.enqueue(scq::slot_id{1}, value_type{101});
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{2}, value_type{200});
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{1}, value_type{103});
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{1}, value_type{102});
        queue.enqueue(scq::slot_id{2}, value_type{201});
    }};

    for (int i = 0; i < 6 / 2; ++i)
    {
        scq::cart<value_type> cart = queue.dequeue(); // might block
        EXPECT_TRUE(cart.valid());
        std::pair<scq::slot_id, std::span<value_type>> cart_data = cart.get();

        EXPECT_EQ(cart_data.second.size(), 2u);

        for (auto && value: cart_data.second)
        {
            EXPECT_TRUE(expected.cross_off({cart_data.first.slot_id, value}));
        }
    }

    enqueue_thread.join();

    // all results seen
    EXPECT_TRUE(expected.empty());
}

TEST(multiple_item_cart_concurrent, single_producer_multiple_consumer)
{
    using value_type = int;

    // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
    scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{2}};

    // expected set contains all (expected) results; after the test which set should be empty (each matching result will
    // be crossed out)
    concurrent_cross_off_list<std::pair<std::size_t, value_type>> expected
    {
        {1, value_type{100}},
        {1, value_type{101}},
        {1, value_type{102}},
        {1, value_type{103}},
        {2, value_type{200}},
        {2, value_type{201}}
    };

    std::thread enqueue_thread{[&queue]()
    {
        // all enqueues are guaranteed to be non-blocking
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{1}, value_type{100});
        queue.enqueue(scq::slot_id{1}, value_type{101});
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{2}, value_type{200});
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{1}, value_type{103});
        std::this_thread::sleep_for(wait_time);
        queue.enqueue(scq::slot_id{1}, value_type{102});
        queue.enqueue(scq::slot_id{2}, value_type{201});
    }};

    // initialise 3 consuming threads
    std::vector<std::thread> dequeue_threads(6 / 2);
    std::generate(dequeue_threads.begin(), dequeue_threads.end(), [&]()
    {
        return std::thread([&queue, &expected]
        {
            scq::cart<value_type> cart = queue.dequeue(); // might block
            EXPECT_TRUE(cart.valid());
            std::pair<scq::slot_id, std::span<value_type>> cart_data = cart.get();

            EXPECT_EQ(cart_data.second.size(), 2u);

            for (auto && value: cart_data.second)
            {
                EXPECT_TRUE(expected.cross_off({cart_data.first.slot_id, value}));
            }
        });
    });

    enqueue_thread.join();

    for (auto && dequeue_thread: dequeue_threads)
        dequeue_thread.join();

    // all results seen
    EXPECT_TRUE(expected.empty());
}

