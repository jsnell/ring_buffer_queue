// -*- mode: c++; c-basic-offset: 4 indent-tabs-mode: nil -*- */
//
// Copyright 2016 Juho Snellman, released under a MIT license (see
// LICENSE).

#ifndef INLINE_DEQUE_H
#define INLINE_DEQUE_H

#include <stdexcept>
#include <cstddef>
#include <limits>

template<typename T,
         size_t InlineCapacity = 1,
         size_t InitialCapacity = InlineCapacity,
         typename CapacityType = uint32_t,
         class Allocator = std::allocator<T>>
class inline_deque {
public:
    explicit inline_deque(size_t initial_capacity = InitialCapacity,
                          const Allocator& alloc = Allocator())
        : ptr_(alloc) {
        if (initial_capacity > InlineCapacity) {
            capacity_ = 1;
            while (capacity_ < initial_capacity) {
                capacity_ *= 2;
            }
            e_.e_ = ptr_.allocate(capacity_);
        } else {
            capacity_ = InlineCapacity;
        }
    }

    explicit inline_deque(std::initializer_list<T> init,
                          const Allocator& alloc = Allocator())
        : inline_deque(init.size(), alloc) {
        for (auto val : init) {
            emplace_back(val);
        }
    }

    ~inline_deque() {
        reset();
    }

    // Adding new elements at front / back of queue.

    void push_front(const T& e) {
        if (full()) {
            overflow();
        }
        ptr_.read_--;
        ptr_.construct(&slot(ptr_read()), e);
    }

    void push_back(const T& e) {
        if (full()) {
            overflow();
        }
        ptr_.construct(&slot(ptr_write()), e);
        ptr_.write_++;
    }

