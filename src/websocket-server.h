/* ============================================================================
 * websocket-server.h — WebSocket Server for Broadcast Overlay Control
 *
 * Implementa um servidor WebSocket autossuficiente (sem dependências externas)
 * que escuta em uma porta TCP configurável e aceita comandos JSON para
 * controlar o Broadcast Overlay System remotamente.
 *
 * Características:
 *   - Self-contained: SHA1, Base64 e WebSocket protocol implementados nativamente
 *   - Thread-safe: fila de comandos protegida por mutex
 *   - Múltiplos comandos: lower_third, gc, ticker, social
 *   - Ping/Pong: mantém conexão ativa
 *
 * Protocolo de comandos (JSON via WebSocket):
 *   { "command": "lower_third", "name": "...", "title": "...", "duration": 8.0 }
 *   { "command": "gc",          "text": "...", "enabled": true }
 *   { "command": "ticker",      "text": "...", "speed": 100.0, "enabled": true }
 *   { "command": "social",      "instagram": "@...", "tiktok": "@...", ... }
 *
 * Uso:
 *   WebSocketServer ws;
 *   ws.start(8080);
 *   // ... no tick do OBS:
 *   WSCommand cmd;
 *   while (ws.poll_command(cmd)) {
 *       // aplicar cmd.type e cmd.data_json no contexto
 *   }
 *   ws.stop();
 *
 * ============================================================================
 */

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

/* ── Configuração de Sockets (Cross-platform) ───────────────────────────────
 * Winsock2 no Windows, POSIX sockets no Linux/macOS.
 */

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

/* ── WebSocket Magic GUID (RFC 6455) ──────────────────────────────────────── */
#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* ── Porta padrão do servidor WebSocket ──────────────────────────────────── */
#define WS_DEFAULT_PORT 8080

/* ============================================================================
 * ESTRUTURA DE COMANDO
 * ============================================================================
 * Representa um comando recebido via WebSocket.
 * type:      "lower_third", "gc", "ticker", "social"
 * data_json: JSON string com os dados do comando (ex: {"name":"Maria","title":"CEO"})
 */
struct WSCommand {
    std::string type;
    std::string data_json;
};

/* ============================================================================
 * WEBSOCKET SERVER
 * ============================================================================
 * Servidor WebSocket single-client que escuta em uma porta TCP,
 * aceita uma conexão, realiza o handshake HTTP→WebSocket e então
 * recebe comandos JSON em texto.
 */
class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    /* Inicia o servidor na porta especificada.
     * Retorna true se o servidor iniciou com sucesso. */
    bool start(int port);

    /* Para o servidor e fecha todas as conexões. Thread-safe. */
    void stop();

    /* Retorna true se o servidor está rodando. */
    bool is_running() const { return running_; }

    /* Obtém o próximo comando da fila (thread-safe).
     * Deve ser chamado do tick do OBS.
     * Retorna true se um comando foi obtido. */
    bool poll_command(WSCommand &cmd);

private:
    /* ── Função da thread do servidor ────────────────────────────────────── */
    void server_thread_func();

    /* ── Gerencia um cliente conectado (handshake + loop de mensagens) ──── */
    void handle_client(socket_t client_fd);

    /* ── Realiza handshake HTTP → WebSocket ─────────────────────────────── */
    bool perform_handshake(socket_t client_fd);

    /* ── Lê um frame WebSocket (já desmascarado) ────────────────────────── */
    bool read_frame(socket_t client_fd, std::string &payload);

    /* ── Envia um frame WebSocket (texto) ───────────────────────────────── */
    bool send_frame(socket_t client_fd, const std::string &payload);

    /* ── SHA1 (para o handshake WebSocket) ──────────────────────────────── */
    static void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20]);

    /* ── Base64 encode (para o handshake WebSocket) ─────────────────────── */
    static std::string base64_encode(const uint8_t *data, size_t len);

    /* ── Estado do servidor ──────────────────────────────────────────────── */
    std::atomic<bool>  running_;      /* true enquanto o servidor está ativo */
    std::thread        server_thread_; /* Thread do servidor */
    socket_t           server_fd_;    /* Socket do servidor (listen) */
    int                port_;         /* Porta de escuta */

    /* ── Fila de comandos thread-safe ────────────────────────────────────── */
    std::mutex              mutex_;
    std::queue<WSCommand>   commands_;
};

/* ============================================================================
 * FUNÇÕES AUXILIARES PÚBLICAS
 * ============================================================================ */

/* Parsing de string para bool (aceita "true", "false", "1", "0") */
static inline bool ws_parse_bool(const char *val, bool default_val)
{
    if (!val || !*val) return default_val;
    if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) return true;
    if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) return false;
    return default_val;
}
