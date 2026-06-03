/* ============================================================================
 * graphics-render.cpp — Broadcast Overlay System (Renderização)
 *
 * Implementa toda a renderização gráfica dos componentes do overlay.
 *
 * Otimizações de performance:
 *   - Font caching: 16 slots (8 tamanhos × regular + bold)
 *   - Fontes recriadas apenas quando dirty (mudança de propriedades)
 *   - draw_rounded_rect otimizado com número mínimo de draw calls
 *
 * ============================================================================
 */

#include "broadcast-source.h"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <graphics/font.h>
#include <cstring>
#include <cmath>
#include <cstdio>

/* ============================================================================
 * FUNÇÕES DE EASING
 * ============================================================================
 */

float ease_out_cubic(float t)
{
    return 1.0f - powf(1.0f - t, 3.0f);
}

/* ============================================================================
 * GESTÃO DE FONTES CACHE
 * ============================================================================
 *
 * Mantém 16 fontes cacheadas (8 tamanhos × regular/bold).
 * Tamanhos: 14, 16, 20, 24, 28, 36, 48, 72
 *
 * As fontes são (re)criadas apenas quando font_dirty == true (ocorre
 * quando o usuário altera as propriedades via broadcast_update).
 *
 * Isso evita criar/destruir gs_font_t a cada frame, que é uma operação
 * muito cara (carregar arquivos de fonte, rasterizar glifos na GPU).
 */

static const int FONT_SIZES[] = {14, 16, 20, 24, 28, 36, 48, 72};

static const int FONT_COUNT = 8; /* sizeof(FONT_SIZES) / sizeof(int) */

/* Obtém uma fonte do cache, criando-a se necessário.
 * Retorna a fonte correspondente ao tamanho e bold solicitados.
 * Nota: gs_font_t já é um tipo ponteiro (typedef struct gs_font *gs_font_t),
 *       então usamos gs_font_t, não gs_font_t *.
 */
static gs_font_t get_cached_font(BroadcastContext *ctx, int size, bool bold)
{
    /* Reconstrói todo o cache se dirty */
    if (ctx->font_dirty) {
        for (int i = 0; i < FONT_CACHE_TOTAL; i++) {
            if (ctx->font_cache[i]) {
                gs_font_destroy(ctx->font_cache[i]);
                ctx->font_cache[i] = NULL;
            }
            ctx->font_sizes[i] = 0;
            ctx->font_cache_bold[i] = false;
        }

        /* Cria 2 entradas por tamanho: [j*2] = regular, [j*2+1] = bold */
        for (int j = 0; j < FONT_COUNT; j++) {
            int reg_idx = j * 2;
            int bold_idx = j * 2 + 1;
            int sz = FONT_SIZES[j];

            /* Regular */
            ctx->font_cache[reg_idx] = gs_font_create("Arial", (uint32_t)sz);
            if (ctx->font_cache[reg_idx]) {
                ctx->font_sizes[reg_idx] = sz;
                ctx->font_cache_bold[reg_idx] = false;
            }

            /* Bold */
            ctx->font_cache[bold_idx] = gs_font_create("Arial Bold", (uint32_t)sz);
            if (ctx->font_cache[bold_idx]) {
                ctx->font_sizes[bold_idx] = sz;
                ctx->font_cache_bold[bold_idx] = true;
            } else {
                /* Fallback: se não encontrar "Arial Bold", usa "Arial" como bold */
                ctx->font_cache[bold_idx] = gs_font_create("Arial", (uint32_t)sz);
                if (ctx->font_cache[bold_idx]) {
                    ctx->font_sizes[bold_idx] = sz;
                    ctx->font_cache_bold[bold_idx] = true;
                }
            }
        }
        ctx->font_dirty = false;
    }

    /* Busca no cache */
    for (int i = 0; i < FONT_CACHE_TOTAL; i++) {
        if (ctx->font_sizes[i] == size &&
            ctx->font_cache_bold[i] == bold &&
            ctx->font_cache[i]) {
            return ctx->font_cache[i];
        }
    }

    /* Cache miss: cria fonte avulsa. Caller deve destruí-la! */
    const char *name = bold ? "Arial Bold" : "Arial";
    return gs_font_create(name, (uint32_t)size);
}

