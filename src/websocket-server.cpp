/* ============================================================================
 * websocket-server.cpp — WebSocket Server Implementation
 *
 * Implementacao completa do servidor WebSocket para controle remoto do
 * Broadcast Overlay System via comandos JSON.
 *
 * Contem implementacoes nativas de:
 *   - SHA1 (para handshake WebSocket RFC 6455)
 *   - Base64 (para codificacao da chave de handshake)
 *   - Servidor TCP (cross-platform: Winsock2 / POSIX)
 *   - Protocolo WebSocket (framing, masking, ping/pong)
 *
 * ============================================================================
 */

#include <obs-module.h>
#include "websocket-server.h"
#include <vector>
#include <sstream>

/* ============================================================================
 * IMPLEMENTACAO SHA1
 * ============================================================================
 * SHA-1 hash function implementation (FIPS 180-4).
 * Implementacao minimalista sem dependencias externas.
 * Necessaria para o handshake WebSocket (Sec-WebSocket-Accept).
 */

/* Rotacao circular a esquerda de 32 bits */
#define SHA1_ROTL(x, n)  (((x) << (n)) | ((x) >> (32 - (n))))

/* Constantes SHA1 */
#define K0 0x5A827999
#define K1 0x6ED9EBA1
#define K2 0x8F1BBCDC
#define K3 0xCA62C1D6

