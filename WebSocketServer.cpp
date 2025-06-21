#include "WebSocketServer.h"

#include <iostream>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <ctime>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <sstream>

const std::string WS_MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 4096;

WebSocketServer::WebSocketServer(int port)
{
    // Create pipe for inter-thread communication
    if (pipe(pipe_fd) == -1)
    {
        throw std::runtime_error("Failed to create pipe");
    }

    // Make pipe non-blocking
    if (!set_non_blocking(pipe_fd[0]) || !set_non_blocking(pipe_fd[1]))
    {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        throw std::runtime_error("Failed to set pipe non-blocking");
    }

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        throw std::runtime_error("Failed to create socket");
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        close(server_fd);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        throw std::runtime_error("Failed to set socket options");
    }

    // Set non-blocking
    if (!set_non_blocking(server_fd))
    {
        close(server_fd);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        throw std::runtime_error("Failed to set non-blocking");
    }

    // Bind socket
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        close(server_fd);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        throw std::runtime_error("Failed to bind socket");
    }

    // Listen
    if (listen(server_fd, SOMAXCONN) == -1)
    {
        close(server_fd);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        throw std::runtime_error("Failed to listen");
    }

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        close(server_fd);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        throw std::runtime_error("Failed to create epoll");
    }

    // Add server socket to epoll
    add_to_epoll(server_fd);

    // Add pipe read end to epoll
    add_to_epoll(pipe_fd[0]);

    std::cout << "WebSocket server listening on port " << port << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  Type message to broadcast to all clients" << std::endl;
    std::cout << "  /quit - exit server" << std::endl;
    std::cout << "  /clients - show connected clients count" << std::endl;
    std::cout << "  /time - broadcast current time" << std::endl;
}

WebSocketServer::~WebSocketServer()
{
    running = false;
    if (server_fd != -1)
        close(server_fd);
    if (epoll_fd != -1)
        close(epoll_fd);
    if (pipe_fd[0] != -1)
        close(pipe_fd[0]);
    if (pipe_fd[1] != -1)
        close(pipe_fd[1]);
}

void WebSocketServer::run()
{
    struct epoll_event events[MAX_EVENTS];

    while (running)
    {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 1 second timeout
        if (num_events == -1)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < num_events; i++)
        {
            int fd = events[i].data.fd;

            if (fd == server_fd)
            {
                // New connection
                handle_new_connection();
            }
            else if (fd == pipe_fd[0])
            {
                // Handle pending messages
                handle_pending_messages();
            }
            else
            {
                // Client data
                handle_client_data(fd);
            }
        }
    }
}

void WebSocketServer::stop()
{
    running = false;
    // Notify epoll to wake up
    char signal = 1;
    write(pipe_fd[1], &signal, 1);
}

bool WebSocketServer::is_running() const
{
    return running;
}

void WebSocketServer::queue_message(const std::string &message)
{
    {
        std::lock_guard<std::mutex> lock(message_queue_mutex);
        pending_messages.push(message);
    }

    // Notify the main thread via pipe
    char signal = 1;
    write(pipe_fd[1], &signal, 1);
}

void WebSocketServer::broadcast_message_immediate(const std::string &message)
{
    auto frame = create_websocket_frame(message);
    int sent_count = 0;

    for (auto &conn : ws_connections)
    {
        if (conn.second)
        { // Only to completed WebSocket connections
            ssize_t result = send(conn.first, frame.data(), frame.size(), 0);
            if (result > 0)
            {
                sent_count++;
            }
        }
    }

    std::cout << "Broadcasted message to " << sent_count << " clients: " << message << std::endl;
}

void WebSocketServer::broadcast_message(const std::string &message)
{
    queue_message(message);
}

int WebSocketServer::get_connected_clients_count() const
{
    int count = 0;
    for (const auto &conn : ws_connections)
    {
        if (conn.second)
            count++;
    }
    return count;
}

std::string WebSocketServer::base64_encode(const unsigned char *data, size_t len)
{
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data, len);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    return result;
}

std::string WebSocketServer::generate_accept_key(const std::string &key)
{
    std::string combined = key + WS_MAGIC_STRING;
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(combined.c_str()), combined.length(), hash);
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

bool WebSocketServer::set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

void WebSocketServer::add_to_epoll(int fd)
{
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Edge-triggered
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        perror("epoll_ctl: add");
    }
}

void WebSocketServer::remove_from_epoll(int fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1)
    {
        perror("epoll_ctl: del");
    }
}

std::string WebSocketServer::extract_websocket_key(const std::string &request)
{
    std::istringstream iss(request);
    std::string line;

    while (std::getline(iss, line))
    {
        if (line.find("Sec-WebSocket-Key:") == 0)
        {
            size_t pos = line.find(':');
            if (pos != std::string::npos)
            {
                std::string key = line.substr(pos + 1);
                // Trim whitespace
                key.erase(0, key.find_first_not_of(" \t\r\n"));
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                return key;
            }
        }
    }
    return "";
}

