/* ============================================================================
 * broadcast-source.h — Broadcast Overlay System (Context & Declarations)
 *
 * Define a estrutura de contexto do plugin e declara todas as funções
 * callback que o OBS utiliza para gerenciar a source personalizada.
 *
 * ============================================================================
 */

#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <cstdint>
#include <cstring>
#include <cmath>

/* Inclui o servidor WebSocket para controle remoto */
#include "websocket-server.h"

/* ============================================================================
 * CONSTANTES DO SISTEMA
 * ============================================================================ */

#define DEFAULT_SOURCE_WIDTH  1920
#define DEFAULT_SOURCE_HEIGHT 1080
#define LT_ANIM_DURATION      0.5f
#define LT_DISPLAY_DURATION   8.0f
#define TICKER_DEFAULT_SPEED  80.0f
#define TICKER_BAR_HEIGHT     40
#define FONT_CACHE_SIZE       8
#define FONT_CACHE_TOTAL      16
#define LOWER_THIRD_HEIGHT    120
#define OVERLAY_PADDING       20

/* ============================================================================
 * CORES PADRÃO (formato ARGB)
 * ============================================================================ */

#define COLOR_PRIMARY    0xCCE53935
#define COLOR_SECONDARY  0xCC1E1E1E
#define COLOR_ACCENT     0xFFFFFFFF
#define COLOR_BG         0xBB000000
#define COLOR_TICKER_BG  0xDD1A1A2E

/* ============================================================================
 * ENUMS
 * ============================================================================ */

enum LTAlignment {
    LT_ALIGN_LEFT   = 0,
    LT_ALIGN_RIGHT  = 1,
    LT_ALIGN_CENTER = 2
};

enum SocialPosition {
    SOCIAL_TOP_LEFT      = 0,
    SOCIAL_TOP_RIGHT     = 1,
    SOCIAL_BOTTOM_LEFT   = 2,
    SOCIAL_BOTTOM_RIGHT  = 3
};

/* ============================================================================
 * ESTRUTURA DE CONTEXTO DO BROADCAST OVERLAY
 * ============================================================================ */

struct BroadcastContext {
    obs_source_t *source;

    /* ── LOWER THIRD ───────────────────────────────────────────────────── */
    char   *lt_name;
    char   *lt_title;
    bool    lt_enabled;
    float   lt_duration;
    int     lt_alignment;

    /* ── GC ────────────────────────────────────────────────────────────── */
    char   *gc_text;
    bool    gc_enabled;

    /* ── TICKER ────────────────────────────────────────────────────────── */
    char   *ticker_text;
    bool    ticker_enabled;
    float   ticker_speed;

    /* ── REDES SOCIAIS ─────────────────────────────────────────────────── */
    bool    social_enabled;
    int     social_position;
    char   *instagram;
    char   *tiktok;
    char   *facebook;
    char   *youtube;

    /* ── CORES ─────────────────────────────────────────────────────────── */
    uint32_t color_primary;
    uint32_t color_secondary;
    uint32_t color_accent;
    uint32_t color_bg;

    /* ── DIMENSÕES ─────────────────────────────────────────────────────── */
    uint32_t width;
    uint32_t height;

    /* ── ESTADO DE ANIMAÇÃO ────────────────────────────────────────────── */
    float    elapsed;
    float    lt_anim_progress;
    float    lt_anim_state;
    bool     lt_is_visible;
    float    lt_visible_time;
    float    ticker_offset;

    /* ── WEBSOCKET SERVER ──────────────────────────────────────────────── */
    WebSocketServer *ws_server;         /* Servidor WebSocket (controle remoto) */
    int              ws_port;           /* Porta do servidor WebSocket */
    bool             ws_enabled;        /* Se o WebSocket está habilitado */

    /* ── SHADER EFFECTS ─────────────────────────────────────────────────── */
    gs_effect_t *effect_glass;         /* Efeito de vidro fosco (frosted glass) */
    gs_eparam_t *ep_glass_color;       /* Parâmetro: color */
    gs_eparam_t *ep_glass_highlight;   /* Parâmetro: highlight_color */
    gs_eparam_t *ep_glass_top;         /* Parâmetro: gradient_top */
    gs_eparam_t *ep_glass_bot;         /* Parâmetro: gradient_bot */
    gs_eparam_t *ep_glass_noise;       /* Parâmetro: noise_strength */
    gs_eparam_t *ep_glass_tint;        /* Parâmetro: glass_tint */
    gs_eparam_t *ep_glass_hl_width;    /* Parâmetro: highlight_width */
    bool         glass_loaded;         /* Se o shader foi carregado com sucesso */

    /* ── FONT CACHING ──────────────────────────────────────────────────── */
    gs_font_t font_cache[FONT_CACHE_TOTAL];
    int       font_sizes[FONT_CACHE_TOTAL];
    bool      font_cache_bold[FONT_CACHE_TOTAL];
    bool      font_dirty;
};

/* ============================================================================
 * DECLARAÇÃO DAS FUNÇÕES CALLBACK
 * ============================================================================ */

const char *broadcast_get_name(void *unused);
void *broadcast_create(obs_data_t *settings, obs_source_t *source);
void broadcast_destroy(void *data);
void broadcast_update(void *data, obs_data_t *settings);
uint32_t broadcast_get_width(void *data);
uint32_t broadcast_get_height(void *data);
void broadcast_get_defaults(obs_data_t *settings);
obs_properties_t *broadcast_get_properties(void *data);
void broadcast_video_render(void *data, gs_effect_t *effect);
void broadcast_video_tick(void *data, float seconds);

/* Processa comandos recebidos via WebSocket */
void broadcast_process_ws_commands(void *data);

/* ============================================================================
 * FUNÇÕES AUXILIARES DE RENDERIZAÇÃO
 * ============================================================================ */

void render_lower_third(BroadcastContext *ctx);
void render_gc(BroadcastContext *ctx);
void render_ticker(BroadcastContext *ctx);
void render_social_media(BroadcastContext *ctx);
void draw_rect(float x, float y, float w, float h, uint32_t color);
void draw_rounded_rect(float x, float y, float w, float h,
                        float radius, uint32_t color);
void draw_gradient_rect(float x, float y, float w, float h,
                         uint32_t color_left, uint32_t color_right);

/* ── Desenha retângulo com efeito vidro fosco (usando shader) ──────────── */
void draw_rect_glass(BroadcastContext *ctx, float x, float y,
                      float w, float h, uint32_t color, bool vertical);

/* ── Carrega os shaders do disco ───────────────────────────────────────── */
void broadcast_load_effects(BroadcastContext *ctx);

/* ── Libera os shaders ─────────────────────────────────────────────────── */
void broadcast_unload_effects(BroadcastContext *ctx);

/* ============================================================================
 * FUNÇÕES AUXILIARES DE UTILIDADE
 * ============================================================================ */

float ease_out_cubic(float t);
float ease_in_out_cubic(float t);

#define bfree_safe(ptr)  do { if (ptr) { bfree(ptr); (ptr) = NULL; } } while(0)
#define font_destroy_safe(font)  do { if (font) { gs_font_destroy(font); (font) = NULL; } } while(0)

#define GET_ALPHA_F(color)  (((float)(((color) >> 24) & 0xFF)) / 255.0f)
#define GET_ALPHA(color)    (((color) >> 24) & 0xFF)
#define GET_RED(color)      (((color) >> 16) & 0xFF)
#define GET_GREEN(color)    (((color) >> 8) & 0xFF)
#define GET_BLUE(color)     ((color) & 0xFF)