void WebSocketServer::sha1_hash(const uint8_t *data, size_t len, uint8_t out[20])
{
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    /* Pre-processamento: padding */
    size_t ml = len * 8; /* tamanho em bits */
    size_t padded_len = ((len + 8 + 64) / 64) * 64;
    std::vector<uint8_t> padded(padded_len, 0);
    std::memcpy(padded.data(), data, len);
    padded[len] = 0x80; /* bit 1 no final dos dados */

    /* Tamanho em bits no final (big-endian) */
    for (int i = 0; i < 8; i++) {
        padded[padded_len - 8 + i] = (uint8_t)((ml >> (56 - i * 8)) & 0xFF);
    }

    /* Processa cada bloco de 64 bytes (512 bits) */
    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t w[80];

        /* Prepara o bloco: 16 palavras de 32 bits (big-endian) */
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)padded[offset + i * 4] << 24)
                 | ((uint32_t)padded[offset + i * 4 + 1] << 16)
                 | ((uint32_t)padded[offset + i * 4 + 2] << 8)
                 | ((uint32_t)padded[offset + i * 4 + 3]);
        }

        /* Expande para 80 palavras */
        for (int i = 16; i < 80; i++) {
            w[i] = SHA1_ROTL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        uint32_t f, k, temp;

        for (int i = 0; i < 80; i++) {
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = K0;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = K1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = K2;
            } else {
                f = b ^ c ^ d;
                k = K3;
            }
            temp = SHA1_ROTL(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = SHA1_ROTL(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    /* Saida em big-endian */
    for (int i = 0; i < 4; i++) {
        out[i]      = (uint8_t)((h0 >> (24 - i * 8)) & 0xFF);
        out[4 + i]  = (uint8_t)((h1 >> (24 - i * 8)) & 0xFF);
        out[8 + i]  = (uint8_t)((h2 >> (24 - i * 8)) & 0xFF);
        out[12 + i] = (uint8_t)((h3 >> (24 - i * 8)) & 0xFF);
        out[16 + i] = (uint8_t)((h4 >> (24 - i * 8)) & 0xFF);
    }
}

/* ============================================================================
 * IMPLEMENTACAO BASE64
 * ============================================================================
 * Base64 encode (RFC 4648). Usada para codificar o hash SHA1 durante o
 * handshake WebSocket.
 */

static const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string WebSocketServer::base64_encode(const uint8_t *data, size_t len)
{
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t triple = 0;
        int remaining = (int)(len - i);
        if (remaining > 0) triple |= ((uint32_t)data[i] << 16);
        if (remaining > 1) triple |= ((uint32_t)data[i + 1] << 8);
        if (remaining > 2) triple |= (uint32_t)data[i + 2];

        result += BASE64_CHARS[(triple >> 18) & 0x3F];
        result += BASE64_CHARS[(triple >> 12) & 0x3F];
        result += (remaining > 1) ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=';
        result += (remaining > 2) ? BASE64_CHARS[triple & 0x3F] : '=';
    }

    return result;
}

/* ============================================================================
 * CONSTRUTOR / DESTRUTOR
 * ============================================================================ */

WebSocketServer::WebSocketServer()
    : running_(false)
    , server_fd_(INVALID_SOCKET_VALUE)
    , port_(0)
{
}

WebSocketServer::~WebSocketServer()
{
    stop();
}

/* ============================================================================
 * SERVER START
 * ============================================================================
 * Inicia o servidor WebSocket em uma thread separada.
 * Cria o socket TCP, faz bind e listen na porta especificada.
 */
bool WebSocketServer::start(int port)
{
    if (running_) {
        blog(LOG_WARNING, "[Broadcast WS] Servidor ja esta rodando na porta %d", port_);
        return false;
    }

    port_ = port;

#ifdef _WIN32
    /* Inicializa Winsock2 no Windows */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        blog(LOG_ERROR, "[Broadcast WS] Falha ao inicializar Winsock2");
        return false;
    }
#endif

    /* Cria socket TCP */
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == INVALID_SOCKET_VALUE) {
        blog(LOG_ERROR, "[Broadcast WS] Falha ao criar socket (erro: %d)",
             SOCKET_LAST_ERROR());
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    /* Permite reutilizar o endereco (evita "Address already in use") */
    int reuse = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&reuse, sizeof(reuse)) == SOCKET_ERR) {
        blog(LOG_WARNING, "[Broadcast WS] Aviso: nao foi possivel setar SO_REUSEADDR");
    }

    /* Configura endereco do servidor */
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; /* Escuta em todas as interfaces */
    addr.sin_port = htons((uint16_t)port);

    /* Bind */
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERR) {
        blog(LOG_ERROR, "[Broadcast WS] Falha no bind na porta %d (erro: %d)",
             port, SOCKET_LAST_ERROR());
        CLOSE_SOCKET(server_fd_);
        server_fd_ = INVALID_SOCKET_VALUE;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    /* Listen (backlog de 1 conexao — so uma por vez) */
    if (listen(server_fd_, 1) == SOCKET_ERR) {
        blog(LOG_ERROR, "[Broadcast WS] Falha no listen (erro: %d)",
             SOCKET_LAST_ERROR());
        CLOSE_SOCKET(server_fd_);
        server_fd_ = INVALID_SOCKET_VALUE;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    /* Inicia thread do servidor */
    running_ = true;
    server_thread_ = std::thread(&WebSocketServer::server_thread_func, this);

    blog(LOG_INFO, "[Broadcast WS] Servidor WebSocket iniciado na porta %d", port);
    return true;
}

/* ============================================================================
 * SERVER STOP
 * ============================================================================
 * Para o servidor e fecha a conexao. Thread-safe.
 */
void WebSocketServer::stop()
{
    if (!running_) return;

    running_ = false;

    /* Fecha o socket do servidor para interromper accept() */
    if (server_fd_ != INVALID_SOCKET_VALUE) {
        CLOSE_SOCKET(server_fd_);
        server_fd_ = INVALID_SOCKET_VALUE;
    }

    /* Aguarda a thread do servidor terminar */
    if (server_thread_.joinable()) {
        server_thread_.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    /* Limpa fila de comandos */
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!commands_.empty()) {
            commands_.pop();
        }
    }

    blog(LOG_INFO, "[Broadcast WS] Servidor WebSocket parado na porta %d", port_);
}

/* ============================================================================
 * THREAD DO SERVIDOR
 * ============================================================================
 * Loop principal: aceita conexoes, faz handshake, processa mensagens.
 * Executa em thread separada.
 */