void WebSocketServer::send_websocket_handshake(int client_fd, const std::string &key)
{
    std::string accept_key = generate_accept_key(key);
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        accept_key + "\r\n\r\n";

    send(client_fd, response.c_str(), response.length(), 0);
    ws_connections[client_fd] = true;
    std::cout << "WebSocket handshake completed for client " << client_fd << std::endl;
}

std::vector<uint8_t> WebSocketServer::create_websocket_frame(const std::string &message)
{
    std::vector<uint8_t> frame;
    size_t msg_len = message.length();

    // First byte: FIN=1, opcode=1 (text frame)
    frame.push_back(0x81);

    // Payload length
    if (msg_len < 126)
    {
        frame.push_back(static_cast<uint8_t>(msg_len));
    }
    else if (msg_len < 65536)
    {
        frame.push_back(126);
        frame.push_back((msg_len >> 8) & 0xFF);
        frame.push_back(msg_len & 0xFF);
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--)
        {
            frame.push_back((msg_len >> (i * 8)) & 0xFF);
        }
    }

    // Payload data
    frame.insert(frame.end(), message.begin(), message.end());
    return frame;
}

std::string WebSocketServer::decode_websocket_frame(const std::vector<uint8_t> &data)
{
    if (data.size() < 2)
        return "";

    bool fin = (data[0] & 0x80) != 0;
    uint8_t opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    uint64_t payload_len = data[1] & 0x7F;

    size_t header_len = 2;

    if (payload_len == 126)
    {
        if (data.size() < 4)
            return "";
        payload_len = (data[2] << 8) | data[3];
        header_len = 4;
    }
    else if (payload_len == 127)
    {
        if (data.size() < 10)
            return "";
        payload_len = 0;
        for (int i = 0; i < 8; i++)
        {
            payload_len = (payload_len << 8) | data[2 + i];
        }
        header_len = 10;
    }

    if (masked)
    {
        header_len += 4;
    }

    if (data.size() < header_len + payload_len)
        return "";

    std::string payload;
    if (masked)
    {
        uint8_t mask[4] = {data[header_len - 4], data[header_len - 3], data[header_len - 2], data[header_len - 1]};
        for (uint64_t i = 0; i < payload_len; i++)
        {
            payload += static_cast<char>(data[header_len + i] ^ mask[i % 4]);
        }
    }
    else
    {
        payload.assign(data.begin() + header_len, data.begin() + header_len + payload_len);
    }

    return payload;
}

void WebSocketServer::handle_new_connection()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd == -1)
    {
        perror("accept");
        return;
    }

    if (!set_non_blocking(client_fd))
    {
        perror("set_non_blocking");
        close(client_fd);
        return;
    }

    add_to_epoll(client_fd);
    ws_connections[client_fd] = false; // Not yet WebSocket

    std::cout << "New connection: " << client_fd << std::endl;
}

void WebSocketServer::handle_client_data(int client_fd)
{
    std::vector<uint8_t> buffer(BUFFER_SIZE);
    ssize_t bytes_read = recv(client_fd, buffer.data(), BUFFER_SIZE, 0);

    if (bytes_read <= 0)
    {
        if (bytes_read == 0)
        {
            std::cout << "Client " << client_fd << " disconnected" << std::endl;
        }
        else
        {
            perror("recv");
        }
        close_connection(client_fd);
        return;
    }

    buffer.resize(bytes_read);

    if (!ws_connections[client_fd])
    {
        // Handle HTTP upgrade request
        std::string request(buffer.begin(), buffer.end());
        std::string ws_key = extract_websocket_key(request);

        if (!ws_key.empty())
        {
            send_websocket_handshake(client_fd, ws_key);
        }
        else
        {
            close_connection(client_fd);
        }
    }
    else
    {
        // Handle WebSocket frame
        std::string message = decode_websocket_frame(buffer);
        if (!message.empty())
        {
            std::cout << "Received from client " << client_fd << ": " << message << std::endl;

            // Echo the message back
            std::string response = "Echo: " + message;
            broadcast_message(response);
        }
    }
}

void WebSocketServer::handle_pending_messages()
{
    // Clear the pipe
    char dummy;
    while (read(pipe_fd[0], &dummy, 1) > 0)
        ;

    std::lock_guard<std::mutex> lock(message_queue_mutex);
    while (!pending_messages.empty())
    {
        std::string message = pending_messages.front();
        pending_messages.pop();
        broadcast_message_immediate(message);
    }
}

void WebSocketServer::close_connection(int client_fd)
{
    remove_from_epoll(client_fd);
    ws_connections.erase(client_fd);
    close(client_fd);
}
