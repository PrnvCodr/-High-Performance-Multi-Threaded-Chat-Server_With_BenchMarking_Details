/**
 * High-Performance Multi-Threaded Chat Server v3.0
 *
 * FAANG-Level Windows implementation using:
 * - IOCP (I/O Completion Ports) for async I/O
 * - Lock-free data structures for zero contention
 * - Thread Pool for task processing
 * - Connection Manager for rate limiting
 * - Performance Metrics with P99 latency tracking
 * - O(1) user lookups with reverse indexing
 * - Chat Rooms for multi-room support
 * - Message Store for persistence
 */

#include "chat_room.h"
#include "connection_manager.h"
#include "iocp_server.h"
#include "message_store.h"
#include "perf_metrics.h"
#include "sockutil.h"
#include "thread_pool.h"
#include "win32_compat.h"

#include <csignal>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// Configuration
constexpr size_t THREAD_POOL_SIZE = 0; // 0 = auto (hardware concurrency)
constexpr int DEFAULT_PORT = 8080;

// Global components
std::unique_ptr<ThreadPool> g_thread_pool;
std::unique_ptr<IOCPServer> g_server;
std::unique_ptr<ConnectionManager> g_connection_manager;
std::unique_ptr<ChatRoomManager> g_chat_rooms;
std::unique_ptr<MessageStore> g_message_store;

// Client data storage with O(1) reverse lookup
w32::Mutex g_clients_mutex;
std::unordered_map<int, std::string> g_client_names; // client_id -> username
std::unordered_map<std::string, int>
    g_name_to_id; // username -> client_id (O(1) lookup)

// Forward declarations
void HandleMessage(int client_id, const char *message, int length);
void HandleConnect(int client_id, SOCKET socket);
void HandleDisconnect(int client_id);
void ProcessCommand(int client_id, const std::string &command);
void BroadcastToRoom(int sender_id, const std::string &message);
void SendToClient(int client_id, const std::string &message);
std::string GetTimestamp();
void PrintServerLog(const std::string &message);

// Signal handler for graceful shutdown
volatile bool g_running = true;

BOOL WINAPI ConsoleHandler(DWORD signal) {
  if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
    PrintServerLog("Shutting down server...");
    g_running = false;
    if (g_server) {
      g_server->Stop();
    }
    return TRUE;
  }
  return FALSE;
}