void WebSocketServer::server_thread_func()
{
    blog(LOG_DEBUG, "[Broadcast WS] Thread do servidor iniciada.");

    while (running_) {
        /* Aceita uma conexao (blocking, mas interrompido pelo close do socket) */
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        socket_t client_fd = accept(server_fd_,
                                     (struct sockaddr*)&client_addr,
                                     &addr_len);

        if (!running_) {
            /* Servidor foi parado enquanto accept() aguardava */
            if (client_fd != INVALID_SOCKET_VALUE) {
                CLOSE_SOCKET(client_fd);
            }
            break;
        }

        if (client_fd == INVALID_SOCKET_VALUE) {
            if (running_) {
                blog(LOG_WARNING, "[Broadcast WS] Erro no accept (erro: %d)",
                     SOCKET_LAST_ERROR());
            }
            continue;
        }

        char client_ip[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        blog(LOG_INFO, "[Broadcast WS] Cliente conectado: %s", client_ip);

        /* Gerencia o cliente (handshake + loop de mensagens) */
        handle_client(client_fd);

        /* Fecha conexao com o cliente */
        CLOSE_SOCKET(client_fd);
        blog(LOG_INFO, "[Broadcast WS] Cliente desconectado: %s", client_ip);
    }

    blog(LOG_DEBUG, "[Broadcast WS] Thread do servidor encerrada.");
}

/* ============================================================================
 * HANDLE CLIENT
 * ============================================================================
 * Realiza o handshake WebSocket e depois entra no loop de mensagens.
 */
void WebSocketServer::handle_client(socket_t client_fd)
{
    /* 1. Realiza handshake HTTP -> WebSocket */
    if (!perform_handshake(client_fd)) {
        blog(LOG_WARNING, "[Broadcast WS] Handshake falhou");
        return;
    }

    blog(LOG_INFO, "[Broadcast WS] Handshake concluido. Pronto para receber comandos.");

    /* 2. Loop de mensagens */
    while (running_) {
        std::string payload;
        if (!read_frame(client_fd, payload)) {
            /* Cliente desconectou ou erro */
            break;
        }

        if (payload.empty()) {
            continue; /* Ping/pong ou frame de controle */
        }

        /* Recebeu um comando JSON */
        blog(LOG_DEBUG, "[Broadcast WS] Comando recebido: %s",
             payload.substr(0, 200).c_str());

        /* Extrai o campo "command" do JSON de forma simples */
        std::string cmd_type;
        std::string cmd_data = payload;

        /* Procura por "command":"..." no JSON */
        auto cmd_pos = payload.find("\"command\"");
        if (cmd_pos != std::string::npos) {
            auto colon = payload.find(':', cmd_pos);
            if (colon != std::string::npos) {
                auto quote1 = payload.find('"', colon);
                if (quote1 != std::string::npos) {
                    auto quote2 = payload.find('"', quote1 + 1);
                    if (quote2 != std::string::npos) {
                        cmd_type = payload.substr(quote1 + 1, quote2 - quote1 - 1);
                    }
                }
            }
        }

        if (cmd_type.empty()) {
            blog(LOG_WARNING, "[Broadcast WS] Comando sem campo 'command'");
            send_frame(client_fd, "{\"status\":\"error\",\"message\":\"missing 'command' field\"}");
            continue;
        }

        /* Enfileira o comando para ser processado no tick do OBS */
        {
            std::lock_guard<std::mutex> lock(mutex_);
            WSCommand cmd;
            cmd.type = cmd_type;
            cmd.data_json = cmd_data;
            commands_.push(cmd);
        }

        /* Confirma recebimento */
        std::string ack = "{\"status\":\"ok\",\"command\":\"" + cmd_type + "\"}";
        send_frame(client_fd, ack);
    }
}

/* ============================================================================
 * PERFORM HANDSHAKE
 * ============================================================================
 * Le a requisicao HTTP de upgrade do cliente e responde com o handshake
 * WebSocket (RFC 6455 Section 4).
 *
 * Formato da requisicao do cliente:
 *   GET / HTTP/1.1
 *   Host: ...
 *   Upgrade: websocket
 *   Connection: Upgrade
 *   Sec-WebSocket-Key: <base64>
 *   Sec-WebSocket-Version: 13
 *
 * Resposta do servidor:
 *   HTTP/1.1 101 Switching Protocols
 *   Upgrade: websocket
 *   Connection: Upgrade
 *   Sec-WebSocket-Accept: <base64(SHA1(key + GUID))>
 */
bool WebSocketServer::perform_handshake(socket_t client_fd)
{
    char buffer[4096];
    int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        return false;
    }
    buffer[bytes] = '\0';

    /* Extrai o Sec-WebSocket-Key */
    const char *key_marker = "Sec-WebSocket-Key: ";
    const char *key_start = std::strstr(buffer, key_marker);
    if (!key_start) {
        blog(LOG_WARNING, "[Broadcast WS] Handshake: campo Sec-WebSocket-Key nao encontrado");
        return false;
    }
    key_start += strlen(key_marker);

    /* Pula whitespace e encontra o fim da linha */
    const char *key_end = std::strstr(key_start, "\r\n");
    if (!key_end) {
        return false;
    }

    std::string client_key(key_start, key_end - key_start);

    /* Concatena com a GUID magica */
    std::string concat = client_key + WS_MAGIC_GUID;

    /* Calcula SHA1 */
    uint8_t hash[20];
    sha1_hash((const uint8_t*)concat.data(), concat.size(), hash);

    /* Codifica em Base64 */
    std::string accept_key = base64_encode(hash, 20);

    /* Monta resposta HTTP */
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    if (send(client_fd, response.data(), (int)response.size(), 0) != (int)response.size()) {
        return false;
    }

    return true;
}

