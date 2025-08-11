// SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// IWYU pragma: begin_exports
#include <atomic>             // for atomic, atomic_bool
#include <cassert>            // for assert
#include <condition_variable> // for condition_variable
#include <future>             // for future_errc, future_error
#include <mutex>              // for mutex, unique_lock, scoped_lock
#include <optional>           // for optional
#include <span>               // for span
#include <stdexcept>          // for runtime_error, logic_error, overflow_error
// IWYU pragma: end_exports

#include <cstddef> // for size_t, ptrdiff_t
#include <string>  // for char_traits, operator+, basic_string, to_string, string
#include <utility> // for pair
#include <vector>  // for allocator, move, vector

namespace scq
{

struct params
{
    size_t slots;
    size_t carts;
    size_t capacity;
};

struct slots
{
    size_t value;
};

struct carts
{
    size_t value;
};

struct capacity
{
    size_t value;
};

struct slot_id
{
    size_t value;
};

template <typename value_t>
class slotted_cart_queue;

template <typename value_t>
class cart_future
{
public:
    cart_future() = default;
    cart_future(cart_future const &) = delete;
    cart_future(cart_future &&) = default;
    cart_future & operator=(cart_future const &) = delete;
    cart_future & operator=(cart_future &&) = default;
    ~cart_future()
    {
        if (valid())
            cart_queue->notify_processed_cart(*this);
    }

    using value_type = value_t;

    bool valid() const
    {
        return cart_queue != nullptr;
    }

    std::pair<scq::slot_id, std::span<value_type>> get()
    {
        if (!valid()) // slotted_cart_queue is already closed and no further elements.
            throw std::future_error{std::future_errc::no_state};

        return {id, memory_region};
    }

private:
    template <typename>
    friend class slotted_cart_queue;

    scq::slot_id id{};
    std::span<value_type> memory_region{};

    slotted_cart_queue<value_type> * cart_queue{nullptr};
};

template <typename value_t>
class slotted_cart_queue
{
    struct cart_memory_id
    {
        size_t value;
    };
    struct queue_memory_t;
    struct cart_slots_t;
    struct empty_carts_queue_t;
    struct full_carts_queue_t;

public:
    using value_type = value_t;
    using cart_future_type = cart_future<value_type>;

    slotted_cart_queue() = default;
    slotted_cart_queue(slotted_cart_queue const &) = delete;
    slotted_cart_queue(slotted_cart_queue &&) = delete; // TODO:
    slotted_cart_queue & operator=(slotted_cart_queue const &) = delete;
    slotted_cart_queue & operator=(slotted_cart_queue &&) = delete; // TODO:

    slotted_cart_queue(params params) :
        slot_count{params.slots},
        cart_count{params.carts},
        cart_capacity{params.capacity}
    {
        if (cart_count < slot_count)
            throw std::logic_error{"The number of carts must be >= the number of slots."};

        if (cart_capacity == 0u)
            throw std::logic_error{"The cart capacity must be >= 1."};
    }

    void enqueue(slot_id slot, value_type value)
    {
        using full_cart_type = typename full_carts_queue_t::full_cart_type;

        bool full_queue_was_empty{};
        bool queue_was_closed{};

        std::optional<full_cart_type> full_cart{};

        {
            std::unique_lock<std::mutex> cart_management_lock(cart_management_mutex);

            queue_was_closed = queue_closed;

            auto slot_cart = cart_slots.slot(slot);

            if (!queue_was_closed && slot_cart.empty())
            {
                empty_cart_queue_empty_or_closed_cv.wait(cart_management_lock,
                                                         [this, &slot_cart]
                                                         {
                                                             // wait until either an empty cart is ready, or the slot has a cart, or the queue was closed
                                                             return !empty_carts_queue.empty() || !slot_cart.empty()
                                                                 || queue_closed == true;
                                                         });

                queue_was_closed = queue_closed;

                // if the current slot still has no cart and we have an available empty cart, use that empty cart in
                // this slot
                if (!queue_was_closed && slot_cart.empty())
                {
                    // this assert must be true because of the condition within empty_cart_queue_empty_or_closed_cv
                    assert(!empty_carts_queue.empty());

                    std::span<value_t> memory_region = empty_carts_queue.dequeue();
                    slot_cart.set_memory_region(memory_region);
                    assert_cart_count_variant();
                }
            }

            if (!queue_was_closed)
            {
                slot_cart.emplace_back(std::move(value));

                if (slot_cart.full())
                {
                    full_cart = full_carts_queue_t::move_slot_cart_to_full_cart(slot_cart);
                }
            }
        }

        if (full_cart.has_value())
        {
            std::unique_lock<std::mutex> full_cart_queue_lock(full_cart_queue_mutex);

            full_queue_was_empty = full_carts_queue.empty();

            // enqueue later
            full_carts_queue.enqueue(std::move(*full_cart));
            assert_cart_count_variant();
        }

        if (full_queue_was_empty)
            full_cart_queue_empty_or_closed_cv.notify_all();

        if (queue_was_closed)
            throw std::overflow_error{"slotted_cart_queue is already closed."};
    }

