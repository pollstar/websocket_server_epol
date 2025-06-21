#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <chrono>

#include "WebSocketServer.h"

// Function to handle user input in a separate thread
void input_thread(WebSocketServer *server)
{
    std::string input;

    while (server->is_running())
    {
        std::cout << "> ";
        if (!std::getline(std::cin, input))
        {
            break;
        }

        if (input.empty())
            continue;

        if (input == "/quit")
        {
            std::cout << "Shutting down server..." << std::endl;
            server->stop();
            break;
        }
        else if (input == "/clients")
        {
            std::cout << "Connected clients: " << server->get_connected_clients_count() << std::endl;
        }
        else if (input == "/time")
        {
            auto now = std::time(nullptr);
            std::string time_msg = "Server time: " + std::string(std::ctime(&now));
            time_msg.pop_back(); // Remove newline
            server->queue_message(time_msg);
        }
        else
        {
            // Broadcast user message
            std::string broadcast_msg = "[Server]: " + input;
            server->queue_message(broadcast_msg);
        }
    }
}

int main(int argc, char *argv[])
{
    int port = 8080;
    if (argc > 1)
    {
        port = std::atoi(argv[1]);
    }

    try
    {
        WebSocketServer server(port);

        // Start a thread to handle user input
        std::thread input_handler(input_thread, &server);

        // Main server loop
        server.run();

        // Wait for the input thread to finish
        if (input_handler.joinable())
        {
            input_handler.join();
        }

        std::cout << "Server stopped." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}