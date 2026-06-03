/* ============================================================================
websocket-server.h — WebSocket Server for Broadcast Overlay Control
============================================================================ */
#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define SOCKET_ERR SOCKET_ERROR
#define CLOSE_SOCKET(s) closesocket(s)
#define SOCKET_LAST_ERROR() WSAGetLastError()
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE (-1)
#define SOCKET_ERR (-1)
#define CLOSE_SOCKET(s) close(s)
#define SOCKET_LAST_ERROR() errno
#endif

#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_DEFAULT_PORT 8085

struct WSCommand {
    std::string type;
    std::string data_json;
};

class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();
    bool start(int port);
    void stop();
    bool is_running() const { return running_; }
    bool poll_command(WSCommand &cmd);
private:
    void server_thread_func();
    void handle_client(socket_t client_fd);
    bool perform_handshake(socket_t client_fd);
    bool read_frame(socket_t client_fd, std::string &payload);
    bool send_frame(socket_t client_fd, const std::string &payload);
    static void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20]);
    static std::string base64_encode(const uint8_t *data, size_t len);

    std::atomic<bool> running_;
    std::thread server_thread_;
    socket_t server_fd_;
    int port_;
    std::mutex mutex_;
    std::queue<WSCommand> commands_;
};

static inline bool ws_parse_bool(const char *val, bool default_val) {
    if (!val || !*val) return default_val;
    if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) return true;
    if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) return false;
    return default_val;
}