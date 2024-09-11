#pragma once
#include <cassert>
#include <cstdlib>
#include <execution>
#include <new>
#include <utility>

template<typename T>
class RawMemory
{
public:
    RawMemory() = default;

    RawMemory(const RawMemory &) = delete;
    RawMemory &operator=(const RawMemory &rhs) = delete;
    RawMemory(RawMemory &&other) noexcept
        : buffer_(other.buffer_), capacity_(other.capacity_)
    {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
    }

    RawMemory &operator=(RawMemory &&rhs) noexcept
    {
        if (this != &rhs) {
            Deallocate(buffer_);

            buffer_ = rhs.buffer_;
            capacity_ = rhs.capacity_;

            rhs.buffer_ = nullptr;
            rhs.capacity_ = 0;
        }
        return *this;
    }

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity)), capacity_(capacity)
    {}

    ~RawMemory()
    { Deallocate(buffer_); }

    T *operator+(size_t offset) noexcept
    {
        // Разрешается получать адрес ячейки памяти, следующей за последним
        // элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T *operator+(size_t offset) const noexcept
    {
        return const_cast<RawMemory &>(*this) + offset;
    }

    const T &operator[](size_t index) const noexcept
    {
        return const_cast<RawMemory &>(*this)[index];
    }

    T &operator[](size_t index) noexcept
    {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory &other) noexcept
    {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T *GetAddress() const noexcept
    { return buffer_; }

    T *GetAddress() noexcept
    { return buffer_; }

    size_t Capacity() const
    { return capacity_; }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T *Allocate(size_t n)
    {
        return n != 0 ? static_cast<T *>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи
    // Allocate
    static void Deallocate(T *buf) noexcept
    { operator delete(buf); }

    T *buffer_ = nullptr;
    size_t capacity_ = 0;
};

template<typename T>
class Vector
{
public:
    Vector() = default;

    explicit Vector(size_t size)
        : data_(size), size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector &other)
        : data_(other.size_), size_(other.size_)
    {
        {
            std::uninitialized_copy_n(other.data_.GetAddress(), size_,
                                      data_.GetAddress());
        }
    }

    Vector(Vector &&other) noexcept
    { Swap(other); }

    Vector &operator=(const Vector &rhs)
    {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector temp(rhs);
                Swap(temp);
            }
            else {
                if (rhs.size_ >= size_) {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + size_,
                              data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_,
                                              rhs.size_ - size_,
                                              data_.GetAddress() + size_);
                }
                else {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + rhs.size_,
                              data_.GetAddress());
                    DestroyN(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector &operator=(Vector &&rhs) noexcept
    {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    void Swap(Vector &other) noexcept
    {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    };

    size_t Capacity() const noexcept
    { return data_.Capacity(); }
    size_t Size() const noexcept
    { return size_; }
    const T &operator[](size_t index) const noexcept
    { return data_[index]; }

    T &operator[](size_t index) noexcept
    {
        assert(index < size_);
        return data_[index];
    }

    void Reserve(size_t new_capacity)
    {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        if constexpr (std::is_nothrow_move_constructible_v<T> ||
            !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_,
                                      new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_,
                                      new_data.GetAddress());
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size)
    {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_,
                                                 new_size - size_);
        }
        size_ = new_size;
    }

    using iterator = T *;
    using const_iterator = const T *;

    iterator begin() noexcept
    { return data_.GetAddress(); }

    iterator end() noexcept
    { return data_.GetAddress() + size_; }

    const_iterator begin() const noexcept
    { return data_.GetAddress(); }

    const_iterator end() const noexcept
    { return data_.GetAddress() + size_; }

    const_iterator cbegin() const noexcept
    { return data_.GetAddress(); }

    const_iterator cend() const noexcept
    { return data_.GetAddress() + size_; }

    template<typename... Args>
    iterator Emplace(const_iterator pos, Args &&... args)
    {
        assert(pos >= begin() && pos <= end());

        if (size_ == Capacity()) {
            size_t it_pos = pos - begin();
            RawMemory<T> new_memory(size_ == 0 ? 1 : size_ * 2);
            Construct(new_memory.GetAddress() + it_pos, std::forward<Args>(args)...);
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> ||
                    !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress(), it_pos,
                                              new_memory.GetAddress());
                    std::uninitialized_move_n(data_.GetAddress() + it_pos, end() - pos,
                                              new_memory.GetAddress() + it_pos + 1);

                }
                else {
                    std::uninitialized_copy_n(data_.GetAddress(), it_pos,
                                              new_memory.GetAddress());
                    try {
                        std::uninitialized_copy_n(data_.GetAddress() + it_pos, end() - pos,
                                                  new_memory.GetAddress() + it_pos + 1);
                    }
                    catch (...) {
                        std::destroy_n(new_memory.GetAddress(), it_pos);
                        throw;
                    }
                }
            }
            catch (...) {
                std::destroy_at(new_memory.GetAddress() + it_pos);
                throw;
            }
            data_.Swap(new_memory);
            std::destroy_n(new_memory.GetAddress(), size_);
            size_++;

            return begin() + it_pos;
        }

        size_t it_pos = pos - begin();

        if (pos == cend()) {
            Construct(end(), std::forward<Args>(args)...);
            size_++;
            return const_cast<iterator>(pos);
        }
        else {
            T temp(std::forward<Args>(args)...);
            Construct(end(), std::move(*(end() - 1)));
            try {
                std::move_backward(const_cast<iterator>(pos), end() - 1, end());
            }
            catch (...) {
                std::destroy_at(end());
            }
            *(begin() + it_pos) = std::move(temp);

            size_++;
            return const_cast<iterator>(pos);
        }
    }

    iterator Erase(const_iterator pos) noexcept(
    std::is_nothrow_move_assignable_v<T>)
    {
        assert(pos >= begin() && pos < end());
        size_t shift = pos - begin();
        std::move(begin() + shift + 1, end(), begin() + shift);
        PopBack();
        return begin() + shift;
    }

    iterator Insert(const_iterator pos, const T &value)
    {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T &&value)
    {
        return Emplace(pos, std::move(value));
    }

    template<typename... Args>
    T &EmplaceBack(Args &&... args)
    {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    void PushBack(const T &value)
    { EmplaceBack(std::forward<const T &>(value)); }

    void PushBack(T &&value)
    { EmplaceBack(std::forward<T &&>(value)); }

    void PopBack() noexcept
    {
        assert(size_ > 0);
        Destroy(data_.GetAddress() + size_ - 1);
        size_--;
    }

    ~Vector()
    { DestroyN(data_.GetAddress(), size_); }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    static void DestroyN(T *buf, size_t n) noexcept
    {
        for (size_t i = 0; i != n; ++i) {
            Destroy(buf + i);
        }
    }

    template<typename... Type>
    static void Construct(T *data, Type &&... elem)
    {
        new(data) T(std::forward<Type>(elem)...);
    }

    RawMemory<T> CreateCopy(size_t capacity)
    {
        RawMemory<T> new_data(capacity);

        if constexpr (std::is_nothrow_move_constructible_v<T> ||
            !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), Size(),
                                      new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), Size(),
                                      new_data.GetAddress());
        }

        return new_data;
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T *buf, const T &elem)
    { new(buf) T(elem); }
    static void Destroy(T *buf) noexcept
    { buf->~T(); }
};