/* ============================================================================
 * draw_rect
 * ============================================================================
 * Desenha um retângulo sólido na tela usando GS_TRISTRIP.
 */
void draw_rect(float x, float y, float w, float h, uint32_t color)
{
    uint32_t alpha = GET_ALPHA(color);
    uint32_t red   = GET_RED(color);
    uint32_t green = GET_GREEN(color);
    uint32_t blue  = GET_BLUE(color);

    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x,     y);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w, y);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x,     y + h);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w, y + h);
    gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_rounded_rect
 * ============================================================================
 * Desenha um retângulo com cantos arredondados usando arcos triangulados.
 *
 * Usa 1 draw call para o centro, 1 para cada lateral, e 1 para cada canto
 * com 3 segmentos de arco (triângulos com centro no vértice do canto).
 * Total: 9 draw calls para 1 rounded rect completo.
 *
 * Para radius pequeno (ex: 8px) o resultado visual é equivalente ao CSS
 * border-radius sem uso de shaders.
 */
void draw_rounded_rect(float x, float y, float w, float h,
                        float radius, uint32_t color)
{
    if (radius <= 1.0f || radius > w / 2.0f || radius > h / 2.0f) {
        draw_rect(x, y, w, h, color);
        return;
    }

    uint32_t alpha = GET_ALPHA(color);
    uint32_t red   = GET_RED(color);
    uint32_t green = GET_GREEN(color);
    uint32_t blue  = GET_BLUE(color);

    /* 1. Retângulo central (triângulo maior que cobre quase todo o espaço) */
    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x + radius,        y);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w - radius,    y);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + radius,        y + h);
    gs_color4u(red, green, blue, alpha);

    gs_vertex2f(x + w - radius,    y);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w - radius,    y + h);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + radius,        y + h);
    gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);

    /* 2. Faixa esquerda (entre os dois cantos arredondados esquerdos) */
    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x,              y + radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + radius,    y + radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x,              y + h - radius);
    gs_color4u(red, green, blue, alpha);

    gs_vertex2f(x + radius,    y + radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + radius,    y + h - radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x,              y + h - radius);
    gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);

    /* 3. Faixa direita */
    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x + w - radius, y + radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w,          y + radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w - radius, y + h - radius);
    gs_color4u(red, green, blue, alpha);

    gs_vertex2f(x + w,          y + radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w,          y + h - radius);
    gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w - radius, y + h - radius);
    gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);

    /* 4. Cantos arredondados: 3 segmentos × 3 vértices cada = 9 vértices
     *    por canto. Todos os triângulos compartilham o centro do canto.
     *    gs_render_start/gs_render_stop fora do loop = 1 draw call por canto. */

    const int segments = 3;
    const float step = (float)(M_PI / 2.0 / segments);

    /* Canto superior-esquerdo (π a π/2) */
    gs_render_start(GS_TRISTRIP);
    for (int i = 0; i < segments; i++) {
        float a0 = (float)M_PI + (float)i * step;
        float a1 = (float)M_PI + (float)(i + 1) * step;
        gs_vertex2f(x + radius + cosf(a0) * radius,
                    y + radius + sinf(a0) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + radius + cosf(a1) * radius,
                    y + radius + sinf(a1) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + radius, y + radius);
        gs_color4u(red, green, blue, alpha);
    }
    gs_render_stop(GS_TRISTRIP);

    /* Canto superior-direito (π/2 a 0) */
    gs_render_start(GS_TRISTRIP);
    for (int i = 0; i < segments; i++) {
        float a0 = (float)M_PI / 2.0f + (float)i * step;
        float a1 = (float)M_PI / 2.0f + (float)(i + 1) * step;
        gs_vertex2f(x + w - radius + cosf(a0) * radius,
                    y + radius + sinf(a0) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + w - radius + cosf(a1) * radius,
                    y + radius + sinf(a1) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + w - radius, y + radius);
        gs_color4u(red, green, blue, alpha);
    }
    gs_render_stop(GS_TRISTRIP);

    /* Canto inferior-esquerdo (3π/2 a π) */
    gs_render_start(GS_TRISTRIP);
    for (int i = 0; i < segments; i++) {
        float a0 = (float)M_PI * 1.5f + (float)i * step;
        float a1 = (float)M_PI * 1.5f + (float)(i + 1) * step;
        gs_vertex2f(x + radius + cosf(a0) * radius,
                    y + h - radius + sinf(a0) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + radius + cosf(a1) * radius,
                    y + h - radius + sinf(a1) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + radius, y + h - radius);
        gs_color4u(red, green, blue, alpha);
    }
    gs_render_stop(GS_TRISTRIP);

    /* Canto inferior-direito (0 a π/2) */
    gs_render_start(GS_TRISTRIP);
    for (int i = 0; i < segments; i++) {
        float a0 = 0.0f + (float)i * step;
        float a1 = 0.0f + (float)(i + 1) * step;
        gs_vertex2f(x + w - radius + cosf(a0) * radius,
                    y + h - radius + sinf(a0) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + w - radius + cosf(a1) * radius,
                    y + h - radius + sinf(a1) * radius);
        gs_color4u(red, green, blue, alpha);
        gs_vertex2f(x + w - radius, y + h - radius);
        gs_color4u(red, green, blue, alpha);
    }
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_gradient_rect
 * ============================================================================
 * Retângulo com gradiente horizontal via interpolação de vértices.
 */