/* ============================================================================
 * READ FRAME
 * ============================================================================
 * Le um frame WebSocket (RFC 6455 Section 5.2).
 *
 * Formato do frame:
 *   Byte 0: FIN (1 bit) | RSV (3 bits) | Opcode (4 bits)
 *   Byte 1: MASK (1 bit) | Payload length (7 bits)
 *   Bytes 2-3: Extended length (se length == 126)
 *   Bytes 2-9: Extended length (se length == 127)
 *   Bytes seguintes: Masking key (4 bytes, se MASK == 1)
 *   Bytes seguintes: Payload data
 *
 * Suporta opcodes:
 *   0x1 = Text frame
 *   0x8 = Close frame
 *   0x9 = Ping
 *   0xA = Pong
 *
 * Retorna true se leu um frame de texto com sucesso.
 * O payload e retornado sem mascara (ja decodificado).
 */
bool WebSocketServer::read_frame(socket_t client_fd, std::string &payload)
{
    uint8_t header[2];
    int bytes = recv(client_fd, (char*)header, 2, MSG_WAITALL);
    if (bytes != 2) {
        return false;
    }

    uint8_t fin       = (header[0] >> 7) & 1;
    uint8_t opcode    = header[0] & 0x0F;
    uint8_t masked    = (header[1] >> 7) & 1;
    uint64_t len      = header[1] & 0x7F;

    UNUSED_PARAMETER(fin);

    /* Sanity check: rejeita frames maiores que 1MB */
    static const uint64_t MAX_FRAME_SIZE = 1024 * 1024;

    /* Le tamanho extended */
    if (len == 126) {
        uint8_t ext[2];
        if (recv(client_fd, (char*)ext, 2, MSG_WAITALL) != 2) return false;
        len = ((uint64_t)ext[0] << 8) | (uint64_t)ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (recv(client_fd, (char*)ext, 8, MSG_WAITALL) != 8) return false;
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | (uint64_t)ext[i];
        }
    }

    /* Valida tamanho maximo para evitar DoS */
    if (len > MAX_FRAME_SIZE) {
        blog(LOG_WARNING, "[Broadcast WS] Frame muito grande: %llu bytes (max: %llu)",
             (unsigned long long)len,
             (unsigned long long)MAX_FRAME_SIZE);
        return false;
    }

    /* Le masking key (se mascarado) */
    uint8_t mask[4] = {0};
    if (masked) {
        if (recv(client_fd, (char*)mask, 4, MSG_WAITALL) != 4) return false;
    }

    /* Le payload */
    std::vector<uint8_t> data((size_t)len);
    if (len > 0) {
        uint64_t total = 0;
        while (total < len) {
            int r = recv(client_fd, (char*)(data.data() + total),
                         (int)(len - total), 0);
            if (r <= 0) return false;
            total += r;
        }
    }

    /* Aplica mascara (XOR com masking key) */
    if (masked) {
        for (uint64_t i = 0; i < len; i++) {
            data[(size_t)i] ^= mask[i % 4];
        }
    }

    /* Processa opcode */
    switch (opcode) {
        case 0x1: /* Text frame */
            payload.assign((const char*)data.data(), data.size());
            return true;

        case 0x8: /* Close frame */
            blog(LOG_INFO, "[Broadcast WS] Cliente enviou close frame");
            return false;

        case 0x9: /* Ping — responde com Pong (RFC 6455 Section 5.5.3) */
        {
            /* RFC 6455 exige que o Pong ecoe os dados de aplicacao do Ping */
            std::vector<uint8_t> pong_frame;
            pong_frame.push_back(0x8A); /* FIN + Opcode 0xA (Pong) */
            if (len < 126) {
                pong_frame.push_back((uint8_t)len);
            } else if (len < 65536) {
                pong_frame.push_back(126);
                pong_frame.push_back((uint8_t)((len >> 8) & 0xFF));
                pong_frame.push_back((uint8_t)(len & 0xFF));
            } else {
                pong_frame.push_back(127);
                for (int i = 7; i >= 0; i--) {
                    pong_frame.push_back((uint8_t)((len >> (i * 8)) & 0xFF));
                }
            }
            pong_frame.insert(pong_frame.end(), data.begin(), data.end());
            send(client_fd, (const char*)pong_frame.data(), (int)pong_frame.size(), 0);
            payload.clear();
            return true; /* Nao e um comando, continua */
        }

        case 0xA: /* Pong — apenas ignora */
            payload.clear();
            return true;

        default:
            blog(LOG_WARNING, "[Broadcast WS] Opcode desconhecido: %d", opcode);
            return false;
    }
}

