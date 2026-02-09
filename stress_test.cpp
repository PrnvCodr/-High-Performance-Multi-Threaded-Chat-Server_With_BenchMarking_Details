/**
 * Stress Test Client for High-Performance Chat Server
 *
 * Simulates multiple concurrent clients connecting to the server.
 * Compatible with MinGW 6.3.0+
 *
 * Build: g++ -std=c++17 -O2 -o build/stress_test.exe stress_test.cpp -lws2_32
 * Run: stress_test.exe <server_ip> <port> <num_clients> <msgs_per_client>
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

// ============================================================================
// Statistics
// ============================================================================

std::atomic<int> g_connections_successful{0};
std::atomic<int> g_connections_failed{0};
std::atomic<int> g_messages_sent{0};
std::atomic<int> g_messages_received{0};
std::atomic<int> g_errors{0};

std::vector<double> g_connection_times;
std::vector<double> g_send_latencies;
CRITICAL_SECTION g_stats_lock;

// ============================================================================
// Helper: Thread launcher
// ============================================================================
struct ClientArgs {
  std::string server_ip;
  int port;
  int client_id;
  int messages_to_send;
};

LARGE_INTEGER g_perfFreq;

DWORD WINAPI ClientThread(LPVOID arg) {
  ClientArgs *args = (ClientArgs *)arg;

  SOCKET sock = INVALID_SOCKET;
  struct sockaddr_in server_addr;

  // Create socket
  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    g_connections_failed++;
    g_errors++;
    delete args;
    return 1;
  }

  // Prepare server address
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(args->port);
  server_addr.sin_addr.s_addr = inet_addr(args->server_ip.c_str());

  // Measure connection time
  LARGE_INTEGER conn_start, conn_end;
  QueryPerformanceCounter(&conn_start);

  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      SOCKET_ERROR) {
    closesocket(sock);
    g_connections_failed++;
    g_errors++;
    delete args;
    return 1;
  }

  QueryPerformanceCounter(&conn_end);
  double conn_time =
      ((double)(conn_end.QuadPart - conn_start.QuadPart) * 1000.0) /
      g_perfFreq.QuadPart;

  EnterCriticalSection(&g_stats_lock);
  g_connection_times.push_back(conn_time);
  LeaveCriticalSection(&g_stats_lock);

  g_connections_successful++;

  // Send username
  std::ostringstream username_ss;
  username_ss << "stress_client_" << args->client_id;
  std::string username = username_ss.str() + "\n";
  send(sock, username.c_str(), (int)username.length(), 0);

  // Receive welcome message
  char buffer[1024];
  recv(sock, buffer, sizeof(buffer) - 1, 0);

  // Send messages
  for (int i = 0; i < args->messages_to_send; ++i) {
    std::ostringstream msg_ss;
    msg_ss << "Test message " << i << " from client " << args->client_id
           << "\n";
    std::string msg = msg_ss.str();

    LARGE_INTEGER send_start, send_end;
    QueryPerformanceCounter(&send_start);

    int result = send(sock, msg.c_str(), (int)msg.length(), 0);

    QueryPerformanceCounter(&send_end);

    if (result == SOCKET_ERROR) {
      g_errors++;
      break;
    }

    g_messages_sent++;

    double send_latency =
        ((double)(send_end.QuadPart - send_start.QuadPart) * 1000000.0) /
        g_perfFreq.QuadPart;

    EnterCriticalSection(&g_stats_lock);
    g_send_latencies.push_back(send_latency);
    LeaveCriticalSection(&g_stats_lock);

    // Small delay between messages
    Sleep(10);
  }

  // Cleanup
  closesocket(sock);
  delete args;
  return 0;
}

// ============================================================================
// Calculate Percentile
// ============================================================================
double Percentile(std::vector<double> &sorted_data, double p) {
  if (sorted_data.empty())
    return 0.0;
  size_t idx = (size_t)(sorted_data.size() * p);
  if (idx >= sorted_data.size())
    idx = sorted_data.size() - 1;
  return sorted_data[idx];
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
  if (argc < 5) {
    std::cout << "Usage: stress_test.exe <server_ip> <port> <num_clients> "
                 "<msgs_per_client>\n";
    std::cout << "Example: stress_test.exe 127.0.0.1 8080 50 100\n";
    return 1;
  }

  std::string server_ip = argv[1];
  int port = atoi(argv[2]);
  int num_clients = atoi(argv[3]);
  int msgs_per_client = atoi(argv[4]);

  // Initialize Winsock
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }

  // Initialize performance counter
  QueryPerformanceFrequency(&g_perfFreq);
  InitializeCriticalSection(&g_stats_lock);

  std::cout << "\n";
  std::cout
      << "================================================================\n";
  std::cout << "  Chat Server Stress Test\n";
  std::cout
      << "================================================================\n\n";
  std::cout << "  Server: " << server_ip << ":" << port << "\n";
  std::cout << "  Clients: " << num_clients << "\n";
  std::cout << "  Messages per client: " << msgs_per_client << "\n";
  std::cout << "  Total messages: " << num_clients * msgs_per_client << "\n\n";
  std::cout << "Starting test...\n\n";

  LARGE_INTEGER test_start, test_end;
  QueryPerformanceCounter(&test_start);

  // Launch client threads
  std::vector<HANDLE> threads;
  for (int i = 0; i < num_clients; ++i) {
    ClientArgs *args = new ClientArgs;
    args->server_ip = server_ip;
    args->port = port;
    args->client_id = i;
    args->messages_to_send = msgs_per_client;

    HANDLE h = CreateThread(NULL, 0, ClientThread, args, 0, NULL);
    if (h) {
      threads.push_back(h);
    }

    // Stagger connections
    if ((i + 1) % 10 == 0) {
      Sleep(100);
    }
  }

  // Wait for all threads
  for (HANDLE h : threads) {
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }

  QueryPerformanceCounter(&test_end);
  double total_time_sec =
      ((double)(test_end.QuadPart - test_start.QuadPart)) / g_perfFreq.QuadPart;

  // Calculate statistics
  double avg_conn_time = 0.0;
  double p99_conn_time = 0.0;
  if (!g_connection_times.empty()) {
    double sum = 0.0;
    for (double t : g_connection_times)
      sum += t;
    avg_conn_time = sum / g_connection_times.size();
    std::sort(g_connection_times.begin(), g_connection_times.end());
    p99_conn_time = Percentile(g_connection_times, 0.99);
  }

  double avg_send_latency = 0.0;
  double p50_send_latency = 0.0;
  double p95_send_latency = 0.0;
  double p99_send_latency = 0.0;
  if (!g_send_latencies.empty()) {
    double sum = 0.0;
    for (double t : g_send_latencies)
      sum += t;
    avg_send_latency = sum / g_send_latencies.size();
    std::sort(g_send_latencies.begin(), g_send_latencies.end());
    p50_send_latency = Percentile(g_send_latencies, 0.50);
    p95_send_latency = Percentile(g_send_latencies, 0.95);
    p99_send_latency = Percentile(g_send_latencies, 0.99);
  }

  double msgs_per_sec = g_messages_sent.load() / total_time_sec;

  // Print results
  std::cout
      << "================================================================\n";
  std::cout << "  Results\n";
  std::cout
      << "================================================================\n\n";

  std::cout << "Connection Statistics:\n";
  std::cout << "  Successful:        " << g_connections_successful.load()
            << "\n";
  std::cout << "  Failed:            " << g_connections_failed.load() << "\n";
  std::cout << "  Avg Connect Time:  " << std::fixed << std::setprecision(2)
            << avg_conn_time << " ms\n";
  std::cout << "  P99 Connect Time:  " << p99_conn_time << " ms\n\n";

  std::cout << "Message Statistics:\n";
  std::cout << "  Sent:              " << g_messages_sent.load() << "\n";
  std::cout << "  Errors:            " << g_errors.load() << "\n";
  std::cout << "  Throughput:        " << std::setprecision(0) << msgs_per_sec
            << " msgs/sec\n";
  std::cout << "  Avg Send Latency:  " << std::setprecision(2)
            << avg_send_latency << " us\n";
  std::cout << "  P50 Send Latency:  " << p50_send_latency << " us\n";
  std::cout << "  P95 Send Latency:  " << p95_send_latency << " us\n";
  std::cout << "  P99 Send Latency:  " << p99_send_latency << " us\n\n";

  std::cout << "Total Test Time: " << std::setprecision(2) << total_time_sec
            << " seconds\n";
  std::cout
      << "================================================================\n";

  DeleteCriticalSection(&g_stats_lock);
  WSACleanup();

  return g_errors > 0 ? 1 : 0;
}