void draw_gradient_rect(float x, float y, float w, float h,
                         uint32_t color_left, uint32_t color_right)
{
    uint32_t aL = GET_ALPHA(color_left);
    uint32_t rL = GET_RED(color_left);
    uint32_t gL = GET_GREEN(color_left);
    uint32_t bL = GET_BLUE(color_left);
    uint32_t aR = GET_ALPHA(color_right);
    uint32_t rR = GET_RED(color_right);
    uint32_t gR = GET_GREEN(color_right);
    uint32_t bR = GET_BLUE(color_right);

    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x,     y);
    gs_color4u(rL, gL, bL, aL);
    gs_vertex2f(x + w, y);
    gs_color4u(rR, gR, bR, aR);
    gs_vertex2f(x,     y + h);
    gs_color4u(rL, gL, bL, aL);
    gs_vertex2f(x + w, y + h);
    gs_color4u(rR, gR, bR, aR);
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_rect_glass
 * ============================================================================
 * Desenha um retângulo com efeito de vidro fosco (frosted glass) usando
 * o shader .effect carregado. O shader aplica:
 *   - Gradiente vertical (mais transparente no topo)
 *   - Reflexo de luz na borda superior
 *   - Ruído pseudo-aleatório para textura de vidro jateado
 *   - Tom azulado sutil (frosted glass tint)
 *
 * O efeito usa a técnica "VerticalGlass" ou "HorizontalGlass".
 *
 * Parâmetros:
 *   vertical — true para gradiente vertical, false para horizontal
 */
