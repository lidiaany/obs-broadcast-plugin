/* ============================================================================
 * broadcast-source.h — Broadcast Overlay System (Context & Declarations)
 *
 * Define a estrutura de contexto do plugin e declara todas as funções
 * callback que o OBS utiliza para gerenciar a source personalizada.
 *
 * Texto renderizado via child sources oficiais do OBS:
 *   - text_gdiplus        (Windows, GDI+)
 *   - text_ft2_source_v2  (cross-platform, FreeType2 — OBS 27+)
 *   - text_ft2_source     (cross-platform, FreeType2 — legado)
 *
 * A criacao tenta cada tipo em ordem de prioridade ate
 * encontrar um disponivel (fallback automatico).
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
#define LOWER_THIRD_HEIGHT    120
#define OVERLAY_PADDING       20
#define GC_ANIM_DURATION      0.5f

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

    /* ── OPACIDADE (0.0 = invisivel, 1.0 = opaco) ────────────────────── */
    float opacity_global;
    float opacity_lt;
    float opacity_gc;
    float opacity_ticker;
    float opacity_social;

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

    /* ── ANIMACAO GC ──────────────────────────────────────────────────── */
    float   gc_anim_progress;    /* 0.0 -> 1.0 durante animacao */
    int     gc_anim_state;       /* 1 = entrando, 0 = parado, -1 = saindo */
    char   *gc_anim_type;        /* "fade", "scale", "slide" */
    char   *gc_anim_dir;         /* "up", "down", "left", "right" (slide) */
    float   gc_anim_duration;    /* duracao em segundos */

    /* ── WEBSOCKET SERVER ──────────────────────────────────────────────── */
    WebSocketServer *ws_server;
    int              ws_port;
    bool             ws_enabled;

    /* ── SHADER EFFECTS ─────────────────────────────────────────────────── */
    gs_effect_t *effect_glass;
    gs_eparam_t *ep_glass_color;
    gs_eparam_t *ep_glass_highlight;
    gs_eparam_t *ep_glass_top;
    gs_eparam_t *ep_glass_bot;
    gs_eparam_t *ep_glass_noise;
    gs_eparam_t *ep_glass_tint;
    gs_eparam_t *ep_glass_hl_width;
    bool         glass_loaded;

    /* ── TEXT SOURCES (child sources — API legítima do OBS para texto) ── */
    /* O OBS não tem gs_font_t. Texto é renderizado via child sources:     */
    /*   text_gdiplus (Windows/macOS) ou text_ft2_source_v2 (Linux).      */
    obs_source_t *ts_lt_name;           /* Lower Third: nome */
    obs_source_t *ts_lt_title;          /* Lower Third: cargo */
    obs_source_t *ts_gc;                /* GC: texto central */
    obs_source_t *ts_ticker;            /* Ticker: texto corrido */
    obs_source_t *ts_social_tag[4];     /* Social: labels (IG, TK, FB, YT) */
    obs_source_t *ts_social_handle[4];  /* Social: handles */
    int           gc_font_size;         /* Tamanho de fonte actual do GC */
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

/* ── Tags fixas das redes sociais ────────────────────────────────────────── */
extern const char *SOCIAL_TAGS[4];

/* ── Text source helpers ────────────────────────────────────────────────── */
obs_source_t *broadcast_create_text_src(const char *text, uint32_t argb_color,
                                         int font_size, bool bold);
void          broadcast_update_text_src(obs_source_t **src_ptr, const char *text,
                                         uint32_t argb_color, int font_size, bool bold);
void          broadcast_destroy_text_src(obs_source_t **src_ptr);

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

/* ── Carrega / libera shaders ──────────────────────────────────────────── */
void broadcast_load_effects(BroadcastContext *ctx);
void broadcast_unload_effects(BroadcastContext *ctx);

/* ============================================================================
 * FUNÇÕES AUXILIARES DE UTILIDADE
 * ============================================================================ */

float ease_out_cubic(float t);
float ease_in_out_cubic(float t);

#define bfree_safe(ptr)  do { if (ptr) { bfree(ptr); (ptr) = NULL; } } while(0)

#define GET_ALPHA_F(color)  (((float)(((color) >> 24) & 0xFF)) / 255.0f)
#define GET_ALPHA(color)    (((color) >> 24) & 0xFF)
#define GET_RED(color)      (((color) >> 16) & 0xFF)
#define GET_GREEN(color)    (((color) >> 8) & 0xFF)
#define GET_BLUE(color)     ((color) & 0xFF)