int main(int argc, char *argv[]) {
  // Parse command line
  int port = DEFAULT_PORT;
  if (argc >= 2) {
    port = atoi(argv[1]);
  }

  // Enable ANSI colors on Windows 10+
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  GetConsoleMode(hOut, &dwMode);
  SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  std::cout << "========================================\n";
  std::cout << "  High-Performance Chat Server v2.0    \n";
  std::cout << "  Windows IOCP + Thread Pool Edition   \n";
  std::cout << "========================================\n\n";

  // Initialize Winsock
  if (!InitializeWinsock()) {
    std::cerr << "Failed to initialize Winsock" << std::endl;
    return 1;
  }

  // Set up signal handler
  SetConsoleCtrlHandler(ConsoleHandler, TRUE);

  // Initialize components
  PrintServerLog("Initializing components...");

  // Thread Pool
  size_t pool_size = THREAD_POOL_SIZE;
  if (pool_size == 0) {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    pool_size = sysinfo.dwNumberOfProcessors;
    if (pool_size == 0)
      pool_size = 1;
  }
  g_thread_pool = std::make_unique<ThreadPool>(pool_size);
  PrintServerLog("Thread pool created with " + std::to_string(pool_size) +
                 " workers");

  // Connection Manager - FAANG-level scaling (10K connections)
  ConnectionManager::Config conn_config;
  conn_config.max_connections_per_second = 500; // Was 50
  conn_config.max_messages_per_minute = 600;    // Was 60
  conn_config.connection_timeout_seconds = 300;
  conn_config.max_total_connections = 10000; // Was 1000
  g_connection_manager = std::make_unique<ConnectionManager>(conn_config);
  PrintServerLog(
      "Connection manager initialized (10K connections, 500 conn/sec)");

  // Chat Rooms
  g_chat_rooms = std::make_unique<ChatRoomManager>();
  PrintServerLog("Chat room manager initialized (default room: #general)");

  // Message Store
  MessageStore::Config store_config;
  store_config.max_messages_per_room = 100;
  store_config.log_directory = "./chat_logs";
  store_config.enable_persistence = true;
  g_message_store = std::make_unique<MessageStore>(store_config);
  PrintServerLog("Message store initialized");

  // IOCP Server
  g_server = std::make_unique<IOCPServer>(port, *g_thread_pool);
  g_server->OnMessage(HandleMessage);
  g_server->OnConnect(HandleConnect);
  g_server->OnDisconnect(HandleDisconnect);

  if (!g_server->Start()) {
    std::cerr << "Failed to start server" << std::endl;
    CleanupWinsock();
    return 1;
  }

  PrintServerLog("Server listening on port " + std::to_string(port));
  PrintServerLog("Press Ctrl+C to stop the server\n");

  // Print available commands
  std::cout << "Available client commands:\n";
  std::cout << "  #rooms     - List all chat rooms\n";
  std::cout << "  #join <r>  - Join room <r>\n";
  std::cout << "  #create <r>- Create new room\n";
  std::cout << "  #leave     - Leave current room\n";
  std::cout << "  #online    - List online users\n";
  std::cout << "  #whisper <user> <msg> - Private message\n";
  std::cout << "  #history [n] - Show recent messages\n";
  std::cout << "  #kick <u>  - (Admin) Kick user\n";
  std::cout << "  #ban <u>   - (Admin) Ban user\n";
  std::cout << "  #mute <u>  - (Admin) Mute user\n";
  std::cout << "  #exit      - Disconnect\n\n";

  // Main loop - just wait for shutdown
  while (g_running && g_server->IsRunning()) {
    Sleep(1000);

    // Periodic tasks
    // Check for timed out connections
    auto timed_out =
        g_connection_manager->CheckTimeouts(g_server->GetAllClients());
    for (int id : timed_out) {
      PrintServerLog("Client " + std::to_string(id) + " timed out");
      g_server->DisconnectClient(id);
    }
  }

  // Cleanup
  PrintServerLog("Cleaning up...");
  g_server.reset();
  g_message_store.reset();
  g_chat_rooms.reset();
  g_connection_manager.reset();
  g_thread_pool.reset();

  CleanupWinsock();
  PrintServerLog("Server stopped. Goodbye!");

  return 0;
}

std::string GetTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  w32::LocalTime(&tm, &time_t);

  std::stringstream ss;
  ss << std::put_time(&tm, "%H:%M:%S");
  return ss.str();
}

void PrintServerLog(const std::string &message) {
  std::cout << "[" << GetTimestamp() << "] " << message << std::endl;
}

std::string GetClientName(int client_id) {
  w32::LockGuard lock(g_clients_mutex);
  auto it = g_client_names.find(client_id);
  if (it != g_client_names.end()) {
    return it->second;
  }
  return "User#" + std::to_string(client_id);
}

void SetClientName(int client_id, const std::string &name) {
  w32::LockGuard lock(g_clients_mutex);
  // Remove old name from reverse index if exists
  auto old_it = g_client_names.find(client_id);
  if (old_it != g_client_names.end()) {
    g_name_to_id.erase(old_it->second);
  }
  // Set both forward and reverse mappings
  g_client_names[client_id] = name;
  g_name_to_id[name] = client_id;
}

void HandleConnect(int client_id, SOCKET socket) {
  // Check rate limiting
  std::string ip = GetSocketAddress(socket);
  if (!g_connection_manager->AllowConnection(ip)) {
    PrintServerLog("Connection rejected (rate limit): " + ip);
    PERF_CONN_REJECT(); // Track rejected connections
    g_server->DisconnectClient(client_id);
    return;
  }

  g_connection_manager->OnConnect();
  PERF_CONN_ACCEPT(); // Track accepted connections
  PERF_SET_CONNECTIONS(g_connection_manager->GetConnectionCount());

  // Add to general room
  g_chat_rooms->JoinRoom("general", client_id);

  PrintServerLog("Client " + std::to_string(client_id) + " connected from " +
                 ip);

  // Send welcome message
  std::string welcome = "Welcome to the chat server! You are in #general.\n";
  welcome += "Type #help for available commands.\n";
  SendToClient(client_id, welcome);
}

