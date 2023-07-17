#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <iostream>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }
    
    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    
    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }
    
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        Swap(rhs);
        return *this;
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
    
    Vector() = default;
    
    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }
    
    Vector(const Vector& other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }
    
    Vector(Vector&& other) noexcept : data_(std::move(other.data_)), size_(other.size_) {
        other.size_ = 0;
    }

    Vector& operator=(const Vector& rhs) {
        if(this != &rhs) {
            if(rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                if(rhs.size_ < size_) {
                    std::copy(rhs.begin(), rhs.begin() + rhs.size_, begin());
                    std::destroy_n(data_ + rhs.size_, size_ - rhs.size_);
                }
                if(rhs.size_ >= size_) {
                    std::copy(rhs.begin(), rhs.begin() + size_, begin());
                    std::uninitialized_copy_n(rhs.data_ + size_, rhs.size_ - size_, data_.GetAddress());
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }
    
    Vector& operator=(Vector&& rhs) noexcept {
        if(this != &rhs) {
            if(rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(std::move(rhs));
                Swap(rhs_copy);
            } else {
                data_ = std::move(rhs.data_);
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }
    
    void Reserve(size_t new_capacity) {
        if(new_capacity < data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        Uninitialized(data_.GetAddress(), size_, new_data.GetAddress());
        DestroyAndSwap(new_data);
    }
    
    void Resize(size_t new_size) {
        if(new_size < size_) {
            std::destroy_n(data_ + new_size, size_ - new_size);
            size_ = new_size;
        }
        if(new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
            size_ = new_size;
        }
    }
    
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        if(size_ == data_.Capacity()) {
            return ReallocationEmplace(pos, std::forward<Args>(args)...);
        }
        return NoReallocationEmplace(pos, std::forward<Args>(args)...);
    }
    
    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }
    
    void PushBack(const T& value) {
        EmplaceBack(value);
    }
    
    void PushBack(T&& value) {
        if(size_ == data_.Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + size_) T(std::move(value));
            Uninitialized(data_.GetAddress(), size_, new_data.GetAddress());
            DestroyAndSwap(new_data);
            ++size_;
            return;
        }
        new (data_ + size_) T(std::move(value));
        ++size_;
    }
    
    void PopBack() noexcept {
        std::destroy_n(end() - 1, 1);
        --size_;
    }
    
    iterator Erase(const_iterator pos) noexcept {
        iterator no_const_pos = const_cast<iterator>(pos);
        if constexpr (std::is_nothrow_move_assignable_v<T>) {
            std::move(no_const_pos + 1, end(), no_const_pos);
        } else {
            std::copy(no_const_pos + 1, end(), no_const_pos);
        }
        PopBack();
        return no_const_pos;
    }
    
    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }
    
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
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
        return data_.GetAddress();
    }
    
    const_iterator cend() const noexcept {
        return data_ + size_;
    }
    
    ~Vector() {
        DestroyN(data_.GetAddress(), size_);
    }

private:
    static void Destroy(T* buf) noexcept {
        buf->~T();
    }
    
    static void DestroyN(T* buf, size_t n) noexcept {
        for(size_t i = 0; i != n; ++i) {
            Destroy(buf);
        }
    }
    
    constexpr void Uninitialized(T* data, size_t size, T* new_data ) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data, size, new_data);
        } else {
            std::uninitialized_copy_n(data, size, new_data);
        }
    }
    
    void DestroyAndSwap(RawMemory<T>& new_data) {
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }
    
    template <typename... Args>
    iterator ReallocationEmplace(const_iterator pos, Args&&... args) {
        size_t new_pos = pos - begin();
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        T* elem = new (new_data + new_pos) T(std::forward<Args>(args)...);
        Uninitialized(data_.GetAddress(), new_pos, new_data.GetAddress());
        Uninitialized(data_ + new_pos, size_ - new_pos, new_data + new_pos + 1);
        DestroyAndSwap(new_data);
        ++size_;
        return elem;
    }
    
    template <typename... Args>
    iterator NoReallocationEmplace(const_iterator pos, Args&&... args) {
        size_t new_pos = pos - begin();
        if(pos == end()) {
            new(data_ + size_) T(std::forward<Args>(args)...);
        } else {
            T elem(std::forward<Args>(args)...);
            new (data_ + size_) T(std::move(*(end() - 1)));
            std::move_backward(data_ + new_pos, end() - 1, end());
            *(data_ + new_pos) = std::move(elem);
        }
        ++size_;
        return const_cast<iterator>(pos);
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};