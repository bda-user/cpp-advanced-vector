#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept
        : buffer_(std::move(other.buffer_))
        , capacity_(std::move(other.capacity_)) {
    }

    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        buffer_ = std::move(rhs.buffer_);
        capacity_ = std::move(rhs.capacity_);
        rhs.buffer_ = nullptr;
        rhs.capacity_ = 0;
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:

    using iterator = T*;
    using const_iterator = const T*;

    Vector() noexcept = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                if (rhs.size_ < size_) {
                    size_t i = 0;
                    for (; i < rhs.size_; ++i) {
                        data_[i] = rhs.data_[i];
                    }
                    std::destroy_n(data_ + i, size_ - rhs.size_);
                } else {
                    size_t i = 0;
                    for (; i < size_; ++i) {
                        data_[i] = rhs.data_[i];
                    }
                    std::uninitialized_copy_n(rhs.data_ + i, rhs.size_ - size_, data_ + i);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        Swap(rhs);
        return *this;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        std::destroy_n(data_.GetAddress(), size_);

        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if(new_size == size_) {
            return;
        }

        if(new_size < size_) {
            std::destroy_n(data_ + new_size, size_ - new_size);
        } else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
        }
        size_ = new_size;
    }


    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_ + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_ + size_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

    void UninitializedMoveOrCopy(iterator begin, iterator end, iterator dest){
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move(begin, end, dest);
        } else {
            std::uninitialized_copy(begin, end, dest);
        }
    }

    template <typename... Args>
    iterator EmplaceRealloc(const_iterator pos, Args&&... args) {
        size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
        RawMemory<T> new_data(new_capacity);

        auto position = const_cast<T*>(pos);
        auto new_begin = new_data.GetAddress();
        auto new_position = new_data + (pos - cbegin());

        int step = 0;
        try {
            new (new_position) T(std::forward<Args>(args)...);
            ++step; // 1
            UninitializedMoveOrCopy(begin(), position, new_begin);
            ++step; // 2
            UninitializedMoveOrCopy(position, end(), new_position + 1);
            std::destroy_n(begin(), size_);
            data_.Swap(new_data);
        }  catch (...) {
            if(step == 1) {
                std::destroy_n(new_position, 1);
            } else
            if(step == 2) {
                std::destroy(new_begin, new_position + 1);
            }
            throw;
        }

        ++size_;
        return new_position;
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        auto position = const_cast<T*>(pos);

        if (size_ == Capacity())
            return EmplaceRealloc(pos, std::forward<Args>(args)...);

        if(position == end()) {
            new (position) T(std::forward<Args>(args)...);
            ++size_;
            return position;
        }

        T tmp(std::forward<Args>(args)...);
        std::uninitialized_move_n(end() - 1, 1, end());
        std::move_backward(position, end() - 1, end());
        *position = std::move(tmp);
        ++size_;
        return position;
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::forward<T>(value));
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, std::forward<T&>(const_cast<T&>(value)));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::forward<T>(value));
    }

    void PushBack(const T& value) {
        EmplaceBack(std::forward<T&>(const_cast<T&>(value)));
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_n(end() - 1, 1);
        --size_;
    }

    iterator Erase(const_iterator pos) {
        auto position = const_cast<T*>(pos);
        std::move(position + 1, end(), position);
        PopBack();
        return position;
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

    void Swap(Vector& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};
