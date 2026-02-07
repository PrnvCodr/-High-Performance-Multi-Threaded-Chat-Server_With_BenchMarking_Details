#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

/**
 * High-Performance Object Pool
 *
 * Pre-allocates objects to eliminate heap allocation during hot paths.
 * Thread-safe using lock-free stack.
 */

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <vector>


/**
 * @brief Lock-free object pool for connection data
 *
 * Uses an atomic free-list stack for O(1) acquire/release.
 * Pre-allocates all objects on construction.
 */
template <typename T> class ObjectPool {
public:
  /**
   * @brief Construct pool with given capacity
   * @param capacity Number of objects to pre-allocate
   */
  explicit ObjectPool(size_t capacity)
      : capacity_(capacity),
        storage_(std::make_unique<StorageNode[]>(capacity)),
        free_list_(nullptr), allocated_count_(0) {

    // Initialize all nodes and push to free list
    for (size_t i = 0; i < capacity; ++i) {
      storage_[i].next = free_list_.load(std::memory_order_relaxed);
      free_list_.store(&storage_[i], std::memory_order_relaxed);
    }
  }

  ~ObjectPool() {
    // Objects are automatically destroyed when storage_ is deleted
  }

  // Non-copyable, non-movable
  ObjectPool(const ObjectPool &) = delete;
  ObjectPool &operator=(const ObjectPool &) = delete;
  ObjectPool(ObjectPool &&) = delete;
  ObjectPool &operator=(ObjectPool &&) = delete;

  /**
   * @brief Acquire an object from the pool
   * @return Pointer to object, or nullptr if pool is exhausted
   */
  T *acquire() {
    StorageNode *node = pop_free_list();
    if (!node) {
      return nullptr; // Pool exhausted
    }

    allocated_count_.fetch_add(1, std::memory_order_relaxed);

    // Construct object in-place
    return new (&node->data) T();
  }

  /**
   * @brief Acquire with constructor arguments
   */
  template <typename... Args> T *acquire(Args &&...args) {
    StorageNode *node = pop_free_list();
    if (!node) {
      return nullptr;
    }

    allocated_count_.fetch_add(1, std::memory_order_relaxed);

    // Construct object with arguments
    return new (&node->data) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Release an object back to the pool
   * @param obj Pointer to object (must be from this pool)
   */
  void release(T *obj) {
    if (!obj)
      return;

    // Call destructor
    obj->~T();

    // Get the storage node containing this object
    StorageNode *node = reinterpret_cast<StorageNode *>(
        reinterpret_cast<char *>(obj) - offsetof(StorageNode, data));

    push_free_list(node);
    allocated_count_.fetch_sub(1, std::memory_order_relaxed);
  }

  /**
   * @brief Get number of currently allocated objects
   */
  size_t allocated() const {
    return allocated_count_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get number of available objects
   */
  size_t available() const { return capacity_ - allocated(); }

  /**
   * @brief Get total capacity
   */
  size_t capacity() const { return capacity_; }

  /**
   * @brief RAII wrapper for automatic release
   */
  class Handle {
  public:
    Handle() : pool_(nullptr), obj_(nullptr) {}
    Handle(ObjectPool *pool, T *obj) : pool_(pool), obj_(obj) {}

    ~Handle() {
      if (pool_ && obj_) {
        pool_->release(obj_);
      }
    }

    // Move-only
    Handle(Handle &&other) noexcept : pool_(other.pool_), obj_(other.obj_) {
      other.pool_ = nullptr;
      other.obj_ = nullptr;
    }

    Handle &operator=(Handle &&other) noexcept {
      if (this != &other) {
        if (pool_ && obj_)
          pool_->release(obj_);
        pool_ = other.pool_;
        obj_ = other.obj_;
        other.pool_ = nullptr;
        other.obj_ = nullptr;
      }
      return *this;
    }

    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    T *get() const { return obj_; }
    T *operator->() const { return obj_; }
    T &operator*() const { return *obj_; }
    explicit operator bool() const { return obj_ != nullptr; }

    T *release() {
      T *tmp = obj_;
      obj_ = nullptr;
      pool_ = nullptr;
      return tmp;
    }

  private:
    ObjectPool *pool_;
    T *obj_;
  };

  /**
   * @brief Acquire with RAII handle
   */
  Handle acquire_handle() { return Handle(this, acquire()); }

  template <typename... Args> Handle acquire_handle(Args &&...args) {
    return Handle(this, acquire(std::forward<Args>(args)...));
  }

private:
  struct StorageNode {
    StorageNode *next;
    alignas(T) unsigned char data[sizeof(T)];
  };

  StorageNode *pop_free_list() {
    StorageNode *old_head = free_list_.load(std::memory_order_acquire);

    while (old_head) {
      StorageNode *new_head = old_head->next;
      if (free_list_.compare_exchange_weak(old_head, new_head,
                                           std::memory_order_release,
                                           std::memory_order_acquire)) {
        return old_head;
      }
      // CAS failed, old_head is updated, retry
    }

    return nullptr; // Pool exhausted
  }

  void push_free_list(StorageNode *node) {
    StorageNode *old_head = free_list_.load(std::memory_order_relaxed);
    do {
      node->next = old_head;
    } while (!free_list_.compare_exchange_weak(
        old_head, node, std::memory_order_release, std::memory_order_relaxed));
  }

  const size_t capacity_;
  std::unique_ptr<StorageNode[]> storage_;
  std::atomic<StorageNode *> free_list_;
  std::atomic<size_t> allocated_count_;
};

#endif // OBJECT_POOL_H