    template<typename... Args>
    void emplace_front(Args&&... args) {
        if (full()) {
            overflow();
        }
        ptr_.read_--;
        ptr_.construct(&slot(ptr_read()),
                       std::forward<Args>(args)...);
    }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        if (full()) {
            overflow();
        }
        ptr_.construct(&slot(ptr_write()),
                       std::forward<Args>(args)...);
        ptr_.write_++;
    }

    // Accessing items (front, back, random access, pop).

    const T& front() const {
        require_nonempty();
        return slot(ptr_read());
    }

    const T& back() const {
        require_nonempty();
        return slot(ptr_write(-1));
    }

    T& front() {
        require_nonempty();
        return slot(ptr_read());
    }

    T& back() {
        require_nonempty();
        return slot(ptr_write(-1));
    }

    T& operator[] (size_t i) {
        return slot(ptr_read(i));
    }

    const T& operator[] (size_t i) const {
        return slot(ptr_read(i));
    }

    T& at(size_t i) {
        if (i >= size()) {
            throw std::out_of_range("index too large");
        }
        return slot(ptr_read(i));
    }

    void pop_front() {
        require_nonempty();
        ptr_.destroy(&slot(ptr_read()));
        ptr_.read_++;
        shrink();
    }

    void pop_back() {
        require_nonempty();
        ptr_.write_--;
        ptr_.destroy(&slot(ptr_write()));
        shrink();
    }

    // Size of queue

    bool empty() const {
        return size() == 0;
    }

    CapacityType size() const {
        return ptr_.write_ - ptr_.read_;
    }

    CapacityType max_size() const {
        return std::numeric_limits<CapacityType>::max() >> 1;
    }

    CapacityType capacity() const {
        return capacity_;
    }

    void clear() {
        while (!empty()) {
            pop_front();
        }
    }

    void shrink_to_fit() {
        CapacityType new_capacity = capacity_;
        while (new_capacity &&
               new_capacity > size() * 2) {
            new_capacity /= 2;
        }
        if (new_capacity < capacity_) {
            resize(new_capacity);
        }
    }

    // Copying / assignment

    inline_deque(const inline_deque& other)
        : ptr_(other.ptr_) {
        clone_from(other);
    }

    inline_deque(inline_deque&& other)
        : ptr_(other.ptr_) {
        move_from(other);
    }

    inline_deque& operator=(const inline_deque& other) {
        if (&other == this) {
            return *this;
        }

        reset();
        clone_from(other);
        return *this;
    }

    inline_deque& operator=(inline_deque&& other) {
        if (&other == this) {
            return *this;
        }

        reset();
        move_from(other);

        return *this;
    }

    // TODO: swap? assign?

    // Iterators

    template<typename RB, typename VT>
    struct iterator_base {
        typedef std::random_access_iterator_tag iterator_category;

        iterator_base(RB* q, size_t index)
            : q_(q), i_(index) {
        }

        iterator_base(const iterator_base& other)
            : q_(other.q_),
              i_(other.i_) {
        }

        bool operator==(const iterator_base& other) const {
            return q_ == other.q_ && i_ == other.i_;
        }
        bool operator!=(const iterator_base& other) const {
            return q_ != other.q_ || i_ != other.i_;
        }

        iterator_base operator+(size_t i) const {
            return iterator_base(q_, i_ + i);
        }
        iterator_base& operator+=(size_t i) {
            i_ += i;
            return *this;
        }
        iterator_base& operator++() {
            return *this += 1;
        }
        iterator_base operator++(int) {
            iterator_base ret = *this;
            ++*this;
            return ret;
        }

        iterator_base operator-(size_t i) const {
            return iterator_base(q_, i_ - i);
        }
        iterator_base& operator-=(size_t i) {
            i_ -= i;
            return *this;
        }
        iterator_base& operator--() {
            return *this -= 1;
        }
        iterator_base operator--(int) {
            iterator_base ret = *this;
            --*this;
            return ret;
        }

        bool operator<(const iterator_base& other) const {
            if (q_ == other.q_) {
                return i_ < other.i_;
            }
            return q_ < other.q_;
        }
        bool operator>(const iterator_base& other) const {
            if (q_ == other.q_) {
                return i_ > other.i_;
            }
            return q_ > other.q_;
        }
        bool operator<=(const iterator_base& other) const {
            if (q_ == other.q_) {
                return i_ <= other.i_;
            }
            return q_ <= other.q_;
        }
        bool operator>=(const iterator_base& other) const {
            if (q_ == other.q_) {
                return i_ >= other.i_;
            }
            return q_ >= other.q_;
        }

        VT& operator*() {
            return (*q_)[i_];
        }

        VT* operator->() {
            return &(*q_)[i_];
        }

        operator iterator_base<const inline_deque, const T> const() {
            return iterator_base<const inline_deque, const T>(q_, i_);
        }

    private:
        friend inline_deque;

        RB* q_;
        ptrdiff_t i_;
    };

    typedef iterator_base<inline_deque, T> iterator;
    typedef iterator_base<const inline_deque, const T> const_iterator;

    iterator begin() {
        return iterator(this, 0);
    }

    iterator end() {
        return iterator(this, size());
    }

    const_iterator begin() const {
        return const_iterator(this, 0);
    }

    const_iterator end() const {
        return const_iterator(this, size());
    }

    const_iterator cbegin() const {
        return const_iterator(this, 0);
    }

    const_iterator cend() const {
        return const_iterator(this, size());
    }

    // Modifications at arbitrary locations, using iterators

    iterator erase(const_iterator first, const_iterator last) {
        CapacityType count = last.i_ - first.i_;
        if (count) {
            // First slide all the elements after the deleted ones
            // forward, so that the deleted elements get covered up.
            for (CapacityType i = ptr_.read_ + last.i_;
                 i != ptr_.write_; ++i) {
                slot(i - count) = std::move(slot(i));
            }
            // Then adjust the pointers.
            ptr_.write_ -= count;

            // Finally destroy anything that's beyond the write
            // pointer
            for (CapacityType i = 0; i < count; ++i) {
                ptr_.destroy(&slot(ptr_write(i)));
            }
        }

        return iterator(this, first.i_);
    }

    iterator erase(const_iterator pos) {
        return erase(pos, pos + 1);
    }

    // Insert a single element
    iterator insert(const_iterator pos, const T& val) {
        iterator it = make_space(pos, 1);
        ptr_.construct(&slot(ptr_read(it.i_)), val);
        return it;
    }

    // Construct + insert
    template<typename... Args>
    iterator emplace(const_iterator pos, Args&&... args) {
        iterator it = make_space(pos, 1);
        ptr_.construct(&slot(ptr_read(it.i_)),
                       std::forward<Args>(args)...);
        return it;
    }

    // Move a single element
    iterator insert(const_iterator pos, T&& val) {
        iterator it = make_space(pos, 1);
        ptr_.construct(&slot(ptr_read(it.i_)), val);
        return it;
    }

    // Fill a range
    iterator insert(const_iterator pos, CapacityType n,
                    const T& val) {
        iterator it = make_space(pos, n);
        for (CapacityType i = 0; i < n; ++i) {
            ptr_.construct(&slot(ptr_read(it.i_ + i)), val);
        }
        return it;
    }

    // Misc

    Allocator get_allocator() const {
        return ptr_;
    }

