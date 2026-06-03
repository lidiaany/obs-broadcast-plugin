/* ============================================================================
broadcast-source.h — Broadcast Overlay System (Context & Declarations)
============================================================================ */
#pragma once
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "websocket-server.h"

/* ============================================================================
CONSTANTES DO SISTEMA
============================================================================ */
#define DEFAULT_SOURCE_WIDTH  1920
#define DEFAULT_SOURCE_HEIGHT 1080
#define LT_ANIM_DURATION      1.2f  
#define LT_DISPLAY_DURATION   8.0f
#define TICKER_DEFAULT_SPEED  80.0f
#define TICKER_BAR_HEIGHT     40
#define LOWER_THIRD_HEIGHT    70    /* Ajustado para 70px (15+20+4+14+15+2) */
#define OVERLAY_PADDING       20
#define GC_ANIM_DURATION      0.5f
#define SOCIAL_CAROUSEL_INTERVAL  5.0f
#define SOCIAL_CAROUSEL_FADE_DUR  0.8f
#define SOCIAL_FOOTER_HEIGHT      65

/* ============================================================================
CORES PADRÃO (formato ARGB)
============================================================================ */
#define COLOR_NAVY           0xF21B2F4F  
#define COLOR_BLUE           0xE62C5D91  
#define COLOR_GOLD           0xCCD4AF37  
#define COLOR_PRIMARY        0xCCD4AF37  
#define COLOR_SECONDARY      0xCC2C5D91  
#define COLOR_ACCENT         0xFFFFFFFF  
#define COLOR_BG             0xF21B2F4F  
#define COLOR_TICKER_BG      0xDD1A1A2E  
#define COLOR_TEXT_SECONDARY 0xFFE6E6E6  /* Cinza claro para o cargo (HTML #e6e6e6) */

/* ============================================================================
ENUMS
============================================================================ */
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
TEXT CACHE
============================================================================ */
#define TEXT_CACHE_STR_MAX 512
struct TextCache {
    char     text[TEXT_CACHE_STR_MAX];
    uint32_t color;
    int      font_size;
    bool     bold;
    bool     dirty;  
};

/* ============================================================================
ESTRUTURA DE CONTEXTO DO BROADCAST OVERLAY
============================================================================ */
struct BroadcastContext {
    obs_source_t *source;

    /* LOWER THIRD */
    char   *lt_name;
    char   *lt_title;
    bool    lt_enabled;
    float   lt_duration;
    int     lt_alignment;

    /* GC */
    char   *gc_text;
    bool    gc_enabled;
    int     gc_font_size;

    /* TICKER */
    char   *ticker_text;
    bool    ticker_enabled;
    float   ticker_speed;
    float   ticker_height;
    int     ticker_font_size;
    float   ticker_padding;

    /* REDES SOCIAIS */
    bool    social_enabled;
    int     social_position;
    char   *instagram;
    char   *tiktok;
    char   *facebook;
    char   *youtube;

    /* CAROUSEL SOCIAL */
    int     social_carousel_index;
    float   social_carousel_timer;
    bool    social_carousel_paused;
    float   social_carousel_interval;

    /* OPACIDADE */
    float opacity_global;
    float opacity_lt;
    float opacity_gc;
    float opacity_ticker;
    float opacity_social;

    /* CORES */
    uint32_t color_primary;
    uint32_t color_secondary;
    uint32_t color_accent;
    uint32_t color_bg;
    uint32_t color_accent2;
    float    bg_opacity;
    float    glow_strength;

    /* DIMENSÕES */
    uint32_t width;
    uint32_t height;
    float    scale_x;
    float    scale_y;

    /* ESTADO DE ANIMAÇÃO */
    float     elapsed;
    float    lt_anim_progress;
    float    lt_anim_state;
    bool     lt_is_visible;
    float    lt_visible_time;
    float    ticker_offset;

    /* ANIMACAO LOWER THIRD */
    char   *lt_anim_type;
    char   *lt_anim_dir;
    float   lt_anim_duration;