    cart_future_type dequeue()
    {
        cart_future_type cart_future{};

        {
            std::unique_lock<std::mutex> full_cart_queue_lock(full_cart_queue_mutex);

            full_cart_queue_empty_or_closed_cv.wait(full_cart_queue_lock,
                                                    [this]
                                                    {
                                                        // wait until first cart is full
                                                        return !full_carts_queue.empty() || queue_closed == true;
                                                    });

            if (!full_carts_queue.empty())
            {
                auto full_cart = full_carts_queue.dequeue();
                cart_future.id = full_cart.first;
                cart_future.memory_region = std::move(full_cart.second);
                cart_future.cart_queue = this;
                assert_cart_count_variant();
            }
        }

        // NOTE: cart memory will be released by notify_processed_cart after cart_future was destroyed
        return cart_future;
    }

    void close()
    {
        {
            // this locks the whole queue
            std::scoped_lock lock(cart_management_mutex, full_cart_queue_mutex);

            queue_closed = true;
            cart_slots.move_active_carts_into_full_carts_queue(full_carts_queue);
            assert_cart_count_variant();
        }

        empty_cart_queue_empty_or_closed_cv.notify_all();
        full_cart_queue_empty_or_closed_cv.notify_all();
    }

private:
    size_t slot_count{};
    size_t cart_count{};
    size_t cart_capacity{};

    queue_memory_t queue_memory{scq::carts{cart_count}, scq::capacity{cart_capacity}};
    empty_carts_queue_t empty_carts_queue{scq::carts{cart_count}, queue_memory};
    full_carts_queue_t full_carts_queue{scq::carts{cart_count}};

    friend cart_future_type;

    void assert_cart_count_variant()
    {
        empty_carts_queue.check_invariant();
        full_carts_queue.check_invariant();
    }

    void notify_processed_cart(cart_future_type & cart_future)
    {
        bool empty_queue_was_empty{};
        {
            std::unique_lock<std::mutex> cart_management_lock(cart_management_mutex);

            empty_queue_was_empty = empty_carts_queue.empty();

            empty_carts_queue.enqueue(cart_future.memory_region);
            assert_cart_count_variant();
        }

        if (empty_queue_was_empty)
            empty_cart_queue_empty_or_closed_cv.notify_all();
    }

    std::atomic_bool queue_closed{false};

    cart_slots_t cart_slots{scq::slots{slot_count}, scq::capacity{cart_capacity}};

    std::mutex cart_management_mutex;
    std::condition_variable empty_cart_queue_empty_or_closed_cv;
    std::mutex full_cart_queue_mutex;
    std::condition_variable full_cart_queue_empty_or_closed_cv;
};

template <typename value_t>
struct slotted_cart_queue<value_t>::queue_memory_t
{
    queue_memory_t() = default;
    queue_memory_t(scq::carts carts, scq::capacity capacity) :
        cart_capacity{capacity.value},
        internal_queue_memory(carts.value * capacity.value)
    {}

    std::span<value_t> memory_region(cart_memory_id cart_memory_id)
    {
        size_t size = cart_capacity;
        value_t * begin = internal_queue_memory.data() + cart_memory_id.value * size;
        return {begin, size};
    }

    size_t cart_capacity{};

    std::vector<value_t> internal_queue_memory{};
};

template <typename value_t>
struct slotted_cart_queue<value_t>::cart_slots_t
{
    cart_slots_t() = default;
    cart_slots_t(scq::slots slots, scq::capacity capacity) :
        cart_capacity{capacity.value},
        internal_cart_slots(slots.value) // default init slots many vectors
    {}

    struct slot_cart_t
    {
        size_t _slot_id;
        size_t cart_capacity;
        std::span<value_t> * memory_region_ptr;

        scq::slot_id slot_id() const
        {
            return {_slot_id};
        }

        size_t size() const
        {
            return memory_region().size();
        }

        size_t capacity() const
        {
            return cart_capacity;
        }

        bool empty() const
        {
            return memory_region().empty();
        }

        bool full() const
        {
            return size() >= capacity();
        }

        void emplace_back(value_t value)
        {
            assert(size() < capacity());
            std::span<value_t> _memory_region = memory_region();
            _memory_region = std::span<value_t>(_memory_region.data(), _memory_region.size() + 1u);
            _memory_region.back() = std::move(value);
            set_memory_region(_memory_region);
        }