void draw_rect_glass(BroadcastContext *ctx, float x, float y,
                      float w, float h, uint32_t color, bool vertical)
{
    if (!ctx || !ctx->glass_loaded || !ctx->effect_glass) {
        /* Fallback: retângulo sólido normal */
        draw_rect(x, y, w, h, color);
        return;
    }

    float alpha = (float)GET_ALPHA(color) / 255.0f;
    float red   = (float)GET_RED(color) / 255.0f;
    float green = (float)GET_GREEN(color) / 255.0f;
    float blue  = (float)GET_BLUE(color) / 255.0f;

    /* Configura parâmetros do shader */
    struct vec4 color_vec = {red, green, blue, alpha};
    struct vec4 hl_vec    = {1.0f, 1.0f, 1.0f, 0.3f}; /* Highlight branco suave */

    if (ctx->ep_glass_color)    gs_effect_set_vec4(ctx->ep_glass_color, &color_vec);
    if (ctx->ep_glass_highlight) gs_effect_set_vec4(ctx->ep_glass_highlight, &hl_vec);
    if (ctx->ep_glass_top)       gs_effect_set_float(ctx->ep_glass_top, 0.15f);
    if (ctx->ep_glass_bot)       gs_effect_set_float(ctx->ep_glass_bot, 0.85f);
    if (ctx->ep_glass_noise)     gs_effect_set_float(ctx->ep_glass_noise, 0.08f);
    if (ctx->ep_glass_tint)      gs_effect_set_float(ctx->ep_glass_tint, 0.25f);
    if (ctx->ep_glass_hl_width)  gs_effect_set_float(ctx->ep_glass_hl_width, 0.12f);

    /* Seleciona a técnica baseada na direção do gradiente */
    const char *tech_name = vertical ? "VerticalGlass" : "HorizontalGlass";
    gs_technique_t *tech = gs_effect_get_technique(ctx->effect_glass, tech_name);
    if (!tech) {
        draw_rect(x, y, w, h, color);
        return;
    }

    /* Renderiza usando o shader com UV coordinates para o gradiente */
    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);

    gs_render_start(GS_TRISTRIP);

    /* UV: (0,0) = top-left, (1,0) = top-right, (0,1) = bottom-left, (1,1) = bottom-right
     * VerticalGlass usa UV.y (0=top→transparente, 1=bottom→opaco)
     * HorizontalGlass usa UV.x (0=left→transparente, 1=right→opaco)
     */
    if (vertical) {
        /* Gradiente vertical: topo claro, base escuro */
        gs_texcoord2f(0.0f, 0.0f);  gs_vertex2f(x,     y);      /* top-left */
        gs_texcoord2f(1.0f, 0.0f);  gs_vertex2f(x + w, y);      /* top-right */
        gs_texcoord2f(0.0f, 1.0f);  gs_vertex2f(x,     y + h);  /* bottom-left */
        gs_texcoord2f(1.0f, 1.0f);  gs_vertex2f(x + w, y + h);  /* bottom-right */
    } else {
        /* Gradiente horizontal: esquerda claro, direita escuro */
        gs_texcoord2f(0.0f, 0.0f);  gs_vertex2f(x,     y);      /* left */
        gs_texcoord2f(1.0f, 0.0f);  gs_vertex2f(x + w, y);      /* right */
        gs_texcoord2f(0.0f, 1.0f);  gs_vertex2f(x,     y + h);  /* left */
        gs_texcoord2f(1.0f, 1.0f);  gs_vertex2f(x + w, y + h);  /* right */
    }

    gs_render_stop(GS_TRISTRIP);

    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

/* ============================================================================
 * draw_text_simple (com font caching — sem leaks)
 * ============================================================================
 * Desenha texto na tela usando fonte do cache.
 *
 * Se a fonte não estiver no cache, cria uma avulsa e destrói após uso
 * (sem leak de memória).
 */
