# High-Performance Multi-Threaded Chat Server

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](.)
[![Tests](https://img.shields.io/badge/tests-16%2F16%20passed-brightgreen)](.)
[![Pipeline](https://img.shields.io/badge/Jenkins-Pipeline-blue?logo=jenkins&logoColor=white)](.)
[![License](https://img.shields.io/badge/license-MIT-blue)](.)

A **FAANG-level** high-performance chat server built for Windows using modern C++17, featuring lock-free data structures, asynchronous I/O, and comprehensive performance benchmarking.

![Chat Server Demo](demo_screenshot.png)

---

##  Performance Highlights

| Metric | Result |
|--------|--------|
| **Lock-Free Queue Throughput** | 117 Million ops/sec |
| **Message Pipeline** | 186,000 msg/sec |
| **P99 Latency** | < 1 microsecond |
| **Max Connections** | 10,000+ |
| **Unit Tests** | 16/16 Passed |

---

##  Features

- ✅ **10,000+ concurrent connections** - Scalable IOCP architecture
- ✅ **O(1) user lookups** - Hash-based reverse indexing
- ✅ **Real-time metrics** - P50/P95/P99 latency tracking
- ✅ **Lock-free data structures** - Zero-contention concurrency
- ✅ **Chat rooms** - Create, join, and manage rooms
- ✅ **Private whispers** - Secure direct messaging
- ✅ **Admin commands** - Mute, kick, ban moderation
- ✅ **Message history** - Persistent chat logs
- ✅ **CI/CD Pipeline** - Jenkins automated build, test & deploy

---

##  Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│               Chat Server Architecture                            │
├──────────────────────────────────────────────────────────────────┤
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                   │
│  │ Client 1 │    │ Client 2 │    │ Client N │  (10,000+)        │
│  └────┬─────┘    └────┬─────┘    └────┬─────┘                   │
│       └───────────────┼───────────────┘                          │
│                       ▼                                          │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │              IOCP Server (Async I/O)                        │  │
│  │  • Windows I/O Completion Ports                             │  │
│  │  • Lock-free message handling                               │  │
│  └───────────────────────┬────────────────────────────────────┘  │
│                          ▼                                       │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │              Thread Pool + Lock-Free Queue                  │  │
│  │  • N worker threads                                         │  │
│  │  • Cache-line padded queue                                  │  │
│  └───────────────────────┬────────────────────────────────────┘  │
│                          ▼                                       │
│  ┌──────────────┬──────────────────┬───────────────────┐        │
│  │  Chat Rooms  │  Message Store   │  Connection Mgr   │        │
│  └──────────────┴──────────────────┴───────────────────┘        │
└──────────────────────────────────────────────────────────────────┘
```

---

##  Quick Start

### Prerequisites
- Windows 10/11
- MinGW-w64 (g++ 6.3.0+) or Visual Studio

### Build
```bash
# Using MinGW-w64
build_mingw.bat

# Using Visual Studio
build.bat
```

### Run Server
```bash
build\server.exe 8080
```

### Connect Client
```bash
build\client.exe 127.0.0.1 8080
```

### Interactive Demo
```bash
DEMO_GUIDE.bat
```

---

##  Testing & Benchmarks

### Unit Tests
```bash
build\tests.exe
```
Validates: Lock-free queue, latency histogram, thread pool, performance metrics

### Performance Benchmarks
```bash
build\benchmark.exe
```
Generates `BENCHMARK_RESULTS.md` with throughput and latency metrics

### Load Testing
```bash
# Terminal 1: Start server
build\server.exe 8080

# Terminal 2: Run stress test (50 clients, 100 messages each)
build\stress_test.exe 127.0.0.1 8080 50 100
```

---

##  Commands

| Command | Description |
|---------|-------------|
| `#help` | Show all commands |
| `#rooms` | List chat rooms |
| `#join <room>` | Join a room |
| `#create <room>` | Create new room |
| `#online` | List online users |
| `#whisper <user> <msg>` | Private message |
| `#history [n]` | Show last n messages |
| `#stats` | Show server metrics |
| `#kick <user>` | (Admin) Kick user |
| `#mute <user> [sec]` | (Admin) Mute user |
| `#ban <user>` | (Admin) Ban IP |
| `#exit` | Disconnect |

---

##  Project Structure

```
├── Jenkinsfile             # Jenkins CI/CD Pipeline
├── CMakeLists.txt          # CMake build configuration
├── server.cpp              # Main server with O(1) lookups
├── client.cpp              # Chat client
├── tests.cpp               # Unit test suite (16 tests)
├── benchmark.cpp           # Performance benchmarks
├── stress_test.cpp         # Load testing tool
├── lock_free_queue.h       # Lock-free MPMC queue
├── object_pool.h           # Pre-allocated object pool
├── perf_metrics.h          # Performance metrics + histograms
├── thread_pool.h/cpp       # Fixed-size thread pool
├── iocp_server.h/cpp       # IOCP networking layer
├── sockutil.h/cpp          # Socket utilities
├── connection_manager.h/cpp # Rate limiting (10K connections)
├── chat_room.h/cpp         # Room management
├── message_store.h/cpp     # Message persistence
├── win32_compat.h          # Windows compatibility
├── build_mingw.bat         # MinGW build script
├── build.bat               # MSVC build script
├── DEMO_GUIDE.bat          # Interactive demo
├── BENCHMARK_RESULTS.md    # Generated benchmark results
└── README.md               # This file
```

---

##  Technical Deep Dive

### Lock-Free Queue
- **Cache-line padding** (64 bytes) prevents false sharing
- **Sequence numbers** provide ABA protection
- **Power-of-2 sizing** enables fast modulo via bitmask
- **Throughput:** 117M ops/sec single-threaded

### IOCP Networking
- Async I/O with Windows I/O Completion Ports
- Overlapped operations for non-blocking sends/receives
- Scales to 10,000+ concurrent connections

### Performance Metrics
- Lock-free atomic counters
- Logarithmic histogram buckets for latency
- Real-time P50/P95/P99 percentile queries

---

##  Benchmark Results

See [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) for detailed metrics.

| Benchmark | Throughput | P99 Latency |
|-----------|------------|-------------|
| Single-thread enqueue | 117M ops/sec | - |
| Single-thread dequeue | 108M ops/sec | - |
| Round-trip | 13.8M ops/sec | 0.10 µs |
| 4P/4C concurrent | 6.2M ops/sec | - |
| Task dispatch | 808K ops/sec | 6.90 µs |
| Full pipeline | 186K msg/sec | 1.00 µs |

---

## 🔄 CI/CD Pipeline

This project includes a **Jenkins Declarative Pipeline** ([`Jenkinsfile`](Jenkinsfile)) for automated build, test, and deployment.

### Pipeline Stages

```
┌──────────┐    ┌───────┐    ┌────────────┐    ┌────────────┐    ┌─────────────────┐    ┌─────────┐
│ Checkout │───▶│ Build │───▶│ Unit Tests │───▶│ Benchmarks │───▶│ Static Analysis │───▶│ Archive │
└──────────┘    └───────┘    └────────────┘    └────────────┘    └─────────────────┘    └─────────┘
```

| Stage | Description |
|-------|-------------|
| **Checkout** | Clean workspace + clone source from Git |
| **Build** | Compile all 5 targets via CMake or MinGW |
| **Unit Tests** | Run 16 tests, convert to JUnit XML, publish results |
| **Benchmarks** | Run performance benchmarks, archive `BENCHMARK_RESULTS.md` |
| **Static Analysis** | Code quality scan via `cppcheck` (if installed) |
| **Archive** | Fingerprint & store `.exe` binaries for download |

### Configurable Build Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `BUILD_TYPE` | Choice | `Release` | CMake build type (`Release` / `Debug` / `RelWithDebInfo`) |
| `RUN_BENCHMARKS` | Boolean | `true` | Enable/disable the benchmark stage |
| `USE_CMAKE` | Boolean | `true` | `true` = CMake build, `false` = `build_mingw.bat` |

### Jenkins Setup

1. **Prerequisites** on the Windows build agent:
   - MinGW-w64 (g++ 6.3.0+) or Visual Studio
   - CMake 3.15+ (if using CMake mode)
   - Git
   - *(Optional)* `cppcheck` for static analysis

2. **Create Pipeline Job:**
   - **Jenkins Dashboard** → **New Item** → **Pipeline**
   - **Pipeline** → Definition: **Pipeline script from SCM**
   - **SCM:** Git → Repository URL: `<your-repo-url>`
   - **Script Path:** `Jenkinsfile`
   - Click **Save** → **Build with Parameters**

3. **Install Required Plugins:**

   | Plugin | Purpose |
   |--------|---------|
   | Pipeline | Jenkinsfile support |
   | Git | Source checkout |
   | JUnit | Test result dashboards |
   | Timestamper | Console timestamps |
   | Workspace Cleanup | `cleanWs()` support |

4. **Notifications** *(optional)*:
   Uncomment the email/Slack blocks in `Jenkinsfile` `post` section to enable build notifications.

### Pipeline Features

- ⚡ **Parallel compilation** using all CPU cores (`-j %NUMBER_OF_PROCESSORS%`)
- 📊 **JUnit integration** — custom `[PASS]/[FAIL]` output auto-converted to JUnit XML
- 🔁 **Build log rotation** — keeps last 10 builds, artifacts for last 5
- 🔒 **Concurrent build prevention** — `disableConcurrentBuilds()`
- ⏱️ **30-minute timeout** — prevents stuck builds
- 📧 **Notification hooks** — Email & Slack (ready to enable)

---

##  License

MIT License - feel free to use for any purpose.

---

##  Author

**Pranav Kashyap**  
IIIT Dharwad

---

*Built with ❤️ and C++17*
