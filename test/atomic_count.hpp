// SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <condition_variable>
#include <mutex>

struct atomic_count
{
    std::size_t load()
    {
        std::size_t tmp;

        {
            std::unique_lock<std::mutex> count_lock(_count_mutex);
            tmp = _count;
        }

        return tmp;
    }

    std::size_t operator++()
    {
        std::size_t tmp;
        {
            std::unique_lock<std::mutex> count_lock(_count_mutex);
            tmp = ++_count;
        }

        _count_cv.notify_all();
        return tmp;
    }

    void wait_at_least(std::size_t value)
    {
        // this is an inefficient barrier implementation as each ++atomic_count will trigger a notification
        {
            std::unique_lock<std::mutex> count_lock(_count_mutex);

            _count_cv.wait(count_lock,
                           [this, value]
                           {
                               return _count >= value;
                           });
        }
    }

    std::mutex _count_mutex{};
    std::condition_variable _count_cv{};
    std::size_t _count;
};
