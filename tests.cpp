/**
 * Unit Tests for High-Performance Chat Server Components
 *
 * Lightweight test framework compatible with MinGW 6.3.0+
 * Tests cover: LockFreeQueue, LatencyHistogram, ThreadPool
 *
 * Build: g++ -std=c++17 -O2 -o build/tests.exe tests.cpp thread_pool.cpp
 * -lws2_32 Run: build/tests.exe
 */

// Windows headers must come first
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winsock2.h>

// STL headers
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <vector>

// Include components to test
#include "lock_free_queue.h"
#include "perf_metrics.h"
#include "thread_pool.h"

// ============================================================================
// Simple Test Framework
// ============================================================================

namespace test {

struct TestResult {
  std::string name;
  bool passed;
  std::string message;
};

std::vector<TestResult> g_results;
int g_passed = 0;
int g_failed = 0;

// ANSI colors for Windows 10+
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET "\033[0m"

void EnableColors() {
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  GetConsoleMode(hOut, &dwMode);
  SetConsoleMode(hOut, dwMode | 0x0004);
}

#define TEST(name)                                                             \
  void test_##name();                                                          \
  struct TestRegistrar_##name {                                                \
    TestRegistrar_##name() {                                                   \
      try {                                                                    \
        test_##name();                                                         \
        test::g_results.push_back({#name, true, ""});                          \
        test::g_passed++;                                                      \
      } catch (const std::exception &e) {                                      \
        test::g_results.push_back({#name, false, e.what()});                   \
        test::g_failed++;                                                      \
      }                                                                        \
    }                                                                          \
  } registrar_##name;                                                          \
  void test_##name()

#define ASSERT_TRUE(expr)                                                      \
  if (!(expr)) {                                                               \
    throw std::runtime_error("ASSERT_TRUE failed: " #expr);                    \
  }

#define ASSERT_FALSE(expr)                                                     \
  if (expr) {                                                                  \
    throw std::runtime_error("ASSERT_FALSE failed: " #expr);                   \
  }

#define ASSERT_EQ(a, b)                                                        \
  if ((a) != (b)) {                                                            \
    std::ostringstream ss;                                                     \
    ss << "ASSERT_EQ failed: " << (a) << " != " << (b);                        \
    throw std::runtime_error(ss.str());                                        \
  }

#define ASSERT_GT(a, b)                                                        \
  if (!((a) > (b))) {                                                          \
    std::ostringstream ss;                                                     \
    ss << "ASSERT_GT failed: " << (a) << " <= " << (b);                        \
    throw std::runtime_error(ss.str());                                        \
  }

#define ASSERT_GE(a, b)                                                        \
  if (!((a) >= (b))) {                                                         \
    std::ostringstream ss;                                                     \
    ss << "ASSERT_GE failed: " << (a) << " < " << (b);                         \
    throw std::runtime_error(ss.str());                                        \
  }

} // namespace test

// ============================================================================
// Helper: Simple thread launcher using Windows API
// ============================================================================
struct ThreadArgs {
  std::function<void()> func;
};

DWORD WINAPI ThreadProc(LPVOID arg) {
  ThreadArgs *args = (ThreadArgs *)arg;
  args->func();
  delete args;
  return 0;
}

HANDLE LaunchThread(std::function<void()> func) {
  ThreadArgs *args = new ThreadArgs{func};
  return CreateThread(NULL, 0, ThreadProc, args, 0, NULL);
}

// ============================================================================
// LockFreeQueue Tests
// ============================================================================

TEST(LockFreeQueue_BasicEnqueueDequeue) {
  LockFreeQueue<int> queue(16);

  ASSERT_TRUE(queue.try_enqueue(42));
  ASSERT_TRUE(queue.try_enqueue(100));

  int value;
  ASSERT_TRUE(queue.try_dequeue(value));
  ASSERT_EQ(value, 42);

  ASSERT_TRUE(queue.try_dequeue(value));
  ASSERT_EQ(value, 100);

  ASSERT_FALSE(queue.try_dequeue(value)); // Empty
}

TEST(LockFreeQueue_EmptyCheck) {
  LockFreeQueue<int> queue(8);

  ASSERT_TRUE(queue.empty());
  ASSERT_EQ(queue.size_approx(), 0u);

  queue.try_enqueue(1);
  ASSERT_FALSE(queue.empty());
  ASSERT_EQ(queue.size_approx(), 1u);

  int val;
  queue.try_dequeue(val);
  ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueue_BoundedCapacity) {
  LockFreeQueue<int> queue(4);

  ASSERT_TRUE(queue.try_enqueue(1));
  ASSERT_TRUE(queue.try_enqueue(2));
  ASSERT_TRUE(queue.try_enqueue(3));
  ASSERT_TRUE(queue.try_enqueue(4));

  // Queue should be full now
  ASSERT_FALSE(queue.try_enqueue(5));
}

TEST(LockFreeQueue_FIFO_Order) {
  LockFreeQueue<int> queue(64);

  for (int i = 0; i < 50; ++i) {
    queue.try_enqueue(i);
  }

  for (int i = 0; i < 50; ++i) {
    int value;
    ASSERT_TRUE(queue.try_dequeue(value));
    ASSERT_EQ(value, i);
  }
}

TEST(LockFreeQueue_ConcurrentProducerConsumer) {
  LockFreeQueue<int> queue(4096);
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  const int NUM_ITEMS = 10000;

  // Producer thread
  HANDLE producer = LaunchThread([&]() {
    for (int i = 0; i < NUM_ITEMS; ++i) {
      while (!queue.try_enqueue(i)) {
        Sleep(0);
      }
      produced++;
    }
  });

  // Consumer thread
  HANDLE consumer = LaunchThread([&]() {
    int count = 0;
    int value;
    while (count < NUM_ITEMS) {
      if (queue.try_dequeue(value)) {
        count++;
        consumed++;
      } else {
        Sleep(0);
      }
    }
  });

  WaitForSingleObject(producer, INFINITE);
  WaitForSingleObject(consumer, INFINITE);
  CloseHandle(producer);
  CloseHandle(consumer);

  ASSERT_EQ(produced.load(), NUM_ITEMS);
  ASSERT_EQ(consumed.load(), NUM_ITEMS);
  ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueue_MultiProducerMultiConsumer) {
  LockFreeQueue<int> queue(8192);
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};
  const int ITEMS_PER_PRODUCER = 2500;
  const int NUM_PRODUCERS = 4;
  const int NUM_CONSUMERS = 4;

  std::vector<HANDLE> producers;
  std::vector<HANDLE> consumers;
  std::atomic<bool> done{false};

  // Start producers
  for (int p = 0; p < NUM_PRODUCERS; ++p) {
    producers.push_back(LaunchThread([&, p]() {
      for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        while (!queue.try_enqueue(p * ITEMS_PER_PRODUCER + i)) {
          Sleep(0);
        }
        total_produced++;
      }
    }));
  }

  // Start consumers
  for (int c = 0; c < NUM_CONSUMERS; ++c) {
    consumers.push_back(LaunchThread([&]() {
      int value;
      while (!done || !queue.empty()) {
        if (queue.try_dequeue(value)) {
          total_consumed++;
        } else {
          Sleep(0);
        }
      }
    }));
  }

  // Wait for producers
  for (HANDLE h : producers) {
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }

  // Signal consumers
  while (total_consumed < total_produced) {
    Sleep(10);
  }
  done = true;

  for (HANDLE h : consumers) {
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }

  ASSERT_EQ(total_produced.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
  ASSERT_EQ(total_consumed.load(), total_produced.load());
}

// ============================================================================
// LatencyHistogram Tests
// ============================================================================

TEST(LatencyHistogram_BasicRecording) {
  LatencyHistogram hist;

  hist.record(100);
  hist.record(200);
  hist.record(300);

  ASSERT_EQ(hist.getCount(), 3u);
  ASSERT_EQ(hist.getMin(), 100u);
  ASSERT_EQ(hist.getMax(), 300u);
}

TEST(LatencyHistogram_Average) {
  LatencyHistogram hist;

  hist.record(100);
  hist.record(200);
  hist.record(300);

  double avg = hist.getAverage();
  ASSERT_GE(avg, 199.0);
  ASSERT_GE(201.0, avg);
}

TEST(LatencyHistogram_Percentiles) {
  LatencyHistogram hist;

  for (int i = 0; i < 100; ++i) {
    if (i < 50)
      hist.record(10);
    else if (i < 95)
      hist.record(100);
    else
      hist.record(1000);
  }

  ASSERT_GE(hist.getP50(), 1u);
  ASSERT_GT(hist.getP99(), hist.getP50());
}

TEST(LatencyHistogram_Reset) {
  LatencyHistogram hist;

  hist.record(500);
  hist.record(1000);
  ASSERT_EQ(hist.getCount(), 2u);

  hist.reset();
  ASSERT_EQ(hist.getCount(), 0u);
}

// ============================================================================
// ThreadPool Tests
// ============================================================================

TEST(ThreadPool_BasicExecution) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  for (int i = 0; i < 10; ++i) {
    pool.enqueue([&counter]() { counter++; });
  }

  Sleep(100);

  ASSERT_EQ(counter.load(), 10);
}

TEST(ThreadPool_ParallelExecution) {
  ThreadPool pool(4);
  std::atomic<int> active{0};
  std::atomic<int> max_active{0};

  for (int i = 0; i < 8; ++i) {
    pool.enqueue([&]() {
      int current = ++active;
      int expected = max_active.load();
      while (current > expected &&
             !max_active.compare_exchange_weak(expected, current)) {
      }
      Sleep(50);
      --active;
    });
  }

  Sleep(500);

  ASSERT_GT(max_active.load(), 1);
}

TEST(ThreadPool_TasksComplete) {
  ThreadPool pool(2);
  std::atomic<int> completed{0};
  const int NUM_TASKS = 100;

  for (int i = 0; i < NUM_TASKS; ++i) {
    pool.enqueue([&]() {
      Sleep(1);
      completed++;
    });
  }

  Sleep(1500);

  ASSERT_EQ(completed.load(), NUM_TASKS);
}

TEST(ThreadPool_GracefulShutdown) {
  std::atomic<int> completed{0};

  {
    ThreadPool pool(2);
    for (int i = 0; i < 10; ++i) {
      pool.enqueue([&]() {
        Sleep(10);
        completed++;
      });
    }
  }

  ASSERT_EQ(completed.load(), 10);
}

// ============================================================================
// PerfMetrics Tests
// ============================================================================

TEST(PerfMetrics_MessageCounting) {
  auto &metrics = PerfMetrics::instance();
  metrics.reset();

  for (int i = 0; i < 100; ++i) {
    metrics.recordMessageReceived();
    metrics.recordMessageSent();
  }

  std::string stats = metrics.getStatsString();
  ASSERT_TRUE(stats.find("Messages Received: 100") != std::string::npos);
}

TEST(PerfMetrics_ByteTracking) {
  auto &metrics = PerfMetrics::instance();
  metrics.reset();

  metrics.recordBytesReceived(1024);
  metrics.recordBytesSent(2048);

  std::string stats = metrics.getStatsString();
  ASSERT_TRUE(stats.find("Bytes") != std::string::npos);
}

// ============================================================================
// Main
// ============================================================================

int main() {
  test::EnableColors();

  std::cout << "\n";
  std::cout
      << "================================================================\n";
  std::cout << "  High-Performance Chat Server - Unit Tests\n";
  std::cout
      << "================================================================\n\n";

  std::cout << "Test Results:\n";
  std::cout << "-------------\n";

  for (const auto &result : test::g_results) {
    if (result.passed) {
      std::cout << COLOR_GREEN << "[PASS]" << COLOR_RESET << " " << result.name
                << "\n";
    } else {
      std::cout << COLOR_RED << "[FAIL]" << COLOR_RESET << " " << result.name
                << "\n";
      std::cout << "       " << result.message << "\n";
    }
  }

  std::cout << "\n";
  std::cout
      << "================================================================\n";
  std::cout << "  Summary: " << test::g_passed << " passed, " << test::g_failed
            << " failed\n";
  std::cout
      << "================================================================\n";

  if (test::g_failed > 0) {
    std::cout << COLOR_RED << "\n  SOME TESTS FAILED!\n" << COLOR_RESET;
    return 1;
  } else {
    std::cout << COLOR_GREEN << "\n  ALL TESTS PASSED!\n" << COLOR_RESET;
    return 0;
  }
}
