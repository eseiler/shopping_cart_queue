// SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <mutex>
#include <set>

template <typename item_t>
struct concurrent_cross_off_list
{
    concurrent_cross_off_list(std::initializer_list<item_t> list) : _set{list}
    {}

    // returns true if item was crossed out
    bool cross_off(item_t item)
    {
        std::unique_lock lk{_set_mutex};

        auto it = _set.find(item);
        bool crossed_off = it != _set.end();

        if (crossed_off)
            _set.erase(it);

        return crossed_off;
    }

    void insert(item_t item)
    {
        _set.insert(std::move(item));
    }

    bool empty()
    {
        return _set.empty();
    }

private:
    std::set<item_t> _set;

    std::mutex _set_mutex{};
};
