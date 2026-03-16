#pragma once

#include <atomic>
#include <cstring>
#include <cstddef>

namespace GuitarTuner {

// Lock-free single-producer single-consumer ring buffer.
// Capacity must be a power of 2.
template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer (size_t capacity)
        : capacity_ (capacity), mask_ (capacity - 1), buffer_ (new T[capacity])
    {
        // Ensure power of 2
        static_assert (sizeof (size_t) >= 4, "need at least 32-bit size_t");
        std::memset (buffer_, 0, capacity * sizeof (T));
    }

    ~RingBuffer ()
    {
        delete[] buffer_;
    }

    // Non-copyable
    RingBuffer (const RingBuffer&) = delete;
    RingBuffer& operator= (const RingBuffer&) = delete;

    // Push samples into the buffer (producer side)
    void push (const T* data, size_t count)
    {
        size_t w = writeHead_.load (std::memory_order_relaxed);
        for (size_t i = 0; i < count; i++)
        {
            buffer_[w & mask_] = data[i];
            w++;
        }
        writeHead_.store (w, std::memory_order_release);
    }

    // Read the latest `count` samples without advancing the read head.
    // This reads the most recent `count` samples written.
    void readLatest (T* dest, size_t count) const
    {
        size_t w = writeHead_.load (std::memory_order_acquire);
        if (w < count)
        {
            // Not enough data yet, zero-fill
            std::memset (dest, 0, count * sizeof (T));
            return;
        }
        size_t start = w - count;
        for (size_t i = 0; i < count; i++)
        {
            dest[i] = buffer_[(start + i) & mask_];
        }
    }

    // Number of samples available (written but not read)
    size_t available () const
    {
        return writeHead_.load (std::memory_order_acquire);
    }

    void clear ()
    {
        writeHead_.store (0, std::memory_order_release);
    }

private:
    size_t capacity_;
    size_t mask_;
    T* buffer_;
    std::atomic<size_t> writeHead_ {0};
};

} // namespace GuitarTuner