    /* ANIMACAO GC */
    float    gc_anim_progress;
    int     gc_anim_state;
    char   *gc_anim_type;
    char   *gc_anim_dir;
    float   gc_anim_duration;

    /* WEBSOCKET SERVER */
    WebSocketServer *ws_server;
    int              ws_port;
    bool             ws_enabled;

    /* SHADER EFFECTS */
    gs_effect_t * effect_glass;
    gs_eparam_t *ep_glass_color;
    gs_eparam_t *ep_glass_highlight;
    gs_eparam_t *ep_glass_top;
    gs_eparam_t *ep_glass_bot;
    gs_eparam_t *ep_glass_noise;
    gs_eparam_t *ep_glass_tint;
    gs_eparam_t *ep_glass_hl_width;
    bool         glass_loaded;

    /* TEXT SOURCES */
    obs_source_t *ts_lt_name;
    obs_source_t *ts_lt_title;
    obs_source_t *ts_gc;
    obs_source_t *ts_ticker;
    obs_source_t *ts_social_tag[4];
    obs_source_t *ts_social_handle[4];

    /* TEXT CACHE */
    TextCache tc_lt_name;
    TextCache tc_lt_title;
    TextCache tc_gc;
    TextCache tc_ticker;
    TextCache tc_social_tag[4];
    TextCache tc_social_handle[4];
};

/* ============================================================================
DECLARAÇÃO DAS FUNÇÕES CALLBACK
============================================================================ */
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
void broadcast_process_ws_commands(void *data);

extern const char *SOCIAL_TAGS[4];

obs_source_t *broadcast_create_text_src(const char *text, uint32_t argb_color, int font_size, bool bold);
void          broadcast_update_text_src(obs_source_t **src_ptr, const char *text, uint32_t argb_color, int font_size, bool bold);
void          broadcast_update_text_src_cached(obs_source_t **src_ptr, TextCache *cache, const char *text, uint32_t argb_color, int font_size, bool bold);
void          broadcast_destroy_text_src(obs_source_t **src_ptr);

/* ============================================================================
FUNÇÕES AUXILIARES DE RENDERIZAÇÃO
============================================================================ */
void render_lower_third(BroadcastContext *ctx);
void render_gc(BroadcastContext *ctx);
void render_ticker(BroadcastContext *ctx);
void render_social_media(BroadcastContext *ctx);
void render_social_carousel(BroadcastContext *ctx);

void draw_gradient_3stop(float x, float y, float w, float h, uint32_t color_left, uint32_t color_mid, uint32_t color_right);
void draw_rect(float x, float y, float w, float h, uint32_t color);
void draw_rounded_rect(float x, float y, float w, float h, float radius, uint32_t color);
void draw_gradient_rect(float x, float y, float w, float h, uint32_t color_left, uint32_t color_right);
void draw_gradient_rect_v(float x, float y, float w, float h, uint32_t color_top, uint32_t color_bottom);
void draw_accent_line(float x, float y, float w, float thickness, uint32_t color, float glow_strength);
void draw_rect_glass(BroadcastContext *ctx, float x, float y, float w, float h, uint32_t color, bool vertical);
void broadcast_load_effects(BroadcastContext *ctx);
void broadcast_unload_effects(BroadcastContext *ctx);

float ease_out_cubic(float t);
float ease_in_out_cubic(float t);
float ease_out_expo(float t);
float ease_out_back(float t);

/* ============================================================================
UTILITÁRIOS
============================================================================ */
#define bfree_safe(ptr)  do { if (ptr) { bfree(ptr); (ptr) = NULL; } } while(0)
#define GET_ALPHA_F(color)  (((float)(((color) >> 24) & 0xFF)) / 255.0f)
#define GET_ALPHA(color)    (((color) >> 24) & 0xFF)
#define GET_RED(color)      (((color) >> 16) & 0xFF)
#define GET_GREEN(color)    (((color) >> 8) & 0xFF)
#define GET_BLUE(color)     ((color) & 0xFF)
#define MAKE_ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))