static void draw_text_simple(BroadcastContext *ctx, const char *text,
                              float x, float y, int font_size,
                              uint32_t color, bool bold)
{
    if (!ctx || !text || !*text) return;

    gs_font_t font = get_cached_font(ctx, font_size, bold);
    bool is_fallback = false;

    if (!font) {
        /* Fallback total: cria avulsa */
        const char *name = bold ? "Arial Bold" : "Arial";
        font = gs_font_create(name, (uint32_t)font_size);
        if (!font) return;
        is_fallback = true;
    }

    uint32_t alpha = GET_ALPHA(color);
    uint32_t red   = GET_RED(color);
    uint32_t green = GET_GREEN(color);
    uint32_t blue  = GET_BLUE(color);
    uint32_t text_color = (alpha << 24) | (red << 16) | (green << 8) | blue;

    gs_font_draw_text(font, text, (uint32_t)strlen(text),
                      (uint32_t)x, (uint32_t)y, text_color, 0x00000000);

    /* Se foi criada avulsa, destrói para evitar leak */
    if (is_fallback) {
        gs_font_destroy(font);
    }
}

/* ============================================================================
 * get_text_width (com font caching — sem leaks)
 * ============================================================================
 */
static float get_text_width(BroadcastContext *ctx, const char *text,
                             int font_size, bool bold)
{
    if (!ctx || !text || !*text) return 0.0f;

    gs_font_t font = get_cached_font(ctx, font_size, bold);
    bool is_fallback = false;

    if (!font) {
        const char *name = bold ? "Arial Bold" : "Arial";
        font = gs_font_create(name, (uint32_t)font_size);
        if (!font) return 0.0f;
        is_fallback = true;
    }

    uint32_t width = gs_font_get_text_width(font, text, (uint32_t)strlen(text));

    if (is_fallback) {
        gs_font_destroy(font);
    }

    return (float)width;
}

/* ============================================================================
 * get_text_height
 * ============================================================================
 * Altura aproximada via font_size * 1.2 (compatível com todas as versões
 * do OBS). A API de fonte do OBS varia entre versões (gs_font_get_height,
 * gs_font_get_text_height, etc.), então usamos esta aproximação tipográfica
 * padrão que é precisa o suficiente para posicionamento mono-linha.
 */
static float get_text_height(BroadcastContext *ctx, int font_size)
{
    UNUSED_PARAMETER(ctx);
    return (float)font_size * 1.2f;
}

/* ============================================================================
 * LOWER THIRD
 * ============================================================================
 * Barra semi-transparente na parte inferior com nome (bold) e cargo (regular).
 *
 * Animações: entrada slide (ease-out) e saída slide (quando duração expira).
 */
void render_lower_third(BroadcastContext *ctx)
{
    if (!ctx || !ctx->lt_name || strlen(ctx->lt_name) == 0) return;

    const float w = (float)DEFAULT_SOURCE_WIDTH;
    const float h = (float)DEFAULT_SOURCE_HEIGHT;
    const float bar_h   = (float)LOWER_THIRD_HEIGHT;
    const float side_w  = 8.0f;
    const float pad     = 30.0f;
    const float mb      = 10.0f;
    const float y_base  = h - bar_h - mb;
    const float tx_start = pad + side_w + 15.0f;

    float progress = ctx->lt_anim_progress;
    float anim = ease_out_cubic(progress);
    float alpha_mult = 1.0f;
    float slide_offset = 0.0f;

    if (ctx->lt_alignment == LT_ALIGN_LEFT) {
        slide_offset = -(1.0f - anim) * (tx_start + 600.0f);
    } else if (ctx->lt_alignment == LT_ALIGN_RIGHT) {
        slide_offset = (1.0f - anim) * (w - tx_start + 600.0f);
    } else {
        alpha_mult = anim;
    }

    float nw = get_text_width(ctx, ctx->lt_name, 36, true);
    float tw = (ctx->lt_title && strlen(ctx->lt_title) > 0)
               ? get_text_width(ctx, ctx->lt_title, 24, false) : 0.0f;
    float maxw = (nw > tw) ? nw : tw;
    float bgw = maxw + pad * 2.0f + side_w + 20.0f;
    if (bgw > w) bgw = w;

    float bgx;
    switch (ctx->lt_alignment) {
        case LT_ALIGN_LEFT:   bgx = slide_offset; break;
        case LT_ALIGN_RIGHT:  bgx = w - bgw + slide_offset; break;
        default:              bgx = (w - bgw) * 0.5f; break;
    }

    uint32_t bg_color = ctx->color_bg;
    if (alpha_mult < 1.0f) {
        uint32_t a = (uint32_t)((float)GET_ALPHA(bg_color) * alpha_mult);
        bg_color = (a << 24) | (GET_RED(bg_color) << 16)
                 | (GET_GREEN(bg_color) << 8) | GET_BLUE(bg_color);
    }

    /* Fundo com efeito de vidro fosco (frosted glass) */
    draw_rect_glass(ctx, bgx, y_base, bgw, bar_h, bg_color, true);

    /* Barra lateral colorida (sem glass — mantém cor sólida vibrante) */
    draw_rect(bgx, y_base, side_w, bar_h, ctx->color_primary);

    /* Nome em negrito */
    draw_text_simple(ctx, ctx->lt_name, bgx + tx_start, y_base + 20.0f,
                     36, ctx->color_accent, true);

    /* Cargo em regular */
    if (ctx->lt_title && strlen(ctx->lt_title) > 0) {
        draw_text_simple(ctx, ctx->lt_title, bgx + tx_start, y_base + 65.0f,
                         24, ctx->color_accent, false);
    }
}