        void set_memory_region(std::span<value_t> memory_region_span)
        {
            assert(memory_region_ptr != nullptr);
            *memory_region_ptr = std::span<value_t>{memory_region_span};
        }

        std::span<value_t> memory_region() const
        {
            assert(memory_region_ptr != nullptr);
            return *memory_region_ptr;
        }
    };

    slot_cart_t slot(scq::slot_id slot_id)
    {
        std::span<value_t> & memory_region = internal_cart_slots[slot_id.value];
        return {slot_id.value, cart_capacity, &memory_region};
    }

    void move_active_carts_into_full_carts_queue(full_carts_queue_t & full_carts_queue)
    {
        // TODO: if pending slots are more than queue capacity? is that a problem?

        // put all non-empty / non-full carts into full queue (no element can't be added any more and all pending
        // elements = active to fill elements must be processed)
        for (size_t slot_id = 0u; slot_id < internal_cart_slots.size(); ++slot_id)
        {
            auto slot_cart = slot(scq::slot_id{slot_id});
            if (!slot_cart.empty())
            {
                auto full_cart = full_carts_queue_t::move_slot_cart_to_full_cart(slot_cart);
                full_carts_queue.enqueue(full_cart);
                full_carts_queue.check_invariant();
            }
        }
    }

    size_t cart_capacity{};

    std::vector<std::span<value_t>> internal_cart_slots{}; // position is slot_id
};

template <typename value_t>
struct slotted_cart_queue<value_t>::empty_carts_queue_t
{
    empty_carts_queue_t() = default;
    empty_carts_queue_t(carts carts, queue_memory_t & queue_memory) :
        count{static_cast<std::ptrdiff_t>(carts.value)},
        cart_count{carts.value},
        internal_queue{}
    {
        internal_queue.reserve(count);

        for (size_t i = 0; i < cart_count; ++i)
            internal_queue.push_back(queue_memory.memory_region(cart_memory_id{i}));
    }

    bool empty()
    {
        return count == 0;
    }

    void enqueue(std::span<value_t> memory_region)
    {
        internal_queue.push_back(std::span<value_t>{memory_region.data(), 0});

        ++count;
    }

    std::span<value_t> dequeue()
    {
        --count;

        std::span<value_t> memory_region{internal_queue.back().data(), 0};
        internal_queue.pop_back();
        return memory_region;
    }

    void check_invariant()
    {
        assert(0 <= count);
        assert(count <= static_cast<std::ptrdiff_t>(cart_count));

        if (!(0 <= count))
            throw std::runtime_error{"empty_carts_queue.count: negative"};

        if (!(count <= static_cast<std::ptrdiff_t>(cart_count)))
            throw std::runtime_error{std::string{"empty_carts_queue.count: FULL, count: "} + std::to_string(count)
                                     + " <= " + std::to_string(cart_count)};
    }

    std::atomic<std::ptrdiff_t> count{};
    size_t cart_count{};

    std::vector<std::span<value_t>> internal_queue{};
};

template <typename value_t>
struct slotted_cart_queue<value_t>::full_carts_queue_t
{
    using full_cart_type = std::pair<slot_id, std::span<value_t>>;
    using slot_cart_type = typename cart_slots_t::slot_cart_t;

    full_carts_queue_t() = default;
    full_carts_queue_t(carts carts) : count{0}, cart_count{carts.value}
    {
        internal_queue.reserve(cart_count);
    }

    bool empty()
    {
        return count == 0;
    }

    static full_cart_type move_slot_cart_to_full_cart(slot_cart_type & slot_cart)
    {
        assert(slot_cart.size() > 0);                     // at least one element
        assert(slot_cart.size() <= slot_cart.capacity()); // at most cart capacity many elements

        full_cart_type full_cart{slot_cart.slot_id(), slot_cart.memory_region()};
        slot_cart.set_memory_region(std::span<value_t>{}); // reset slot
        return full_cart;
    }

    void enqueue(full_cart_type full_cart)
    {
        ++count;

        internal_queue.push_back(full_cart);
    }

    full_cart_type dequeue()
    {
        --count;

        full_cart_type tmp = std::move(internal_queue.back());
        internal_queue.pop_back();
        return tmp;
    }

    void check_invariant()
    {
        assert(0 <= count);
        assert(count <= static_cast<std::ptrdiff_t>(cart_count));

        if (!(0 <= count))
            throw std::runtime_error{"full_carts_queue.count: negative"};

        if (!(count <= static_cast<std::ptrdiff_t>(cart_count)))
            throw std::runtime_error{std::string{"full_carts_queue.count: FULL, count: "} + std::to_string(count)
                                     + " <= " + std::to_string(cart_count)};
    }

    std::atomic<std::ptrdiff_t> count{};
    size_t cart_count{};

    std::vector<full_cart_type> internal_queue{};
};

} // namespace scq