protected:
    bool full() {
        return size() == capacity();
    }

    void overflow() {
        CapacityType new_capacity = capacity_ * 2;
        resize(new_capacity ? new_capacity : 1);
    }

    void shrink() {
        if (ptr_read() == 0 && capacity_ > size() * 2) {
            shrink_to_fit();
        }
    }

    bool use_inline() const {
        return capacity_ == InlineCapacity;
    }

    void resize(CapacityType new_capacity) {
        new_capacity = std::max(new_capacity,
                                static_cast<CapacityType>(InlineCapacity));

        if (new_capacity == capacity_) {
            return;
        }

        T* old_e = (use_inline() ? (T*) &e_.inline_e_ : e_.e_);
        T* new_e;

        if (new_capacity == InlineCapacity) {
            new_e = (T*) &e_.inline_e_;
        } else {
            new_e = ptr_.allocate(new_capacity);
        }

        CapacityType current_size = size();
        for (CapacityType i = 0; i < current_size; ++i) {
            // Note: we have to use slot_impl() with a precomputed array
            // pointer instead of slot() here. The reason is that if the
            // new array is inline-allocated, writes to it will clobber
            // clobber e_.e_.
            ptr_.construct(&new_e[i],
                           std::move(slot_impl(ptr_read(i), old_e)));
            ptr_.destroy(&slot_impl(ptr_read(i), old_e));
        }

        if (!use_inline()) {
            ptr_.deallocate(old_e, capacity_);
        }

        capacity_ = new_capacity;

        if (!use_inline()) {
            e_.e_ = new_e;
        }

        ptr_.read_ = 0;
        ptr_.write_ = current_size;
    }

    void move_from(inline_deque& other) {
        ptr_ = other.ptr_;
        capacity_ = other.capacity_;
        if (use_inline()) {
            for (CapacityType i = 0; i < size(); ++i) {
                ptr_.construct(&slot(ptr_read(i)),
                               std::move(other.slot(ptr_read(i))));
                ptr_.destroy(&other.slot(ptr_read(i)));
            }
        } else {
            e_.e_ = other.e_.e_;
            other.e_.e_ = NULL;
        }
        other.capacity_ = InlineCapacity;
        other.ptr_.read_ = other.ptr_.write_;
    }

    void clone_from(const inline_deque& other) {
        ptr_ = other.ptr_;
        capacity_ = other.capacity_;
        if (!use_inline()) {
            e_.e_ = ptr_.allocate(capacity_);
        }
        for (size_t i = 0; i < size(); ++i) {
            ptr_.construct(&slot(ptr_read(i)),
                           other.slot(ptr_read(i)));
        }
    }

    void reset() {
        clear();
        if (!use_inline()) {
            ptr_.deallocate(e_.e_, capacity_);
        }
    }

    void require_nonempty() const {
        if (empty()) {
            throw std::out_of_range("empty queue");
        }
    }

    iterator make_space(const_iterator pos, CapacityType count) {
        if (!count) {
            return iterator(this, pos.i_);
        }

        // It might be a good idea to special-case making space at
        // start / end here. But right now we don't.

        CapacityType move_count = size() - pos.i_;
        CapacityType last = size() - 1;

        // Make sure we have enough capacity
        CapacityType needed_capacity = size() + count;
        if (needed_capacity > capacity_) {
            CapacityType new_capacity = std::max(static_cast<CapacityType>(1),
                                                 capacity_) * 2;
            while (new_capacity < needed_capacity) {
                new_capacity *= 2;
            }
            resize(new_capacity);
        }

        // Move write pointer forward.
        ptr_.write_ += count;
        // Move all elements after pos forward by "count" elements.
        // This creates a gap in the memory. Leave that memory
        // uninitialized (caller will call construct() on it).
        for (CapacityType i = 0; i < move_count; ++i) {
            CapacityType move_index = last - i;
            ptr_.construct(&slot(ptr_read(move_index + count)),
                           std::move(slot(ptr_read(move_index))));
            ptr_.destroy(&slot(ptr_read(move_index)));
        }

        return iterator(this, pos.i_);
    }

    CapacityType ptr_read(CapacityType offset = 0) const {
        return (ptr_.read_ + offset);
    }

    CapacityType ptr_write(CapacityType offset = 0) const {
        return (ptr_.write_ + offset);
    }

    T& slot(CapacityType index) {
        if (use_inline()) {
            return slot_impl(index, (T*) e_.inline_e_);
        } else {
            return slot_impl(index, e_.e_);
        }
    }

    const T& slot(CapacityType index) const {
        if (use_inline()) {
            return slot_impl(index, (T*) e_.inline_e_);
        } else {
            return slot_impl(index, e_.e_);
        }
    }

    T& slot_impl(CapacityType index, T* array) {
        CapacityType actual_index = index & (capacity_ - 1);
        return array[actual_index];
    }

    const T& slot_impl(CapacityType index, T* array) const {
        CapacityType actual_index = index & (capacity_ - 1);
        return array[actual_index];
    }

    union {
        T* e_;
        uint8_t inline_e_[sizeof(T) * InlineCapacity];
    } e_;
    CapacityType capacity_;

    // A dummy struct just used for empty base class optimization.
    struct ptrs : Allocator {
        ptrs(const Allocator& alloc) : Allocator(alloc) {
        }

        ptrs(const struct ptrs& other)
            : Allocator(other),
              read_(other.read_),
              write_(other.write_) {
        }

        struct ptrs operator=(const struct ptrs& other) {
            Allocator::operator=(other);
            read_ = other.read_;
            write_ = other.write_;
            return *this;
        }

        CapacityType read_ = 0;
        CapacityType write_ = 0;
    } ptr_;
};

#endif // INLINE_DEQUE_H
