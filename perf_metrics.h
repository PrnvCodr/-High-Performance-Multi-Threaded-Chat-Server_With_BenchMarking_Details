#ifndef PERF_METRICS_H
#define PERF_METRICS_H

/**
 * High-Performance Metrics System
 *
 * Lock-free atomic counters and histograms for real-time performance
 * monitoring. Tracks messages/sec, latency percentiles (P50/P95/P99), and
 * system stats.
 */

#include "win32_compat.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>


/**
 * @brief Lock-free histogram for latency tracking
 *
 * Uses logarithmic buckets for efficient percentile calculation.
 * Buckets: <1us, 1-10us, 10-100us, 100us-1ms, 1-10ms, 10-100ms, 100ms-1s, >1s
 */
class LatencyHistogram {
public:
  static constexpr int NUM_BUCKETS = 16;

  LatencyHistogram() { reset(); }

  /**
   * @brief Record a latency sample
   * @param microseconds Latency in microseconds
   */
  void record(uint64_t microseconds) {
    int bucket = getBucket(microseconds);
    buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(microseconds, std::memory_order_relaxed);

    // Update min/max with CAS
    uint64_t cur_min = min_.load(std::memory_order_relaxed);
    while (microseconds < cur_min &&
           !min_.compare_exchange_weak(cur_min, microseconds,
                                       std::memory_order_relaxed)) {
    }

    uint64_t cur_max = max_.load(std::memory_order_relaxed);
    while (microseconds > cur_max &&
           !max_.compare_exchange_weak(cur_max, microseconds,
                                       std::memory_order_relaxed)) {
    }
  }

  /**
   * @brief Record duration using high_resolution_clock
   */
  void record(std::chrono::high_resolution_clock::time_point start) {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    record(static_cast<uint64_t>(duration.count()));
  }

  /**
   * @brief Get percentile value
   * @param percentile 0.0 to 1.0 (e.g., 0.99 for P99)
   * @return Estimated latency in microseconds
   */
  uint64_t getPercentile(double percentile) const {
    uint64_t total = count_.load(std::memory_order_relaxed);
    if (total == 0)
      return 0;

    uint64_t threshold = static_cast<uint64_t>(total * percentile);
    uint64_t cumulative = 0;

    for (int i = 0; i < NUM_BUCKETS; ++i) {
      cumulative += buckets_[i].load(std::memory_order_relaxed);
      if (cumulative >= threshold) {
        return getBucketUpperBound(i);
      }
    }

    return max_.load(std::memory_order_relaxed);
  }

  uint64_t getP50() const { return getPercentile(0.50); }
  uint64_t getP95() const { return getPercentile(0.95); }
  uint64_t getP99() const { return getPercentile(0.99); }
  uint64_t getP999() const { return getPercentile(0.999); }

  uint64_t getMin() const { return min_.load(std::memory_order_relaxed); }
  uint64_t getMax() const { return max_.load(std::memory_order_relaxed); }
  uint64_t getCount() const { return count_.load(std::memory_order_relaxed); }

  double getAverage() const {
    uint64_t c = count_.load(std::memory_order_relaxed);
    if (c == 0)
      return 0.0;
    return static_cast<double>(sum_.load(std::memory_order_relaxed)) / c;
  }

  void reset() {
    for (int i = 0; i < NUM_BUCKETS; ++i) {
      buckets_[i].store(0, std::memory_order_relaxed);
    }
    count_.store(0, std::memory_order_relaxed);
    sum_.store(0, std::memory_order_relaxed);
    min_.store(UINT64_MAX, std::memory_order_relaxed);
    max_.store(0, std::memory_order_relaxed);
  }

private:
  static int getBucket(uint64_t us) {
    if (us == 0)
      return 0;
    // Logarithmic buckets: <1, 1-2, 2-4, 4-8, ...
    int bucket = 0;
    uint64_t threshold = 1;
    while (us >= threshold && bucket < NUM_BUCKETS - 1) {
      threshold *= 2;
      bucket++;
    }
    return bucket;
  }

  static uint64_t getBucketUpperBound(int bucket) {
    if (bucket == 0)
      return 1;
    return 1ULL << bucket;
  }

  std::atomic<uint64_t> buckets_[NUM_BUCKETS];
  std::atomic<uint64_t> count_;
  std::atomic<uint64_t> sum_;
  std::atomic<uint64_t> min_;
  std::atomic<uint64_t> max_;
};

/**
 * @brief Global performance metrics singleton
 */
class PerfMetrics {
public:
  static PerfMetrics &instance() {
    static PerfMetrics instance;
    return instance;
  }

  // Message throughput
  void recordMessageSent() {
    messages_sent_.fetch_add(1, std::memory_order_relaxed);
  }
  void recordMessageReceived() {
    messages_received_.fetch_add(1, std::memory_order_relaxed);
  }

  // Connection stats
  void recordConnectionAccepted() {
    connections_accepted_.fetch_add(1, std::memory_order_relaxed);
  }
  void recordConnectionRejected() {
    connections_rejected_.fetch_add(1, std::memory_order_relaxed);
  }
  void setActiveConnections(int count) {
    active_connections_.store(count, std::memory_order_relaxed);
  }

  // Latency tracking
  void
  recordMessageLatency(std::chrono::high_resolution_clock::time_point start) {
    message_latency_.record(start);
  }
  void recordQueueWaitTime(uint64_t microseconds) {
    queue_wait_time_.record(microseconds);
  }