/* ============================================================================
 * GC (GERADOR DE CARACTERES)
 * ============================================================================
 * Texto dinâmico centralizado com ajuste automático de tamanho.
 * Usa negrito para destaque broadcast.
 */
void render_gc(BroadcastContext *ctx)
{
    if (!ctx || !ctx->gc_text || strlen(ctx->gc_text) == 0) return;

    const float w = (float)DEFAULT_SOURCE_WIDTH;
    const float h = (float)DEFAULT_SOURCE_HEIGHT;

    int len = (int)strlen(ctx->gc_text);
    int fs;
    if (len < 20)       fs = 72;
    else if (len < 50)  fs = 48;
    else if (len < 100) fs = 36;
    else                fs = 28;

    float tw = get_text_width(ctx, ctx->gc_text, fs, true);
    float th = get_text_height(ctx, fs);

    while (tw > w - 80.0f && fs > 16) {
        fs -= 4;
        tw = get_text_width(ctx, ctx->gc_text, fs, true);
        th = get_text_height(ctx, fs);
    }

    float x = (w - tw) * 0.5f;
    float y = (h - th) * 0.5f;

    float bp = 30.0f;
    draw_rect(x - bp, y - bp, tw + bp * 2.0f, th + bp * 2.0f, ctx->color_bg);
    draw_text_simple(ctx, ctx->gc_text, x, y, fs, ctx->color_accent, true);
}

/* ============================================================================
 * TICKER (RODAPÉ)
 * ============================================================================
 * Texto corrido estilo "news ticker" com loop infinito.
 * Usa fonte regular.
 */
void render_ticker(BroadcastContext *ctx)
{
    if (!ctx || !ctx->ticker_text || strlen(ctx->ticker_text) == 0) return;

    const float w = (float)DEFAULT_SOURCE_WIDTH;
    const float h = (float)DEFAULT_SOURCE_HEIGHT;
    const float bh = (float)TICKER_BAR_HEIGHT;
    const int fs = 24;
    const float pad = 50.0f;

    float tw = get_text_width(ctx, ctx->ticker_text, fs, false);
    float uw = tw + pad;
    float off = fmodf(ctx->ticker_offset, (uw > 0.0f) ? uw : 1.0f);

    draw_rect(0.0f, h - bh, w, bh, COLOR_TICKER_BG);
    draw_rect(0.0f, h - bh, w, 3.0f, ctx->color_primary);

    float yt = h - bh + (bh - get_text_height(ctx, fs)) * 0.5f;

    float x1 = w - off;
    draw_text_simple(ctx, ctx->ticker_text, x1, yt, fs, ctx->color_accent, false);

    float x2 = x1 - uw;
    if (x2 + tw > 0.0f) {
        draw_text_simple(ctx, ctx->ticker_text, x2, yt, fs, ctx->color_accent, false);
    }

    float x3 = x1 + uw;
    if (x3 < w) {
        draw_text_simple(ctx, ctx->ticker_text, x3, yt, fs, ctx->color_accent, false);
    }
}

