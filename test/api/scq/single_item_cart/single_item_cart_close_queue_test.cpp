
#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <span>
#include <thread>

#include <scq/slotted_cart_queue.hpp>

#include "../concurrent_cross_off_list.hpp"

static constexpr std::chrono::milliseconds wait_time(10);

TEST(single_item_cart_close_queue, no_producer_no_consumer)
{
    using value_type = int;

    // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
    scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

    queue.close();
}

TEST(single_item_cart_close_queue, single_producer_no_consumer)
{
    using value_type = int;

    // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
    scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

    queue.enqueue(scq::slot_id{1}, value_type{100});
    queue.enqueue(scq::slot_id{1}, value_type{101});
    queue.enqueue(scq::slot_id{1}, value_type{102});
    queue.enqueue(scq::slot_id{1}, value_type{103});

    queue.close();

    EXPECT_THROW(queue.enqueue(scq::slot_id{2}, value_type{200}), std::overflow_error);
}

TEST(single_item_cart_close_queue, multiple_producer_no_consumer)
{
    using value_type = int;

    // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
    scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

    // initialise 5 producing threads
    std::vector<std::thread> enqueue_threads(5);
    std::generate(enqueue_threads.begin(), enqueue_threads.end(), [&]()
    {
        static size_t thread_id = 0;
        return std::thread([thread_id = thread_id++, &queue]
        {
            switch (thread_id)
            {
                case 0:
                    queue.enqueue(scq::slot_id{1}, value_type{100});
                    break;
                case 1:
                    queue.enqueue(scq::slot_id{1}, value_type{101});
                    break;
                case 2:
                    queue.enqueue(scq::slot_id{2}, value_type{200});
                    break;
                case 3:
                    queue.enqueue(scq::slot_id{1}, value_type{103});
                    break;
                case 4:
                    queue.enqueue(scq::slot_id{1}, value_type{102});
                    break;
            }

            std::this_thread::sleep_for(2 * wait_time);

            // queue should already be closed
            EXPECT_THROW(queue.enqueue(scq::slot_id{0}, value_type{0}), std::overflow_error);
        });
    });

    std::this_thread::sleep_for(wait_time);
    queue.close();

    for (auto && enqueue_thread: enqueue_threads)
        enqueue_thread.join();
}

TEST(single_item_cart_close_queue, no_producer_single_consumer)
{
    using value_type = int;

    // close first then dequeue.
    {
        // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
        scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

        queue.close();

        // should be non-blocking if queue was closed
        scq::cart<value_type> cart = queue.dequeue();

        EXPECT_FALSE(cart.valid());

        EXPECT_THROW(cart.get(), std::future_error);
    }

    // dequeue first then close.
    {
        // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
        scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

        std::thread dequeue_thread{[&queue]
        {
            // should be blocking if queue was not yet closed
            scq::cart<value_type> cart = queue.dequeue();

            EXPECT_FALSE(cart.valid());

            EXPECT_THROW(cart.get(), std::future_error);
        }};

        // close after all threads block
        std::this_thread::sleep_for(wait_time);
        queue.close();

        dequeue_thread.join();
    }
}

TEST(single_item_cart_close_queue, no_producer_multiple_consumer)
{
    using value_type = int;

    // close first then dequeue.
    {
        // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
        scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

        queue.close();

        for (int i = 0; i < 5; ++i)
        {
            // should be non-blocking if queue was closed
            scq::cart<value_type> cart = queue.dequeue();

            EXPECT_FALSE(cart.valid());

            EXPECT_THROW(cart.get(), std::future_error);
        }
    }

    // dequeue first then close.
    {
        // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
        scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

        // initialise 5 consuming threads
        std::vector<std::thread> dequeue_threads(5);
        std::generate(dequeue_threads.begin(), dequeue_threads.end(), [&]()
        {
            return std::thread([&queue]
            {
                // should be blocking if queue was not yet closed
                scq::cart<value_type> cart = queue.dequeue();

                EXPECT_FALSE(cart.valid());

                EXPECT_THROW(cart.get(), std::future_error);
            });
        });

        // close after all threads block
        std::this_thread::sleep_for(wait_time);
        queue.close();

        for (auto && dequeue_thread: dequeue_threads)
            dequeue_thread.join();
    }
}

TEST(single_item_cart_close_queue, multiple_producer_multiple_consumer)
{
    using value_type = int;

    // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
    scq::slotted_cart_queue<value_type> queue{scq::slot_count{5}, scq::cart_count{5}, scq::cart_capacity{1}};

    // expected set contains all (expected) results; after the test which set should be empty (each matching result will
    // be crossed out)
    concurrent_cross_off_list<std::pair<std::size_t, value_type>> expected
    {
        {1, value_type{100}},
        {1, value_type{101}},
        {1, value_type{102}},
        {1, value_type{103}},
        {2, value_type{200}}
    };

    // initialise 5 producing threads
    std::vector<std::thread> enqueue_threads(5);
    std::generate(enqueue_threads.begin(), enqueue_threads.end(), [&]()
    {
        static size_t thread_id = 0;
        return std::thread([thread_id = thread_id++, &queue]
        {
            switch (thread_id)
            {
                case 0:
                    queue.enqueue(scq::slot_id{1}, value_type{100});
                    break;
                case 1:
                    queue.enqueue(scq::slot_id{1}, value_type{101});
                    break;
                case 2:
                    queue.enqueue(scq::slot_id{2}, value_type{200});
                    break;
                case 3:
                    queue.enqueue(scq::slot_id{1}, value_type{103});
                    break;
                case 4:
                    queue.enqueue(scq::slot_id{1}, value_type{102});
                    break;
            }

            std::this_thread::sleep_for(thread_id * wait_time);
        });
    });

    std::thread late_enqueue_thread([&queue]()
    {
        std::this_thread::sleep_for(5 * wait_time);

        // queue should already be closed
        EXPECT_THROW(queue.enqueue(scq::slot_id{0}, value_type{0}), std::overflow_error);
    });

    // initialise 5 consuming threads
    std::vector<std::thread> dequeue_threads(5);
    std::generate(dequeue_threads.begin(), dequeue_threads.end(), [&]()
    {
        return std::thread([&queue, &expected]
        {
            while (true)
            {
                // should be blocking if queue was not yet closed
                scq::cart<value_type> cart = queue.dequeue();

                // abort if queue was closed
                if (!cart.valid())
                {
                    EXPECT_FALSE(cart.valid());
                    EXPECT_THROW(cart.get(), std::future_error);
                    break;
                }

                EXPECT_TRUE(cart.valid());

                std::pair<scq::slot_id, std::span<value_type>> cart_data = cart.get();

                EXPECT_TRUE(expected.cross_off({
                    std::get<0>(cart_data).slot_id,
                    std::get<1>(cart_data)[0]
                }));
            }
        });
    });

    for (auto && enqueue_thread: enqueue_threads)
        enqueue_thread.join();

    // close after all producer threads are finished
    queue.close();

    late_enqueue_thread.join();

    for (auto && dequeue_thread: dequeue_threads)
        dequeue_thread.join();

    // all results seen
    EXPECT_TRUE(expected.empty());
}