#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>

class WebSocketServer
{
public:
    WebSocketServer(int port);
    ~WebSocketServer();

    void run();
    void stop();
    bool is_running() const;

    // Thread-safe method for adding messages to the queue
    void queue_message(const std::string& message);
    // Immediate sending (used in the main thread)
    void broadcast_message_immediate(const std::string& message); 
    void broadcast_message(const std::string& message);
    // Function to get the number of connected clients
    int get_connected_clients_count() const;

private:
    int server_fd;
    int epoll_fd;
    int pipe_fd[2];                           // for notifications between threads
    std::unordered_map<int, bool> ws_connections; // fd -> is_websocket_handshake_complete

    // For multithreading
    std::mutex message_queue_mutex;
    std::queue<std::string> pending_messages;
    std::atomic<bool> running{true};

    std::string base64_encode(const unsigned char *data, size_t len);
    std::string generate_accept_key(const std::string &key);
    bool set_non_blocking(int fd);
    void add_to_epoll(int fd);
    void remove_from_epoll(int fd);
    std::string extract_websocket_key(const std::string &request);
    void send_websocket_handshake(int client_fd, const std::string &key);
    std::vector<uint8_t> create_websocket_frame(const std::string &message);
    std::string decode_websocket_frame(const std::vector<uint8_t> &data);
    void handle_new_connection();
    void handle_client_data(int client_fd);
    void handle_pending_messages();
    void close_connection(int client_fd);
};