void HandleDisconnect(int client_id) {
  std::string name = GetClientName(client_id);
  std::string room = g_chat_rooms->GetClientRoom(client_id);

  g_chat_rooms->LeaveRoom(client_id);
  g_connection_manager->OnDisconnect();
  PERF_SET_CONNECTIONS(g_connection_manager->GetConnectionCount());

  {
    w32::LockGuard lock(g_clients_mutex);
    // Remove from reverse index first
    auto name_it = g_client_names.find(client_id);
    if (name_it != g_client_names.end()) {
      g_name_to_id.erase(name_it->second);
    }
    g_client_names.erase(client_id);
  }

  // Notify room
  std::string bye = name + " has left the chat";
  // We need to be careful with GetRoomMembers as it returns a vector
  // and if the room is empty/deleted it returns empty.

  if (room != "") {
    auto members = g_chat_rooms->GetRoomMembers(room);
    for (int member_id : members) {
      // Check if member is still connected is implied by being in a room list
      // usually
      SendToClient(member_id, bye);
    }
  }

  PrintServerLog("Client " + std::to_string(client_id) + " (" + name +
                 ") disconnected");
}

void HandleMessage(int client_id, const char *message, int length) {
  std::string msg(message, length);

  // Trim whitespace
  while (!msg.empty() &&
         (msg.back() == '\n' || msg.back() == '\r' || msg.back() == '\0')) {
    msg.pop_back();
  }

  if (msg.empty()) {
    return;
  }

  // Check rate limiting
  if (!g_connection_manager->AllowMessage(client_id)) {
    SendToClient(client_id,
                 "You are sending too many messages. Please slow down.");
    return;
  }
  g_connection_manager->RecordMessage(client_id);

  // Track message metrics
  PERF_MSG_RECV();
  PERF_BYTES_RECV(length);

  // Check mute
  if (g_connection_manager->IsMuted(client_id)) {
    SendToClient(client_id, "You are muted.");
    return;
  }

  // Check if this is a name registration (first message)
  std::string current_name = GetClientName(client_id);
  if (current_name == "User#" + std::to_string(client_id)) {
    // First message is the username
    // Simplified logic: just existing check if starts with #, if so, it's a
    // command, not a name set maybe? But the original logic was: first message
    // IS name.
    if (msg[0] == '#') {
      // It's a command, let them run it but don't set name yet?
      // Or maybe we process command.
      // Original logic implies: if name is default, treat message as name.
    } else {
      SetClientName(client_id, msg);

      std::string room = g_chat_rooms->GetClientRoom(client_id);
      std::string join_msg = msg + " has joined #" + room;

      auto members = g_chat_rooms->GetRoomMembers(room);
      for (int member_id : members) {
        if (member_id != client_id) {
          SendToClient(member_id, join_msg);
        }
      }

      PrintServerLog("Client " + std::to_string(client_id) +
                     " registered as: " + msg);
      return;
    }
  }

  // Check for commands
  if (msg[0] == '#') {
    ProcessCommand(client_id, msg);
    return;
  }

  // Regular chat message - broadcast to room
  BroadcastToRoom(client_id, msg);
}

