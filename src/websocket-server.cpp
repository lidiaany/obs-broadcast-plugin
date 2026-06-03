/* ============================================================================
websocket-server.cpp — WebSocket Server Implementation
============================================================================ */
#include <obs-module.h>
#include "websocket-server.h"
#include <vector>
#include <sstream>

#define SHA1_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define K0 0x5A827999
#define K1 0x6ED9EBA1
#define K2 0x8F1BBCDC
#define K3 0xCA62C1D6

void WebSocketServer::sha1_hash(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    size_t ml = len * 8;
    size_t padded_len = ((len + 8 + 64) / 64) * 64;
    std::vector<uint8_t> padded(padded_len, 0);
    std::memcpy(padded.data(), data, len);
    padded[len] = 0x80;
    for (int i = 0; i < 8; i++) padded[padded_len - 8 + i] = (uint8_t)((ml >> (56 - i * 8)) & 0xFF);
    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = ((uint32_t)padded[offset + i * 4] << 24) | ((uint32_t)padded[offset + i * 4 + 1] << 16) | ((uint32_t)padded[offset + i * 4 + 2] << 8) | ((uint32_t)padded[offset + i * 4 + 3]);
        for (int i = 16; i < 80; i++) w[i] = SHA1_ROTL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        uint32_t f, k, temp;
        for (int i = 0; i < 80; i++) {
            if (i < 20) { f = (b & c) | ((~b) & d); k = K0; }
            else if (i < 40) { f = b ^ c ^ d; k = K1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = K2; }
            else { f = b ^ c ^ d; k = K3; }
            temp = SHA1_ROTL(a, 5) + f + e + k + w[i]; e = d; d = c; c = SHA1_ROTL(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    for (int i = 0; i < 4; i++) {
        out[i] = (uint8_t)((h0 >> (24 - i * 8)) & 0xFF); out[4 + i] = (uint8_t)((h1 >> (24 - i * 8)) & 0xFF);
        out[8 + i] = (uint8_t)((h2 >> (24 - i * 8)) & 0xFF); out[12 + i] = (uint8_t)((h3 >> (24 - i * 8)) & 0xFF);
        out[16 + i] = (uint8_t)((h4 >> (24 - i * 8)) & 0xFF);
    }
}

static const char BASE64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string WebSocketServer::base64_encode(const uint8_t *data, size_t len) {
    std::string result; result.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t triple = 0; int remaining = (int)(len - i);
        if (remaining > 0) triple |= ((uint32_t)data[i] << 16);
        if (remaining > 1) triple |= ((uint32_t)data[i + 1] << 8);
        if (remaining > 2) triple |= (uint32_t)data[i + 2];
        result += BASE64_CHARS[(triple >> 18) & 0x3F]; result += BASE64_CHARS[(triple >> 12) & 0x3F];
        result += (remaining > 1) ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=';
        result += (remaining > 2) ? BASE64_CHARS[triple & 0x3F] : '=';
    }
    return result;
}

WebSocketServer::WebSocketServer() : running_(false), server_fd_(INVALID_SOCKET_VALUE), port_(0) {}
WebSocketServer::~WebSocketServer() { stop(); }

bool WebSocketServer::start(int port) {
    if (running_) return false;
    port_ = port;
#ifdef _WIN32
    WSADATA wsaData; if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
#endif
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == INVALID_SOCKET_VALUE) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    int reuse = 1; setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    struct sockaddr_in addr; std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons((uint16_t)port);
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERR) { CLOSE_SOCKET(server_fd_); server_fd_ = INVALID_SOCKET_VALUE; return false; }
    if (listen(server_fd_, 1) == SOCKET_ERR) { CLOSE_SOCKET(server_fd_); server_fd_ = INVALID_SOCKET_VALUE; return false; }
    running_ = true; server_thread_ = std::thread(&WebSocketServer::server_thread_func, this);
    return true;
}

void WebSocketServer::stop() {
    if (!running_) return; running_ = false;
    if (server_fd_ != INVALID_SOCKET_VALUE) { CLOSE_SOCKET(server_fd_); server_fd_ = INVALID_SOCKET_VALUE; }
    if (server_thread_.joinable()) server_thread_.join();
#ifdef _WIN32
    WSACleanup();
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    while (!commands_.empty()) commands_.pop();
}

void WebSocketServer::server_thread_func() {
    while (running_) {
        struct sockaddr_in client_addr; socklen_t addr_len = sizeof(client_addr);
        socket_t client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (!running_) { if (client_fd != INVALID_SOCKET_VALUE) CLOSE_SOCKET(client_fd); break; }
        if (client_fd == INVALID_SOCKET_VALUE) continue;
        handle_client(client_fd); CLOSE_SOCKET(client_fd);
    }
}

