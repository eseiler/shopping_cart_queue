// SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h> // for Test, TestInfo, Message, TEST, EXPECT_THROW, TestPartResult

#include <string> // for basic_string

#include <scq/slotted_cart_queue.hpp> // for slotted_cart_queue, logic_error, cart_capacity, cart_count, slot_count

TEST(slotted_cart_queue_test, default_construct)
{
    scq::slotted_cart_queue<int> queue{};
}

TEST(slotted_cart_queue_test, valid_construct)
{
    scq::slotted_cart_queue<int> queue{{.slots = 5, .carts = 5, .capacity = 1}};
}

TEST(slotted_cart_queue_test, invalid_construct)
{
    // capacity of a cart is too small (a cart should be able to store at least one item)
    EXPECT_THROW((scq::slotted_cart_queue<int>{{.slots = 5, .carts = 5, .capacity = 0}}), std::logic_error);

    // less carts than slots (would dead-lock)
    EXPECT_THROW((scq::slotted_cart_queue<int>{{.slots = 5, .carts = 1, .capacity = 1}}), std::logic_error);
}
