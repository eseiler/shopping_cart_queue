// SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h> // for AssertionResult, Message, Test, TestPartResult, CmpHelperEQ, Cmp...

#include <algorithm>        // for generate
#include <cstddef>          // for size_t
#include <initializer_list> // for initializer_list
#include <string>           // for basic_string
#include <thread>           // for thread
#include <utility>          // for pair
#include <vector>           // for vector

#include <scq/slotted_cart_queue.hpp> // for slotted_cart_queue, slot_id, span, atomic_size_t, cart_capacity

#include "../concurrent_cross_off_list.hpp" // for concurrent_cross_off_list

static constexpr size_t max_iterations = 55555;

TEST(multiple_item_cart_concurrent_integration, multiple_producer_multiple_consumer)
{
    using value_type = int;

    static constexpr size_t slot_count{5};
    static constexpr size_t cart_capacity{8};

    scq::slotted_cart_queue<value_type> queue{{.slots = slot_count, .carts = 10, .capacity = cart_capacity}};

    static constexpr size_t expected_full_cart_count = (max_iterations / cart_capacity) * slot_count;
    static constexpr size_t expected_non_full_cart_count = slot_count;
    static constexpr size_t expected_non_full_cart_capacity = max_iterations % cart_capacity;

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

    std::atomic_size_t full_cart_count{};
    std::atomic_size_t non_full_cart_count{};

    // initialise 5 consuming threads
    std::vector<std::thread> dequeue_threads(5);
    std::generate(dequeue_threads.begin(),
                  dequeue_threads.end(),
                  [&]()
                  {
                      return std::thread(
                          [&queue, &expected, &full_cart_count, &non_full_cart_count]
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

                                  size_t elements_count = cart_data.second.size();
                                  if (elements_count == cart_capacity)
                                  {
                                      ++full_cart_count;
                                      EXPECT_EQ(elements_count, cart_capacity);
                                  }
                                  else
                                  {
                                      ++non_full_cart_count;
                                      EXPECT_EQ(elements_count, expected_non_full_cart_capacity);
                                  }

                                  for (auto && value : cart_data.second)
                                  {
                                      results.emplace_back(cart_data.first.value, value);
                                  }
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

    EXPECT_EQ(full_cart_count.load(), expected_full_cart_count);
    EXPECT_EQ(non_full_cart_count.load(), expected_non_full_cart_count);

    // all results seen
    EXPECT_TRUE(expected.empty());
}