  // Task queue
  void setQueueDepth(size_t depth) {
    queue_depth_.store(depth, std::memory_order_relaxed);
  }
  void recordTaskProcessed() {
    tasks_processed_.fetch_add(1, std::memory_order_relaxed);
  }

  // Bytes transferred
  void recordBytesSent(size_t bytes) {
    bytes_sent_.fetch_add(bytes, std::memory_order_relaxed);
  }
  void recordBytesReceived(size_t bytes) {
    bytes_received_.fetch_add(bytes, std::memory_order_relaxed);
  }

  /**
   * @brief Calculate messages per second (call periodically)
   */
  double getMessagesPerSecond() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_rate_calc_)
                       .count();

    if (elapsed < 100)
      return last_msg_rate_; // Don't recalc too often

    uint64_t current = messages_received_.load(std::memory_order_relaxed);
    uint64_t diff = current - last_msg_count_;

    last_msg_rate_ = (diff * 1000.0) / elapsed;
    last_msg_count_ = current;
    last_rate_calc_ = now;

    return last_msg_rate_;
  }

  /**
   * @brief Get formatted stats string
   */
  std::string getStatsString() {
    std::ostringstream ss;

    ss << "\n========== SERVER PERFORMANCE METRICS ==========\n";
    ss << std::fixed << std::setprecision(2);

    // Throughput
    ss << "\n[Throughput]\n";
    ss << "  Messages Received: " << messages_received_.load() << "\n";
    ss << "  Messages Sent:     " << messages_sent_.load() << "\n";
    ss << "  Message Rate:      " << getMessagesPerSecond() << " msg/s\n";
    ss << "  Bytes Received:    " << formatBytes(bytes_received_.load())
       << "\n";
    ss << "  Bytes Sent:        " << formatBytes(bytes_sent_.load()) << "\n";

    // Connections
    ss << "\n[Connections]\n";
    ss << "  Active:   " << active_connections_.load() << "\n";
    ss << "  Accepted: " << connections_accepted_.load() << "\n";
    ss << "  Rejected: " << connections_rejected_.load() << "\n";

    // Latency
    ss << "\n[Message Latency]\n";
    if (message_latency_.getCount() > 0) {
      ss << "  Samples: " << message_latency_.getCount() << "\n";
      ss << "  Avg:     " << message_latency_.getAverage() << " us\n";
      ss << "  P50:     " << message_latency_.getP50() << " us\n";
      ss << "  P95:     " << message_latency_.getP95() << " us\n";
      ss << "  P99:     " << message_latency_.getP99() << " us\n";
      ss << "  Min:     " << message_latency_.getMin() << " us\n";
      ss << "  Max:     " << message_latency_.getMax() << " us\n";
    } else {
      ss << "  (No samples yet)\n";
    }

    // Task queue
    ss << "\n[Task Queue]\n";
    ss << "  Depth:     " << queue_depth_.load() << "\n";
    ss << "  Processed: " << tasks_processed_.load() << "\n";

    ss << "\n=================================================\n";

    return ss.str();
  }

  /**
   * @brief Reset all metrics
   */
  void reset() {
    messages_sent_.store(0);
    messages_received_.store(0);
    connections_accepted_.store(0);
    connections_rejected_.store(0);
    active_connections_.store(0);
    queue_depth_.store(0);
    tasks_processed_.store(0);
    bytes_sent_.store(0);
    bytes_received_.store(0);
    message_latency_.reset();
    queue_wait_time_.reset();
    last_msg_count_ = 0;
    last_msg_rate_ = 0;
  }

private:
  PerfMetrics() {
    reset();
    last_rate_calc_ = std::chrono::steady_clock::now();
  }

  static std::string formatBytes(uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && unit < 3) {
      size /= 1024;
      unit++;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return ss.str();
  }

  // Counters
  std::atomic<uint64_t> messages_sent_{0};
  std::atomic<uint64_t> messages_received_{0};
  std::atomic<uint64_t> connections_accepted_{0};
  std::atomic<uint64_t> connections_rejected_{0};
  std::atomic<int> active_connections_{0};
  std::atomic<size_t> queue_depth_{0};
  std::atomic<uint64_t> tasks_processed_{0};
  std::atomic<uint64_t> bytes_sent_{0};
  std::atomic<uint64_t> bytes_received_{0};

  // Histograms
  LatencyHistogram message_latency_;
  LatencyHistogram queue_wait_time_;

  // Rate calculation
  std::chrono::steady_clock::time_point last_rate_calc_;
  uint64_t last_msg_count_ = 0;
  double last_msg_rate_ = 0;
};

// Convenience macros for instrumentation
#define PERF_MSG_SENT() PerfMetrics::instance().recordMessageSent()
#define PERF_MSG_RECV() PerfMetrics::instance().recordMessageReceived()
#define PERF_CONN_ACCEPT() PerfMetrics::instance().recordConnectionAccepted()
#define PERF_CONN_REJECT() PerfMetrics::instance().recordConnectionRejected()
#define PERF_SET_CONNECTIONS(n) PerfMetrics::instance().setActiveConnections(n)
#define PERF_QUEUE_DEPTH(d) PerfMetrics::instance().setQueueDepth(d)
#define PERF_TASK_DONE() PerfMetrics::instance().recordTaskProcessed()
#define PERF_BYTES_SENT(b) PerfMetrics::instance().recordBytesSent(b)
#define PERF_BYTES_RECV(b) PerfMetrics::instance().recordBytesReceived(b)

#endif // PERF_METRICS_H