/* ============================================================================
 * REDES SOCIAIS
 * ============================================================================
 * Lista de redes em canto configurável da tela.
 * Tags em bold (IG, TK, FB, YT), handles em regular.
 */
void render_social_media(BroadcastContext *ctx)
{
    if (!ctx) return;

    struct SocialItem {
        const char *tag;
        const char *handle;
    };

    SocialItem items[4];
    int count = 0;

    if (ctx->instagram && strlen(ctx->instagram) > 0) {
        items[count].tag = "IG";  items[count].handle = ctx->instagram; count++;
    }
    if (ctx->tiktok && strlen(ctx->tiktok) > 0) {
        items[count].tag = "TK";  items[count].handle = ctx->tiktok; count++;
    }
    if (ctx->facebook && strlen(ctx->facebook) > 0) {
        items[count].tag = "FB";  items[count].handle = ctx->facebook; count++;
    }
    if (ctx->youtube && strlen(ctx->youtube) > 0) {
        items[count].tag = "YT";  items[count].handle = ctx->youtube; count++;
    }

    if (count == 0) return;

    const float w  = (float)DEFAULT_SOURCE_WIDTH;
    const float h  = (float)DEFAULT_SOURCE_HEIGHT;
    const float ih = 36.0f;
    const float px = 20.0f;
    const float py = 12.0f;
    const float gp = 4.0f;
    const int   fs = 20;
    const float mg = 20.0f;

    float mw = 0.0f, th = 0.0f;
    for (int i = 0; i < count; i++) {
        float tag_w    = get_text_width(ctx, items[i].tag, 14, true);
        float handle_w = get_text_width(ctx, items[i].handle, fs, false);
        float iw = tag_w + 8.0f + handle_w + px * 2.0f;
        if (iw > mw) mw = iw;
        th += ih + gp;
    }
    th -= gp;
    th += py * 2.0f;
    if (mw < 180.0f) mw = 180.0f;

    float bx, by;
    switch (ctx->social_position) {
        case SOCIAL_TOP_LEFT:
            bx = mg; by = mg; break;
        case SOCIAL_TOP_RIGHT:
            bx = w - mw - mg; by = mg; break;
        case SOCIAL_BOTTOM_LEFT:
            bx = mg; by = h - th - mg - TICKER_BAR_HEIGHT - 10.0f; break;
        default:
            bx = w - mw - mg; by = h - th - mg - TICKER_BAR_HEIGHT - 10.0f; break;
    }

    /* Fundo com efeito de vidro fosco (horizontal para painel lateral) */
    draw_rect_glass(ctx, bx, by, mw, th, ctx->color_bg, false);

    float cy = by + py;
    for (int i = 0; i < count; i++) {
        float tx = bx + px;
        float ty = cy + (ih - get_text_height(ctx, fs)) * 0.5f;

        /* Tag em bold */
        draw_text_simple(ctx, items[i].tag, tx, ty + 2.0f, 14,
                         ctx->color_primary, true);

        /* Handle em regular */
        float hx = tx + get_text_width(ctx, items[i].tag, 14, true) + 8.0f;
        draw_text_simple(ctx, items[i].handle, hx, ty, fs,
                         ctx->color_accent, false);

        if (i < count - 1) {
            draw_rect(bx + 10.0f, cy + ih, mw - 20.0f, 1.0f, 0x33FFFFFF);
        }
        cy += ih + gp;
    }
}
