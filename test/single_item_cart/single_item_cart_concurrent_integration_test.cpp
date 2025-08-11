// SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h> // for AssertionResult, Test, Message, TestPartResult, EXPECT_TRUE, Tes...

#include <algorithm>        // for generate
#include <cstddef>          // for size_t
#include <initializer_list> // for initializer_list
#include <string>           // for basic_string
#include <thread>           // for thread
#include <utility>          // for pair, get
#include <vector>           // for vector

#include <scq/slotted_cart_queue.hpp> // for slotted_cart_queue, cart_future, slot_id, future_error, span

#include "../concurrent_cross_off_list.hpp" // for concurrent_cross_off_list

static constexpr size_t max_iterations = 50000;

TEST(single_item_cart_concurrent_integration, multiple_producer_multiple_consumer)
{
    using value_type = int;

    // this slotted_cart_queue should behave like a normal queue, but with nondeterministic results
    scq::slotted_cart_queue<value_type> queue{{.slots = 5, .carts = 5, .capacity = 1}};

    // expected set contains all (expected) results; after the test which set should be empty (each matching result will
    // be crossed out)
    concurrent_cross_off_list<std::pair<size_t, value_type>> expected{};
    for (size_t thread_id = 0; thread_id < 5; ++thread_id)
        for (size_t i = 0; i < max_iterations; ++i)
            expected.insert(std::pair<size_t, value_type>{thread_id, i});

    // initialise 5 producing threads
    std::vector<std::thread> enqueue_threads(5);
    std::generate(enqueue_threads.begin(),
                  enqueue_threads.end(),
                  [&]()
                  {
                      static size_t thread_id = 0;
                      return std::thread(
                          [thread_id = thread_id++, &queue]
                          {
                              for (size_t i = 0; i < max_iterations; ++i)
                              {
                                  queue.enqueue(scq::slot_id{thread_id}, static_cast<value_type>(i));
                              }
                          });
                  });

    // initialise 5 consuming threads
    std::vector<std::thread> dequeue_threads(5);
    std::generate(dequeue_threads.begin(),
                  dequeue_threads.end(),
                  [&]()
                  {
                      return std::thread(
                          [&queue, &expected]
                          {
                              std::vector<std::pair<size_t, value_type>> results{};
                              results.reserve(max_iterations);

                              while (true)
                              {
                                  scq::cart_future<value_type> cart = queue.dequeue(); // might block

                                  if (!cart.valid())
                                  {
                                      EXPECT_FALSE(cart.valid());
                                      EXPECT_THROW(cart.get(), std::future_error);
                                      break;
                                  }

                                  EXPECT_TRUE(cart.valid());

                                  std::pair<scq::slot_id, std::span<value_type>> cart_data = cart.get();

                                  results.emplace_back(std::get<0>(cart_data).value, std::get<1>(cart_data)[0]);
                              }

                              // cross off results after enqueue / dequeue is done
                              for (auto && result : results)
                              {
                                  EXPECT_TRUE(expected.cross_off(result));
                              }
                          });
                  });

    for (auto && enqueue_thread : enqueue_threads)
        enqueue_thread.join();

    queue.close();

    for (auto && dequeue_thread : dequeue_threads)
        dequeue_thread.join();

    // all results seen
    EXPECT_TRUE(expected.empty());
}
