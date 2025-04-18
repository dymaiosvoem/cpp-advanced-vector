#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity) 
            : buffer_(Allocate(capacity))
            , capacity_(capacity) {}

    RawMemory(const RawMemory& other) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) {
        buffer_ = std::exchange(other.buffer_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
    }

    RawMemory& operator=(RawMemory&& rhs) {
        if (this != std::addressof(rhs)) {
            Deallocate(buffer_);
            capacity_ = 0;
            Swap(rhs);
        }
        return *this;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    const T* GetAddress() const {
        return buffer_;
    }

    size_t Capacity() const noexcept {
        return capacity_;
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }
private:
    T* buffer_ = nullptr;
    size_t capacity_ = 0;

    static T* Allocate(size_t size) {
        return size == 0 ? nullptr : reinterpret_cast<T*>(operator new (size * sizeof(T)));
    }

    static void Deallocate(T* buf) noexcept {
        operator delete (buf);
    }
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }   

    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

    Vector() = default;

    Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other) : data_(other.Size()), size_(other.Size()) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.Size(), data_.GetAddress());
    } 

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != std::addressof(rhs)) {
            if (data_.Capacity() < rhs.Size()) {
                Vector<T> rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                if (size_ > rhs.Size()) {
                    std::destroy_n(data_ + rhs.Size(), size_ - rhs.Size());
                } else {
                    std::uninitialized_copy_n(rhs.data_ + size_, rhs.Size() - size_, data_.GetAddress() + size_);
                }
                std::copy_n(rhs.data_.GetAddress(), std::min(rhs.Size(), size_), data_.GetAddress());
                size_ = rhs.Size();
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != std::addressof(rhs)) {
            if (data_.Capacity() < rhs.Size() ) {
                Vector<T> rhs_copy(std::move(rhs));
                Swap(rhs_copy);
            } else {
                Swap(rhs);
            }
        }
        return *this;
    }

    template <typename ... Args>
    T& EmplaceBack(Args&& ... args) {
        auto emplace_back_it = Emplace(end(), std::forward<Args>(args)...);
        return *emplace_back_it;
    }

    template <typename ... Args>
    void EmplaceWithoutReallocate(const_iterator pos, Args&& ... args) {
        size_t distance_to_emplace = static_cast<size_t>(std::distance(cbegin(), pos));

        if (pos == end()) {
            new (end()) T(std::forward<Args>(args)...);
        } else {
            T temp_args(std::forward<Args>(args)...);

            new (end()) T(std::forward<T>(data_[size_ - 1]));
            std::move_backward(begin() + distance_to_emplace, end() - 1, end());
            data_[distance_to_emplace] = std::move(temp_args);
        }
    }

    template <typename ... Args>
    void EmplaceWithReallocate(const_iterator pos, Args&& ... args) {
        size_t distance_to_emplace = static_cast<size_t>(std::distance(cbegin(), pos));
        size_t distance_to_end = static_cast<size_t>(std::distance(pos, cend()));

        RawMemory<T> new_vec(size_ == 0 ? 1 : size_ * 2);
        new (new_vec.GetAddress() + distance_to_emplace) T(std::forward<Args>(args)...);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin(), distance_to_emplace, new_vec.GetAddress());
            std::uninitialized_move_n(begin() + distance_to_emplace, distance_to_end, new_vec.GetAddress() + distance_to_emplace + 1);
            std::destroy_n(begin(), size_);
        } else {
            std::uninitialized_copy_n(begin(), distance_to_emplace, new_vec.GetAddress());
            std::uninitialized_copy_n(begin() + distance_to_emplace, distance_to_end, new_vec.GetAddress() + distance_to_emplace + 1);
            std::destroy_n(begin(), size_);
        }
        data_.Swap(new_vec);
    }

    template <typename ... Args>
    iterator Emplace(const_iterator pos, Args&& ... args) {
        size_t distance_to_emplace = static_cast<size_t>(std::distance(cbegin(), pos));
        size_t new_size = size_ + 1;

        if (Capacity() >= new_size) {
            EmplaceWithoutReallocate(pos, std::forward<Args>(args)...);
        } else {
            EmplaceWithReallocate(pos, std::forward<Args>(args)...);
        }

        size_ = new_size;
        return begin() + distance_to_emplace;
    }

    iterator Erase(const_iterator pos) {
        size_t pos_to_erase = static_cast<size_t>(std::distance(cbegin(), pos));
        
        assert(size_ != 0);
        size_t new_size = size_ - 1;

        if (std::is_move_assignable_v<T>) {
            for (size_t i = pos_to_erase; i < new_size; ++i) {
                data_[i] = std::move(data_[i + 1]);
            }
        } else {
            for (size_t i = pos_to_erase; i < new_size; ++i) {
                data_[i] = data_[i + 1];
            }
        }
        
        std::destroy_at(end() - 1);
        size_ = new_size;
        return begin() + pos_to_erase;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    void PopBack() noexcept {
        if (size_ == 0) {
            return;
        }
        std::destroy_at(data_.GetAddress() + size_ - 1);
        size_ -= 1;
    }

    void PushBack(const T& value) {
        size_t new_size = size_ + 1;

        if (Capacity() >= new_size) {
            EmplaceWithoutReallocate(end(), value);
        } else {
            EmplaceWithReallocate(end(), value);
        }

        size_ = new_size;
    }

    void PushBack(T&& value) {
        size_t new_size = size_ + 1;

        if (Capacity() >= new_size) {
            EmplaceWithoutReallocate(end(), std::move(value));
        } else {
            EmplaceWithReallocate(end(), std::move(value));
        }

        size_ = new_size;
    }

    void Resize(size_t new_size) {
        if (Capacity() > new_size) {
            if (size_ > new_size) {
                std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            } else {
                std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            }
        } else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }

        size_ = new_size;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_raw_memory(new_capacity);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_raw_memory.GetAddress());
            std::destroy_n(data_.GetAddress(), size_);
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_raw_memory.GetAddress());
            std::destroy_n(data_.GetAddress(), size_);
        }

        data_.Swap(new_raw_memory);
    }

    void Swap(Vector& other) {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};