# SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
# SPDX-License-Identifier: CC0-1.0

cmake_minimum_required (VERSION 3.25)

# This includes `cmake/test/config.cmake` which takes care of setting up the test infrastructure. It also provides
# the `add_app_test` macro, which is used to add tests to the CMake test suite.
include (test/config)

add_subdirectory (single_item_cart)
add_subdirectory (multiple_item_cart)

add_app_test (slotted_cart_queue_test.cpp)

message (STATUS "You can run `make check` to build and run tests.")
