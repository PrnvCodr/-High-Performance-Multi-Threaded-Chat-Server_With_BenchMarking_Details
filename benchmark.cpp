/**
 * Benchmark Suite for High-Performance Chat Server
 *
 * Measures actual throughput and latency for resume-worthy metrics.
 * Generates BENCHMARK_RESULTS.md with real numbers.
 * Compatible with MinGW 6.3.0+
 *
 * Build: g++ -std=c++17 -O2 -o build/benchmark.exe benchmark.cpp
 * thread_pool.cpp -lws2_32 Run: build/benchmark.exe
 */

// Windows headers must come first
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winsock2.h>

// STL headers
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

#include "lock_free_queue.h"
#include "perf_metrics.h"
#include "thread_pool.h"

// ============================================================================
// Helper: Simple thread launcher using Windows API
// ============================================================================
struct BenchThreadArgs {
  std::function<void()> func;
};

DWORD WINAPI BenchThreadProc(LPVOID arg) {
  BenchThreadArgs *args = (BenchThreadArgs *)arg;
  args->func();
  delete args;
  return 0;
}

HANDLE LaunchBenchThread(std::function<void()> func) {
  BenchThreadArgs *args = new BenchThreadArgs{func};
  return CreateThread(NULL, 0, BenchThreadProc, args, 0, NULL);
}

// ============================================================================
// Benchmark Utilities
// ============================================================================

class Benchmark {
public:
  struct Result {
    std::string name;
    double ops_per_second;
    double avg_latency_us;
    double p50_latency_us;
    double p95_latency_us;
    double p99_latency_us;
  };

  static std::vector<Result> results;

  template <typename Func>
  static void Run(const std::string &name, int iterations, Func &&func) {
    std::cout << "  Running: " << name << "..." << std::flush;

    // Warmup
    for (int i = 0; i < std::min(100, iterations / 10); ++i) {
      func();
    }

    // Timed run
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    for (int i = 0; i < iterations; ++i) {
      func();
    }

    QueryPerformanceCounter(&end);

    double total_us =
        ((double)(end.QuadPart - start.QuadPart) * 1000000.0) / freq.QuadPart;
    double ops_per_sec = (iterations * 1000000.0) / total_us;
    double avg_latency = total_us / iterations;

    Result r;
    r.name = name;
    r.ops_per_second = ops_per_sec;
    r.avg_latency_us = avg_latency;
    r.p50_latency_us = 0;
    r.p95_latency_us = 0;
    r.p99_latency_us = 0;
    results.push_back(r);

    std::cout << " " << std::fixed << std::setprecision(0) << ops_per_sec
              << " ops/sec\n";
  }

  template <typename Func>
  static void RunWithLatency(const std::string &name, int iterations,
                             Func &&func) {
    std::cout << "  Running: " << name << "..." << std::flush;

    std::vector<double> latencies;
    latencies.reserve(iterations);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    // Warmup
    for (int i = 0; i < std::min(100, iterations / 10); ++i) {
      func();
    }

    // Timed run with individual measurements
    for (int i = 0; i < iterations; ++i) {
      LARGE_INTEGER start, end;
      QueryPerformanceCounter(&start);
      func();
      QueryPerformanceCounter(&end);
      double dur_us =
          ((double)(end.QuadPart - start.QuadPart) * 1000000.0) / freq.QuadPart;
      latencies.push_back(dur_us);
    }

    // Calculate statistics
    std::sort(latencies.begin(), latencies.end());
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    double avg = sum / iterations;
    double p50 = latencies[static_cast<size_t>(iterations * 0.50)];
    double p95 = latencies[static_cast<size_t>(iterations * 0.95)];
    double p99 = latencies[static_cast<size_t>(iterations * 0.99)];

    double ops_per_sec = (iterations * 1000000.0) / sum;

    Result r;
    r.name = name;
    r.ops_per_second = ops_per_sec;
    r.avg_latency_us = avg;
    r.p50_latency_us = p50;
    r.p95_latency_us = p95;
    r.p99_latency_us = p99;
    results.push_back(r);

    std::cout << " " << std::fixed << std::setprecision(0) << ops_per_sec
              << " ops/sec"
              << " (P99: " << std::setprecision(2) << p99 << " us)\n";
  }
};

std::vector<Benchmark::Result> Benchmark::results;

// ============================================================================
// Lock-Free Queue Benchmarks
// ============================================================================

