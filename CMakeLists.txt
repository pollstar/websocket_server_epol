cmake_minimum_required(VERSION 3.10)
project(WebSocketServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Add executable
add_executable(websocket_server main.cpp WebSocketServer.cpp)

# Link libraries
target_link_libraries(websocket_server 
    OpenSSL::SSL 
    OpenSSL::Crypto
    Threads::Threads
)

# Compiler flags
target_compile_options(websocket_server PRIVATE -Wall -Wextra -O2)

# Installation
install(TARGETS websocket_server DESTINATION bin)