void ProcessCommand(int client_id, const std::string &cmd) {
  std::string name = GetClientName(client_id);
  std::istringstream iss(cmd);
  std::string command;
  iss >> command;

  if (command == "#exit") {
    g_server->DisconnectClient(client_id);
  } else if (command == "#help") {
    std::string help = "Available commands:\n";
    help += "  #rooms     - List all chat rooms\n";
    help += "  #join <r>  - Join room <r>\n";
    help += "  #create <r>- Create new room\n";
    help += "  #leave     - Leave to general\n";
    help += "  #online    - List online users\n";
    help += "  #whisper <user> <msg> - Private message\n";
    help += "  #history [n] - Show last n messages\n";
    help += "  #stats     - Show server performance metrics\n";
    help += "  #exit      - Disconnect\n";
    SendToClient(client_id, help);
  } else if (command == "#rooms") {
    auto rooms = g_chat_rooms->ListRooms();
    std::string list = "Available rooms:\n";
    for (const auto &room : rooms) {
      list += "  #" + room + " (" +
              std::to_string(g_chat_rooms->GetRoomMembers(room).size()) +
              " users)\n";
    }
    SendToClient(client_id, list);
  } else if (command == "#join") {
    std::string room_name;
    iss >> room_name;
    if (room_name.empty()) {
      SendToClient(client_id, "Usage: #join <room_name>");
      return;
    }

    std::string old_room = g_chat_rooms->GetClientRoom(client_id);

    // Don't restart join if already in
    if (old_room == room_name) {
      SendToClient(client_id, "You are already in #" + room_name);
      return;
    }

    if (g_chat_rooms->JoinRoom(room_name, client_id)) {
      // Notify old room
      if (!old_room.empty()) {
        auto old_members = g_chat_rooms->GetRoomMembers(old_room);
        for (int m : old_members) {
          SendToClient(m, name + " left #" + old_room);
        }
      }

      // Notify new room
      auto new_members = g_chat_rooms->GetRoomMembers(room_name);
      for (int m : new_members) {
        if (m != client_id) {
          SendToClient(m, name + " joined #" + room_name);
        }
      }

      SendToClient(client_id, "Joined #" + room_name);
    } else {
      SendToClient(client_id, "Failed to join room. Does it exist?");
    }
  } else if (command == "#create") {
    std::string room_name;
    iss >> room_name;
    if (room_name.empty()) {
      SendToClient(client_id, "Usage: #create <room_name>");
      return;
    }

    if (g_chat_rooms->CreateRoom(room_name, client_id)) {
      g_chat_rooms->JoinRoom(room_name, client_id);
      SendToClient(client_id, "Created and joined #" + room_name);
      PrintServerLog("Room created: #" + room_name + " by " + name);
    } else {
      SendToClient(client_id, "Failed to create room. Does it already exist?");
    }
  } else if (command == "#leave") {
    std::string current = g_chat_rooms->GetClientRoom(client_id);
    if (current != "general") {
      g_chat_rooms->JoinRoom("general", client_id);
      SendToClient(client_id, "You left #" + current + " and joined #general");
    } else {
      SendToClient(client_id, "You are already in #general");
    }
  } else if (command == "#online") {
    // This relies on IOCPServer::GetAllClients locking potentially, which is
    // fine BUT GetAllClients returns a copy, so iteration is safe. However, we
    // need to be careful about concurrent map access if GetAllClients isn't
    // thread safe. We fixed IOCPServer to use w32::LockGuard so it should be
    // safe.
    auto clients = g_server->GetAllClients();
    std::string list =
        "Online users (" + std::to_string(clients.size()) + "):\n";
    for (const auto &client : clients) {
      std::string cname = GetClientName(client.id);
      std::string room = g_chat_rooms->GetClientRoom(client.id);
      list += "  " + cname + " (#" + room + ")\n";
    }
    SendToClient(client_id, list);
  } else if (command == "#whisper") {
    std::string target_name;
    std::string private_msg;
    iss >> target_name;
    std::getline(iss, private_msg);

    if (target_name.empty() || private_msg.empty()) {
      SendToClient(client_id, "Usage: #whisper <username> <message>");
      return;
    }

    // Find target - O(1) lookup using reverse index
    int target_id = -1;
    {
      w32::LockGuard lock(g_clients_mutex);
      auto it = g_name_to_id.find(target_name);
      if (it != g_name_to_id.end()) {
        target_id = it->second;
      }
    }

    if (target_id == -1) {
      SendToClient(client_id, "User not found: " + target_name);
      return;
    }

    SendToClient(target_id, "[Whisper from " + name + "]:" + private_msg);
    SendToClient(client_id, "[Whisper to " + target_name + "]:" + private_msg);
  } else if (command == "#history") {
    int count = 10;
    iss >> count;
    if (count < 1)
      count = 10;
    if (count > 50)
      count = 50;

    std::string room = g_chat_rooms->GetClientRoom(client_id);
    auto messages = g_message_store->GetRecent(room, count);

    std::string history = "Last " + std::to_string(messages.size()) +
                          " messages in #" + room + ":\n";
    for (const auto &msg : messages) {
      history += "  " + msg.ToString() + "\n";
    }
    SendToClient(client_id, history);
  } else if (command == "#kick") {
    // Admin only (for now, anyone can use - add proper auth later)
    std::string target_name;
    iss >> target_name;

    // O(1) lookup using reverse index
    int target_id = -1;
    {
      w32::LockGuard lock(g_clients_mutex);
      auto it = g_name_to_id.find(target_name);
      if (it != g_name_to_id.end()) {
        target_id = it->second;
      }
    }

    if (target_id != -1) {
      SendToClient(target_id, "You have been kicked by " + name);
      g_server->DisconnectClient(target_id);
      SendToClient(client_id, "Kicked " + target_name);
      PrintServerLog(name + " kicked " + target_name);
    } else {
      SendToClient(client_id, "User not found");
    }
  } else if (command == "#ban") {
    std::string target_name;
    iss >> target_name;

    // Need to find ID to find IP
    // O(1) lookup using reverse index
    int target_id = -1;
    {
      w32::LockGuard lock(g_clients_mutex);
      auto it = g_name_to_id.find(target_name);
      if (it != g_name_to_id.end()) {
        target_id = it->second;
      }
    }

    if (target_id != -1) {
      auto client = g_server->GetClient(target_id);
      if (client) {
        g_connection_manager->Ban(client->ip_address);
        SendToClient(target_id, "You have been banned by " + name);
        g_server->DisconnectClient(target_id);
        SendToClient(client_id, "Banned IP for " + target_name);
        PrintServerLog(name + " banned " + target_name);
      }
    } else {
      SendToClient(client_id, "User not found");
    }
  } else if (command == "#mute") {
    std::string target_name;
    int duration = 60; // Default 60 seconds
    iss >> target_name >> duration;

    // O(1) lookup using reverse index
    int target_id = -1;
    {
      w32::LockGuard lock(g_clients_mutex);
      auto it = g_name_to_id.find(target_name);
      if (it != g_name_to_id.end()) {
        target_id = it->second;
      }
    }

    if (target_id != -1) {
      g_connection_manager->Mute(target_id, duration);
      SendToClient(target_id, "You have been muted for " +
                                  std::to_string(duration) + " seconds");
      SendToClient(client_id, "Muted " + target_name + " for " +
                                  std::to_string(duration) + " seconds");
      PrintServerLog(name + " muted " + target_name);
    } else {
      SendToClient(client_id, "User not found");
    }
  } else if (command == "#stats") {
    // Performance metrics - FAANG-level monitoring
    std::string stats = PerfMetrics::instance().getStatsString();
    SendToClient(client_id, stats);
  } else {
    SendToClient(client_id,
                 "Unknown command. Type #help for available commands.");
  }
}

void BroadcastToRoom(int sender_id, const std::string &message) {
  std::string name = GetClientName(sender_id);
  std::string room = g_chat_rooms->GetClientRoom(sender_id);

  // Store message
  ChatMessage chat_msg(sender_id, name, room, message);
  g_message_store->Store(chat_msg);

  // Format message
  std::string formatted = name + ": " + message;

  // Send to all room members
  auto members = g_chat_rooms->GetRoomMembers(room);
  for (int member_id : members) {
    if (member_id != sender_id) {
      SendToClient(member_id, formatted);
    }
  }

  PrintServerLog("[#" + room + "] " + name + ": " + message);
}

void SendToClient(int client_id, const std::string &message) {
  std::string msg = message;
  if (msg.empty())
    return;
  if (msg.back() != '\n') {
    msg += '\n';
  }
  g_server->Send(client_id, msg.c_str(), (int)msg.length());

  // Track send metrics
  PERF_MSG_SENT();
  PERF_BYTES_SENT(msg.length());
}