void BenchmarkLockFreeQueue() {
  std::cout << "\n[Lock-Free Queue Benchmarks]\n";
  std::cout << "-----------------------------\n";

  // Single-threaded enqueue
  {
    LockFreeQueue<int> queue(1048576);
    Benchmark::Run("Single-thread enqueue", 1000000,
                   [&]() { queue.try_enqueue(42); });
  }

  // Single-threaded dequeue
  {
    LockFreeQueue<int> queue(1048576);
    for (int i = 0; i < 1000000; ++i)
      queue.try_enqueue(i);
    int val;
    Benchmark::Run("Single-thread dequeue", 1000000,
                   [&]() { queue.try_dequeue(val); });
  }

  // Single-threaded round-trip
  {
    LockFreeQueue<int> queue(1024);
    int val;
    Benchmark::RunWithLatency("Enqueue+Dequeue round-trip", 100000, [&]() {
      queue.try_enqueue(42);
      queue.try_dequeue(val);
    });
  }

  // Multi-threaded throughput (4P/4C)
  {
    LockFreeQueue<int> queue(1048576);
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> ops{0};

    std::vector<HANDLE> threads;

    // 4 Producers
    for (int i = 0; i < 4; ++i) {
      threads.push_back(LaunchBenchThread([&]() {
        while (!start)
          Sleep(0);
        while (!stop) {
          if (queue.try_enqueue(42))
            ops++;
        }
      }));
    }

    // 4 Consumers
    for (int i = 0; i < 4; ++i) {
      threads.push_back(LaunchBenchThread([&]() {
        while (!start)
          Sleep(0);
        int val;
        while (!stop) {
          if (queue.try_dequeue(val))
            ops++;
        }
      }));
    }

    std::cout << "  Running: 4P/4C concurrent throughput..." << std::flush;
    start = true;
    Sleep(2000);
    stop = true;

    for (HANDLE h : threads) {
      WaitForSingleObject(h, INFINITE);
      CloseHandle(h);
    }

    double ops_per_sec = ops.load() / 2.0;
    std::cout << " " << std::fixed << std::setprecision(0) << ops_per_sec
              << " ops/sec\n";

    Benchmark::Result r;
    r.name = "4P/4C concurrent throughput";
    r.ops_per_second = ops_per_sec;
    r.avg_latency_us = 0;
    Benchmark::results.push_back(r);
  }
}

// ============================================================================
// Thread Pool Benchmarks
// ============================================================================

void BenchmarkThreadPool() {
  std::cout << "\n[Thread Pool Benchmarks]\n";
  std::cout << "------------------------\n";

  // Task dispatch latency
  {
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    Benchmark::RunWithLatency("Task dispatch latency", 100000, [&]() {
      pool.enqueue([&counter]() { counter++; });
    });

    // Wait for completion
    while (counter < 100000)
      Sleep(1);
  }

  // Throughput test
  {
    ThreadPool pool(4);
    std::atomic<int> completed{0};
    const int TASKS = 100000;

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    for (int i = 0; i < TASKS; ++i) {
      pool.enqueue([&completed]() { completed++; });
    }

    // Wait for completion
    while (completed < TASKS)
      Sleep(1);

    QueryPerformanceCounter(&end);
    double duration_ms =
        ((double)(end.QuadPart - start.QuadPart) * 1000.0) / freq.QuadPart;
    double tasks_per_sec = (TASKS * 1000.0) / duration_ms;

    std::cout << "  Task throughput (4 threads): " << std::fixed
              << std::setprecision(0) << tasks_per_sec << " tasks/sec\n";

    Benchmark::Result r;
    r.name = "ThreadPool task throughput (4 threads)";
    r.ops_per_second = tasks_per_sec;
    Benchmark::results.push_back(r);
  }
}

// ============================================================================
// Latency Histogram Benchmarks
// ============================================================================

void BenchmarkLatencyHistogram() {
  std::cout << "\n[Latency Histogram Benchmarks]\n";
  std::cout << "------------------------------\n";

  LatencyHistogram hist;

  Benchmark::Run("Histogram record()", 1000000, [&]() { hist.record(42); });

  Benchmark::Run("Histogram getP99()", 100000, [&]() {
    volatile auto p = hist.getP99();
    (void)p;
  });
}

// ============================================================================
// Message Processing Simulation
// ============================================================================

