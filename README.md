# WebSocket Server in C++

A simple WebSocket server non-blocking sockets for Linux.

## Features

- ✅ Non-blocking sockets with epoll
- ✅ Handles multiple clients concurrently
- ✅ Minimal dependencies (only pthread, OpenSSL and standard C++ libraries)
- ✅ Simple API for message handling
- ✅ Graceful shutdown support
- ✅ Example usage included

##  Build && Run
To build the project, clone the repository and follow the steps below.

### Requirements

- Linux (Ubuntu/Debian/CentOS/etc.)
- GCC 7+ or Clang 6+
- CMake 3.10+
- pthread library

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install libssl-dev cmake build-essential
```

**CentOS/RHEL:**
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake
```

### Build

```bash
# Create build directory
mkdir build
cd build

# Configure the project
cmake ..

# Compile
make

# Or build in release mode
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### Running

```bash
./websocket_server [port]
```

Default port is 8080.

## Testing

1. Start the server:
    ```bash
    ./websocket_server 8080
    ```
2. Open `test_client.html` in your browser.
3. Connect to `ws://localhost:8080`.
4. Send messages and observe echo responses.

## Architecture

### Main Components

- **Server Core:** Manages socket creation, binding, listening, and accepting new client connections.
- **Epoll Event Loop:** Uses Linux epoll for efficient, scalable I/O event notification, handling multiple clients concurrently.
- **Client Handler:** Each client connection is tracked and managed, with non-blocking reads/writes and message parsing.
- **Message Dispatcher:** Routes incoming WebSocket frames to user-defined handlers for processing and response.
- **Shutdown Controller:** Ensures graceful shutdown by closing sockets and cleaning up resources.

### Non-blocking Architecture

- Utilizes non-blocking sockets to prevent the server from stalling on slow or inactive clients.
- Epoll is configured in edge-triggered mode, minimizing system calls and maximizing throughput.
- All I/O operations are event-driven, allowing the server to efficiently handle thousands of simultaneous connections.
- No dedicated thread per client; a single event loop manages all connections, reducing resource usage.

### Thread Safety

- The core event loop runs in a single thread for simplicity and performance.
- Shared resources (such as connection lists) are protected using mutexes when accessed from auxiliary threads (e.g., for shutdown or message broadcasting).
- Message handlers can be extended to use worker threads for CPU-intensive tasks, with proper synchronization.


## Performance
- Uses minimal system resources
- Can handle thousands of concurrent connections
- Non-blocking I/O ensures high responsiveness
- Edge-triggered epoll minimizes the number of system calls

### Optimizations

- Compile with optimization flags (`-O2`, `-O3`)
- Use static linking if needed
- Profile with `perf` or `valgrind`

## Debugging

### Debug Build
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Using GDB
Run on default port 8080
```bash
gdb ./websocket_server
(gdb) run
```
Run on other port 
```bash
gdb --args ./websocket_server [port]
(gdb) run
```


### Memory Leak Check
```bash
valgrind --leak-check=full ./websocket_server [port]
```

## Limitations
- Basic error handling  
- No built-in SSL/TLS support (plain WebSocket only)  
- Limited protocol extension support  
- No authentication or authorization mechanisms  
- No IPv6 or Unix domain socket support  
- Minimal logging and monitoring features  
- Not production-hardened; intended for educational/demo purposes  
- Documentation and test coverage are limited
- Basic error handling

## Possible Improvements

- Add support for SSL/TLS (wss://) connections
- Implement configurable logging levels
- Improve error handling and reporting
- Add support for WebSocket protocol extensions (e.g., permessage-deflate)
- Provide a more flexible API for custom message routing
- Add unit and integration tests
- Support IPv6 and Unix domain sockets
- Implement authentication and authorization mechanisms
- Provide Dockerfile and CI/CD pipeline examples
- Enhance documentation with more usage examples


## License

MIT License