/* ============================================================================
 * SEND FRAME
 * ============================================================================
 * Envia um frame WebSocket de texto (sem mascara — apenas servidor -> cliente).
 */
bool WebSocketServer::send_frame(socket_t client_fd, const std::string &payload)
{
    size_t len = payload.size();
    std::vector<uint8_t> frame;

    /* Byte 0: FIN (1) + RSV (000) + Opcode (0001 = text) = 0x81 */
    frame.push_back(0x81);

    /* Byte 1+: Payload length */
    if (len < 126) {
        frame.push_back((uint8_t)len);
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((uint8_t)((len >> 8) & 0xFF));
        frame.push_back((uint8_t)(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((uint8_t)((len >> (i * 8)) & 0xFF));
        }
    }

    /* Payload data */
    frame.insert(frame.end(), payload.begin(), payload.end());

    if (send(client_fd, (const char*)frame.data(), (int)frame.size(), 0) != (int)frame.size()) {
        return false;
    }

    return true;
}

/* ============================================================================
 * POLL COMMAND
 * ============================================================================
 * Obtem o proximo comando da fila. Chamado pelo tick do OBS.
 * Thread-safe.
 */
bool WebSocketServer::poll_command(WSCommand &cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.empty()) {
        return false;
    }
    cmd = commands_.front();
    commands_.pop();
    return true;
}
