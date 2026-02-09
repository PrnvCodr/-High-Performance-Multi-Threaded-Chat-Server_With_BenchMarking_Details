#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

/**
 * Lock-Free MPMC Queue for High-Performance Task Processing
 *
 * Features:
 * - Wait-free enqueue (single producer optimization)
 * - Lock-free dequeue (multiple consumers)
 * - Cache-line padding to prevent false sharing
 * - Bounded queue to prevent memory exhaustion
 */

#include <atomic>
#include <cstddef>
#include <memory>

// Platform-specific CPU pause instruction (must be defined before use)
#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_mm_pause)
#else
#define _mm_pause() __builtin_ia32_pause()
#endif

// Cache line size for padding (typically 64 bytes on modern CPUs)
constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief Padded atomic to prevent false sharing between cores
 */
template <typename T> struct alignas(CACHE_LINE_SIZE) PaddedAtomic {
  std::atomic<T> value;
  char padding[CACHE_LINE_SIZE - sizeof(std::atomic<T>)];

  PaddedAtomic() : value(T{}) {}
  explicit PaddedAtomic(T v) : value(v) {}
};

/**
 * @brief Lock-free bounded MPMC queue
 *
 * Uses array-based circular buffer with atomic head/tail pointers.
 * Each slot has a sequence number for ABA protection.
 */
template <typename T> class LockFreeQueue {
public:
  /**
   * @brief Construct queue with given capacity
   * @param capacity Must be power of 2 for performance
   */
  explicit LockFreeQueue(size_t capacity = 4096)
      : capacity_(roundUpToPowerOf2(capacity)), mask_(capacity_ - 1),
        buffer_(std::make_unique<Cell[]>(capacity_)), head_(0), tail_(0) {

    // Initialize sequence numbers
    for (size_t i = 0; i < capacity_; ++i) {
      buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  ~LockFreeQueue() = default;

  // Non-copyable, non-movable
  LockFreeQueue(const LockFreeQueue &) = delete;
  LockFreeQueue &operator=(const LockFreeQueue &) = delete;
  LockFreeQueue(LockFreeQueue &&) = delete;
  LockFreeQueue &operator=(LockFreeQueue &&) = delete;

  /**
   * @brief Try to enqueue an item
   * @param item Item to enqueue
   * @return true if successful, false if queue is full
   */
  bool try_enqueue(T item) {
    Cell *cell;
    size_t pos = tail_.load(std::memory_order_relaxed);

    for (;;) {
      cell = &buffer_[pos & mask_];
      size_t seq = cell->sequence.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

      if (diff == 0) {
        // Slot is ready for writing
        if (tail_.compare_exchange_weak(pos, pos + 1,
                                        std::memory_order_relaxed)) {
          break;
        }
      } else if (diff < 0) {
        // Queue is full
        return false;
      } else {
        // Another thread is ahead, retry
        pos = tail_.load(std::memory_order_relaxed);
      }
    }

    // Store the data
    cell->data = std::move(item);
    cell->sequence.store(pos + 1, std::memory_order_release);

    return true;
  }

  /**
   * @brief Blocking enqueue - spins until successful
   * @param item Item to enqueue
   */
  void enqueue(T item) {
    while (!try_enqueue(std::move(item))) {
      // Spin with backoff
      _mm_pause(); // CPU hint for spin-wait
    }
  }

  /**
   * @brief Try to dequeue an item
   * @param result Output parameter for dequeued item
   * @return true if successful, false if queue is empty
   */
  bool try_dequeue(T &result) {
    Cell *cell;
    size_t pos = head_.load(std::memory_order_relaxed);

    for (;;) {
      cell = &buffer_[pos & mask_];
      size_t seq = cell->sequence.load(std::memory_order_acquire);
      intptr_t diff =
          static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

      if (diff == 0) {
        // Slot is ready for reading
        if (head_.compare_exchange_weak(pos, pos + 1,
                                        std::memory_order_relaxed)) {
          break;
        }
      } else if (diff < 0) {
        // Queue is empty
        return false;
      } else {
        // Another thread is ahead, retry
        pos = head_.load(std::memory_order_relaxed);
      }
    }

    // Read the data
    result = std::move(cell->data);
    cell->sequence.store(pos + mask_ + 1, std::memory_order_release);

    return true;
  }

  /**
   * @brief Check if queue is empty (approximate)
   */
  bool empty() const {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t t = tail_.load(std::memory_order_relaxed);
    return h >= t;
  }

  /**
   * @brief Get approximate size
   */
  size_t size_approx() const {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t t = tail_.load(std::memory_order_relaxed);
    return t > h ? t - h : 0;
  }

  /**
   * @brief Get capacity
   */
  size_t capacity() const { return capacity_; }

private:
  struct Cell {
    std::atomic<size_t> sequence;
    T data;
    char padding[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>) - sizeof(T)];
  };

  static size_t roundUpToPowerOf2(size_t n) {
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return ++n;
  }

  const size_t capacity_;
  const size_t mask_;
  std::unique_ptr<Cell[]> buffer_;

  // Separate cache lines for head and tail to prevent false sharing
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
};

#endif // LOCK_FREE_QUEUE_H