void WebSocketServer::handle_client(socket_t client_fd) {
    if (!perform_handshake(client_fd)) return;
    while (running_) {
        std::string payload;
        if (!read_frame(client_fd, payload)) break;
        if (payload.empty()) continue;
        std::string cmd_type;
        auto cmd_pos = payload.find("\"command\"");
        if (cmd_pos != std::string::npos) {
            auto colon = payload.find(':', cmd_pos);
            if (colon != std::string::npos) {
                auto quote1 = payload.find('"', colon);
                if (quote1 != std::string::npos) {
                    auto quote2 = payload.find('"', quote1 + 1);
                    if (quote2 != std::string::npos) cmd_type = payload.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            }
        }
        if (cmd_type.empty()) { send_frame(client_fd, "{\"status\":\"error\",\"message\":\"missing 'command' field\"}"); continue; }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            WSCommand cmd; cmd.type = cmd_type; cmd.data_json = payload; commands_.push(cmd);
        }
        send_frame(client_fd, "{\"status\":\"ok\",\"command\":\"" + cmd_type + "\"}");
    }
}

bool WebSocketServer::perform_handshake(socket_t client_fd) {
    char buffer[4096]; int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) return false; buffer[bytes] = '\0';
    const char *key_marker = "Sec-WebSocket-Key: ";
    const char *key_start = std::strstr(buffer, key_marker);
    if (!key_start) return false;
    key_start += strlen(key_marker);
    const char *key_end = std::strstr(key_start, "\r\n");
    if (!key_end) return false;
    std::string client_key(key_start, key_end - key_start);
    std::string concat = client_key + WS_MAGIC_GUID;
    uint8_t hash[20]; sha1_hash((const uint8_t*)concat.data(), concat.size(), hash);
    std::string accept_key = base64_encode(hash, 20);
    std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_key + "\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
    if (send(client_fd, response.data(), (int)response.size(), 0) != (int)response.size()) return false;
    return true;
}

bool WebSocketServer::read_frame(socket_t client_fd, std::string &payload) {
    uint8_t header[2]; int bytes = recv(client_fd, (char*)header, 2, MSG_WAITALL);
    if (bytes != 2) return false;
    uint8_t opcode = header[0] & 0x0F; uint8_t masked = (header[1] >> 7) & 1; uint64_t len = header[1] & 0x7F;
    if (len == 126) { uint8_t ext[2]; if (recv(client_fd, (char*)ext, 2, MSG_WAITALL) != 2) return false; len = ((uint64_t)ext[0] << 8) | (uint64_t)ext[1]; }
    else if (len == 127) { uint8_t ext[8]; if (recv(client_fd, (char*)ext, 8, MSG_WAITALL) != 8) return false; len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | (uint64_t)ext[i]; }
    if (len > 1024 * 1024) return false;
    uint8_t mask[4] = {0}; if (masked) if (recv(client_fd, (char*)mask, 4, MSG_WAITALL) != 4) return false;
    std::vector<uint8_t> data((size_t)len);
    if (len > 0) { uint64_t total = 0; while (total < len) { int r = recv(client_fd, (char*)(data.data() + total), (int)(len - total), 0); if (r <= 0) return false; total += r; } }
    if (masked) for (uint64_t i = 0; i < len; i++) data[(size_t)i] ^= mask[i % 4];
    if (opcode == 0x1) { payload.assign((const char*)data.data(), data.size()); return true; }
    if (opcode == 0x8) return false;
    if (opcode == 0x9) {
        std::vector<uint8_t> pong_frame; pong_frame.push_back(0x8A);
        if (len < 126) pong_frame.push_back((uint8_t)len);
        else if (len < 65536) { pong_frame.push_back(126); pong_frame.push_back((uint8_t)((len >> 8) & 0xFF)); pong_frame.push_back((uint8_t)(len & 0xFF)); }
        else { pong_frame.push_back(127); for (int i = 7; i >= 0; i--) pong_frame.push_back((uint8_t)((len >> (i * 8)) & 0xFF)); }
        pong_frame.insert(pong_frame.end(), data.begin(), data.end());
        send(client_fd, (const char*)pong_frame.data(), (int)pong_frame.size(), 0); payload.clear(); return true;
    }
    if (opcode == 0xA) { payload.clear(); return true; }
    return false;
}

bool WebSocketServer::send_frame(socket_t client_fd, const std::string &payload) {
    size_t len = payload.size(); std::vector<uint8_t> frame; frame.push_back(0x81);
    if (len < 126) frame.push_back((uint8_t)len);
    else if (len < 65536) { frame.push_back(126); frame.push_back((uint8_t)((len >> 8) & 0xFF)); frame.push_back((uint8_t)(len & 0xFF)); }
    else { frame.push_back(127); for (int i = 7; i >= 0; i--) frame.push_back((uint8_t)((len >> (i * 8)) & 0xFF)); }
    frame.insert(frame.end(), payload.begin(), payload.end());
    if (send(client_fd, (const char*)frame.data(), (int)frame.size(), 0) != (int)frame.size()) return false;
    return true;
}

bool WebSocketServer::poll_command(WSCommand &cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.empty()) return false;
    cmd = commands_.front(); commands_.pop(); return true;
}