void BenchmarkMessageProcessing() {
  std::cout << "\n[Message Processing Simulation]\n";
  std::cout << "-------------------------------\n";

  LockFreeQueue<std::string> queue(65536);
  ThreadPool pool(4);
  std::atomic<int> processed{0};
  LatencyHistogram latency;

  const int NUM_MESSAGES = 50000;

  std::cout << "  Simulating " << NUM_MESSAGES
            << " messages through full pipeline...\n";

  LARGE_INTEGER freq, start, end;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&start);

  // Producer: receive messages
  HANDLE producer = LaunchBenchThread([&]() {
    for (int i = 0; i < NUM_MESSAGES; ++i) {
      std::string msg = "Message#" + std::to_string(i);
      while (!queue.try_enqueue(std::move(msg))) {
        Sleep(0);
      }
    }
  });

  // Consumers: process messages
  std::atomic<bool> done{false};
  std::vector<HANDLE> consumers;
  for (int i = 0; i < 4; ++i) {
    consumers.push_back(LaunchBenchThread([&]() {
      std::string msg;
      while (!done || !queue.empty()) {
        if (queue.try_dequeue(msg)) {
          LARGE_INTEGER msg_start, msg_end;
          QueryPerformanceCounter(&msg_start);
          // Simulate some processing
          volatile int sum = 0;
          for (char c : msg)
            sum += c;
          QueryPerformanceCounter(&msg_end);
          double dur_us =
              ((double)(msg_end.QuadPart - msg_start.QuadPart) * 1000000.0) /
              freq.QuadPart;
          latency.record((uint64_t)dur_us);
          processed++;
        }
      }
    }));
  }

  WaitForSingleObject(producer, INFINITE);
  CloseHandle(producer);

  while (processed < NUM_MESSAGES)
    Sleep(1);
  done = true;

  for (HANDLE h : consumers) {
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }

  QueryPerformanceCounter(&end);
  double duration_ms =
      ((double)(end.QuadPart - start.QuadPart) * 1000.0) / freq.QuadPart;
  double msg_per_sec = (NUM_MESSAGES * 1000.0) / duration_ms;

  std::cout << "  Throughput:     " << std::fixed << std::setprecision(0)
            << msg_per_sec << " msg/sec\n";
  std::cout << "  P50 Latency:    " << latency.getP50() << " us\n";
  std::cout << "  P95 Latency:    " << latency.getP95() << " us\n";
  std::cout << "  P99 Latency:    " << latency.getP99() << " us\n";

  Benchmark::Result r;
  r.name = "Full message pipeline";
  r.ops_per_second = msg_per_sec;
  r.p50_latency_us = latency.getP50();
  r.p95_latency_us = latency.getP95();
  r.p99_latency_us = latency.getP99();
  Benchmark::results.push_back(r);
}

// ============================================================================
// Generate Results File
// ============================================================================

void GenerateResultsFile() {
  std::ofstream file("BENCHMARK_RESULTS.md");

  // Get system info
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  int num_cpus = sysinfo.dwNumberOfProcessors;

  file << "# Benchmark Results\n\n";
  file << "**Date:** " << __DATE__ << " " << __TIME__ << "\n";
  file << "**CPU Cores:** " << num_cpus << "\n";
  file << "**Compiler:** MinGW-w64 g++ (C++17, -O2)\n\n";

  file << "---\n\n";

  file << "## Summary\n\n";
  file << "| Benchmark | Throughput | P99 Latency |\n";
  file << "|-----------|------------|-------------|\n";

  for (const auto &r : Benchmark::results) {
    file << "| " << r.name << " | " << std::fixed << std::setprecision(0)
         << r.ops_per_second << " ops/sec | ";
    if (r.p99_latency_us > 0) {
      file << std::setprecision(2) << r.p99_latency_us << " us |\n";
    } else {
      file << "- |\n";
    }
  }

  file << "\n---\n\n";

  file << "## Lock-Free Queue Performance\n\n";
  file << "The lock-free MPMC queue uses:\n";
  file << "- **Cache-line padding** to prevent false sharing\n";
  file << "- **Sequence numbers** for ABA protection\n";
  file << "- **Power-of-2 sizing** for fast modulo via bitmask\n\n";

  file << "## Thread Pool Performance\n\n";
  file << "Custom thread pool with:\n";
  file << "- Fixed worker count to prevent thrashing\n";
  file << "- Condition variable for efficient waiting\n";
  file << "- Exception safety in task execution\n\n";

  file << "## Full Pipeline Performance\n\n";
  file << "End-to-end message processing simulation:\n";
  file << "1. Message received and enqueued\n";
  file << "2. Worker thread dequeues\n";
  file << "3. Message processed\n";
  file << "4. Latency recorded\n\n";

  file << "---\n\n";
  file << "*Generated by benchmark.exe*\n";

  file.close();
  std::cout << "\nResults written to BENCHMARK_RESULTS.md\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::cout << "\n";
  std::cout
      << "================================================================\n";
  std::cout << "  High-Performance Chat Server - Benchmark Suite\n";
  std::cout
      << "================================================================\n";

  BenchmarkLockFreeQueue();
  BenchmarkThreadPool();
  BenchmarkLatencyHistogram();
  BenchmarkMessageProcessing();

  std::cout
      << "\n================================================================\n";
  std::cout << "  Benchmark Complete!\n";
  std::cout
      << "================================================================\n";

  GenerateResultsFile();

  return 0;
}
