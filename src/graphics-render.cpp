/* ============================================================================
 * graphics-render.cpp — Broadcast Overlay System (Renderização)
 *
 * Implementa toda a renderização gráfica dos componentes do overlay.
 *
 * TEXTO: renderizado via child sources do OBS (text_gdiplus / text_ft2_source_v2).
 * O OBS não tem uma API de fontes directa (gs_font_t não existe no libobs).
 * As child sources são geridas em BroadcastContext e actualizadas em
 * broadcast_update(). Aqui apenas as renderizamos com transforms de posição.
 *
 * GEOMETRIA: retângulos sólidos, gradientes e cantos arredondados usam a
 * API legítima gs_render_start / gs_vertex2f / gs_color4u.
 *
 * SHADER: frosted glass via gs_effect_t carregado de ficheiro .effect.
 *
 * ============================================================================
 */

#include "broadcast-source.h"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <cstring>
#include <cmath>
#include <cstdio>

/* ============================================================================
 * FUNÇÕES DE EASING
 * ============================================================================ */

float ease_out_cubic(float t)
{
    return 1.0f - powf(1.0f - t, 3.0f);
}

float ease_in_out_cubic(float t)
{
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

/* ============================================================================
 * RENDERIZAÇÃO DE TEXTO VIA CHILD SOURCES
 * ============================================================================
 *
 * O OBS não expõe gs_font_t nem funções gs_font_*. A forma oficial e correcta
 * de renderizar texto num plugin OBS é usar child sources do tipo:
 *   - text_gdiplus       (Windows / macOS)
 *   - text_ft2_source_v2 (Linux)
 *
 * As child sources são criadas e actualizadas em broadcast_update() (thread
 * principal). Aqui chamamos obs_source_video_render() com transforms de
 * posição para as desenhar no local correcto do overlay.
 */

/* Renderiza uma child source de texto na posição (x, y). */
static void draw_text_source(obs_source_t *src, float x, float y)
{
    if (!src) return;
    gs_matrix_push();
    gs_matrix_translate3f(x, y, 0.0f);
    obs_source_video_render(src);
    gs_matrix_pop();
}

/*
 * Retorna a largura da child source.
 * Após a primeira renderização, obs_source_get_width() devolve o valor real.
 * Antes disso (primeiro frame), usa estimativa tipográfica como fallback.
 */
static float get_src_width(obs_source_t *src, const char *text, int font_size)
{
    if (src) {
        uint32_t w = obs_source_get_width(src);
        if (w > 0) return (float)w;
    }
    if (!text || !*text) return 0.0f;
    /* Estimativa: ~0.55 × font_size × nº de caracteres */
    return (float)(int)strlen(text) * (float)font_size * 0.55f;
}

/*
 * Retorna a altura da child source.
 * Fallback: font_size × 1.2 (padrão tipográfico).
 */
static float get_src_height(obs_source_t *src, int font_size)
{
    if (src) {
        uint32_t h = obs_source_get_height(src);
        if (h > 0) return (float)h;
    }
    return (float)font_size * 1.2f;
}

/* ============================================================================
 * draw_rect
 * ============================================================================
 * Retângulo sólido via GS_TRISTRIP.
 */
void draw_rect(float x, float y, float w, float h, uint32_t color)
{
    uint32_t alpha = GET_ALPHA(color);
    uint32_t red   = GET_RED(color);
    uint32_t green = GET_GREEN(color);
    uint32_t blue  = GET_BLUE(color);

    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x,     y);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w, y);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x,     y + h);  gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w, y + h);  gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_rounded_rect
 * ============================================================================
 * Retângulo com cantos arredondados usando triângulos simples.
 * 9 draw calls no total: 1 centro + 2 faixas laterais + 4 cantos (3 segs cada).
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

    /* Centro */
    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x + radius,     y);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w - radius, y);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + radius,     y + h);  gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w - radius, y + h);  gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);

    /* Faixa esquerda */
    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x,          y + radius);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + radius, y + radius);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x,          y + h - radius);  gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + radius, y + h - radius);  gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);

    /* Faixa direita */
    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x + w - radius, y + radius);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w,          y + radius);      gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w - radius, y + h - radius);  gs_color4u(red, green, blue, alpha);
    gs_vertex2f(x + w,          y + h - radius);  gs_color4u(red, green, blue, alpha);
    gs_render_stop(GS_TRISTRIP);

    /* Cantos arredondados (3 segmentos por canto) */
    const int   segs = 3;
    const float step = (float)(M_PI / 2.0 / segs);

    auto corner = [&](float cx, float cy, float start_angle) {
        gs_render_start(GS_TRISTRIP);
        for (int i = 0; i < segs; i++) {
            float a0 = start_angle + (float)i       * step;
            float a1 = start_angle + (float)(i + 1) * step;
            gs_vertex2f(cx + cosf(a0) * radius, cy + sinf(a0) * radius);
            gs_color4u(red, green, blue, alpha);
            gs_vertex2f(cx + cosf(a1) * radius, cy + sinf(a1) * radius);
            gs_color4u(red, green, blue, alpha);
            gs_vertex2f(cx, cy);
            gs_color4u(red, green, blue, alpha);
        }
        gs_render_stop(GS_TRISTRIP);
    };

    corner(x + radius,         y + radius,         (float)M_PI);           /* top-left     */
    corner(x + w - radius,     y + radius,         (float)M_PI / 2.0f);    /* top-right    */
    corner(x + radius,         y + h - radius,     (float)M_PI * 1.5f);    /* bottom-left  */
    corner(x + w - radius,     y + h - radius,     0.0f);                  /* bottom-right */
}

/* ============================================================================
 * draw_gradient_rect
 * ============================================================================
 * Retângulo com gradiente horizontal via interpolação de vértices.
 */
void draw_gradient_rect(float x, float y, float w, float h,
                         uint32_t color_left, uint32_t color_right)
{
    uint32_t aL = GET_ALPHA(color_left),  rL = GET_RED(color_left),
             gL = GET_GREEN(color_left),  bL = GET_BLUE(color_left);
    uint32_t aR = GET_ALPHA(color_right), rR = GET_RED(color_right),
             gR = GET_GREEN(color_right), bR = GET_BLUE(color_right);

    gs_render_start(GS_TRISTRIP);
    gs_vertex2f(x,     y);      gs_color4u(rL, gL, bL, aL);
    gs_vertex2f(x + w, y);      gs_color4u(rR, gR, bR, aR);
    gs_vertex2f(x,     y + h);  gs_color4u(rL, gL, bL, aL);
    gs_vertex2f(x + w, y + h);  gs_color4u(rR, gR, bR, aR);
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_rect_glass
 * ============================================================================
 * Retângulo com efeito frosted glass via shader .effect.
 * Fallback para retângulo sólido se o shader não estiver carregado.
 */
void draw_rect_glass(BroadcastContext *ctx, float x, float y,
                      float w, float h, uint32_t color, bool vertical)
{
    if (!ctx || !ctx->glass_loaded || !ctx->effect_glass) {
        draw_rect(x, y, w, h, color);
        return;
    }

    float alpha = (float)GET_ALPHA(color) / 255.0f;
    float red   = (float)GET_RED(color)   / 255.0f;
    float green = (float)GET_GREEN(color) / 255.0f;
    float blue  = (float)GET_BLUE(color)  / 255.0f;

    struct vec4 color_vec = {red, green, blue, alpha};
    struct vec4 hl_vec    = {1.0f, 1.0f, 1.0f, 0.3f};

    if (ctx->ep_glass_color)     gs_effect_set_vec4 (ctx->ep_glass_color,     &color_vec);
    if (ctx->ep_glass_highlight) gs_effect_set_vec4 (ctx->ep_glass_highlight, &hl_vec);
    if (ctx->ep_glass_top)       gs_effect_set_float(ctx->ep_glass_top,       0.15f);
    if (ctx->ep_glass_bot)       gs_effect_set_float(ctx->ep_glass_bot,       0.85f);
    if (ctx->ep_glass_noise)     gs_effect_set_float(ctx->ep_glass_noise,     0.08f);
    if (ctx->ep_glass_tint)      gs_effect_set_float(ctx->ep_glass_tint,      0.25f);
    if (ctx->ep_glass_hl_width)  gs_effect_set_float(ctx->ep_glass_hl_width,  0.12f);

    const char *tech_name = vertical ? "VerticalGlass" : "HorizontalGlass";
    gs_technique_t *tech = gs_effect_get_technique(ctx->effect_glass, tech_name);
    if (!tech) {
        draw_rect(x, y, w, h, color);
        return;
    }

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);

    gs_render_start(GS_TRISTRIP);
    gs_texcoord2f(0.0f, 0.0f);  gs_vertex2f(x,     y);
    gs_texcoord2f(1.0f, 0.0f);  gs_vertex2f(x + w, y);
    gs_texcoord2f(0.0f, 1.0f);  gs_vertex2f(x,     y + h);
    gs_texcoord2f(1.0f, 1.0f);  gs_vertex2f(x + w, y + h);
    gs_render_stop(GS_TRISTRIP);

    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

/* ============================================================================
 * LOWER THIRD
 * ============================================================================
 * Barra semi-transparente na parte inferior com nome (bold 36pt) e
 * cargo (regular 24pt). Animação: slide entrada/saída + fade para centro.
 */
void render_lower_third(BroadcastContext *ctx)
{
    if (!ctx || !ctx->lt_name || strlen(ctx->lt_name) == 0) return;

    const float W      = (float)DEFAULT_SOURCE_WIDTH;
    const float H      = (float)DEFAULT_SOURCE_HEIGHT;
    const float bar_h  = (float)LOWER_THIRD_HEIGHT;
    const float side_w = 8.0f;
    const float pad    = 30.0f;
    const float mb     = 10.0f;
    const float y_base = H - bar_h - mb;
    const float tx_off = pad + side_w + 15.0f;

    float anim       = ease_out_cubic(ctx->lt_anim_progress);
    float alpha_mult = 1.0f;
    float slide_off  = 0.0f;

    if (ctx->lt_alignment == LT_ALIGN_LEFT) {
        slide_off = -(1.0f - anim) * (tx_off + 600.0f);
    } else if (ctx->lt_alignment == LT_ALIGN_RIGHT) {
        slide_off = (1.0f - anim) * (W - tx_off + 600.0f);
    } else {
        alpha_mult = anim;
    }

    float nw   = get_src_width(ctx->ts_lt_name,  ctx->lt_name,  36);
    float tw   = (ctx->lt_title && strlen(ctx->lt_title) > 0)
                 ? get_src_width(ctx->ts_lt_title, ctx->lt_title, 24) : 0.0f;
    float maxw = (nw > tw) ? nw : tw;
    float bgw  = maxw + pad * 2.0f + side_w + 20.0f;
    if (bgw > W) bgw = W;

    float bgx;
    switch (ctx->lt_alignment) {
        case LT_ALIGN_LEFT:   bgx = slide_off;          break;
        case LT_ALIGN_RIGHT:  bgx = W - bgw + slide_off; break;
        default:              bgx = (W - bgw) * 0.5f;   break;
    }

    /* Aplica alpha da animação à cor de fundo */
    uint32_t bg_color = ctx->color_bg;
    if (alpha_mult < 1.0f) {
        uint32_t a = (uint32_t)((float)GET_ALPHA(bg_color) * alpha_mult);
        bg_color = (a << 24) | (GET_RED(bg_color) << 16)
                 | (GET_GREEN(bg_color) << 8) | GET_BLUE(bg_color);
    }

    /* Fundo glass */
    draw_rect_glass(ctx, bgx, y_base, bgw, bar_h, bg_color, true);
    /* Barra lateral colorida */
    draw_rect(bgx, y_base, side_w, bar_h, ctx->color_primary);

    /* Texto: nome (bold) */
    draw_text_source(ctx->ts_lt_name, bgx + tx_off, y_base + 20.0f);

    /* Texto: cargo (regular) */
    if (ctx->lt_title && strlen(ctx->lt_title) > 0)
        draw_text_source(ctx->ts_lt_title, bgx + tx_off, y_base + 65.0f);
}

/* ============================================================================
 * GC (GERADOR DE CARACTERES)
 * ============================================================================
 * Texto dinâmico centralizado. Tamanho de fonte ajustado ao comprimento
 * do texto em broadcast_update() e guardado em ctx->gc_font_size.
 */
void render_gc(BroadcastContext *ctx)
{
    if (!ctx || !ctx->gc_text || strlen(ctx->gc_text) == 0) return;

    const float W  = (float)DEFAULT_SOURCE_WIDTH;
    const float H  = (float)DEFAULT_SOURCE_HEIGHT;
    const int   fs = ctx->gc_font_size;

    float tw = get_src_width (ctx->ts_gc, ctx->gc_text, fs);
    float th = get_src_height(ctx->ts_gc, fs);

    float x  = (W - tw) * 0.5f;
    float y  = (H - th) * 0.5f;
    float bp = 30.0f;

    draw_rect(x - bp, y - bp, tw + bp * 2.0f, th + bp * 2.0f, ctx->color_bg);
    draw_text_source(ctx->ts_gc, x, y);
}

/* ============================================================================
 * TICKER (RODAPÉ)
 * ============================================================================
 * Texto corrido estilo news ticker com loop infinito.
 * O texto é renderizado até 3 vezes por frame para cobrir o wrap.
 */
void render_ticker(BroadcastContext *ctx)
{
    if (!ctx || !ctx->ticker_text || strlen(ctx->ticker_text) == 0) return;

    const float W   = (float)DEFAULT_SOURCE_WIDTH;
    const float H   = (float)DEFAULT_SOURCE_HEIGHT;
    const float bh  = (float)TICKER_BAR_HEIGHT;
    const int   fs  = 24;
    const float pad = 50.0f;

    float tw  = get_src_width (ctx->ts_ticker, ctx->ticker_text, fs);
    float uw  = tw + pad;
    float off = (uw > 0.0f) ? fmodf(ctx->ticker_offset, uw) : 0.0f;

    /* Barra de fundo + linha de cor */
    draw_rect(0.0f, H - bh, W, bh, COLOR_TICKER_BG);
    draw_rect(0.0f, H - bh, W, 3.0f, ctx->color_primary);

    float th = get_src_height(ctx->ts_ticker, fs);
    float yt = H - bh + (bh - th) * 0.5f;

    /* Primeira instância (posição actual) */
    float x1 = W - off;
    draw_text_source(ctx->ts_ticker, x1, yt);

    /* Segunda instância (imediatamente à esquerda — wrap) */
    float x2 = x1 - uw;
    if (x2 + tw > 0.0f)
        draw_text_source(ctx->ts_ticker, x2, yt);

    /* Terceira instância (imediatamente à direita — fill) */
    float x3 = x1 + uw;
    if (x3 < W)
        draw_text_source(ctx->ts_ticker, x3, yt);
}

/* ============================================================================
 * REDES SOCIAIS
 * ============================================================================
 * Painel lateral com as redes activas. Tags em bold 14pt + handles 20pt.
 * Posição configurável (4 cantos).
 */
void render_social_media(BroadcastContext *ctx)
{
    if (!ctx) return;

    /* Indexa apenas as redes com handle preenchido */
    struct SocialItem { int idx; const char *handle; };
    SocialItem items[4];
    int count = 0;

    const char *handles[4] = {
        ctx->instagram, ctx->tiktok, ctx->facebook, ctx->youtube
    };
    for (int i = 0; i < 4; i++) {
        if (handles[i] && strlen(handles[i]) > 0) {
            items[count].idx    = i;
            items[count].handle = handles[i];
            count++;
        }
    }
    if (count == 0) return;

    /* Tags partilhadas do ficheiro broadcast-source.cpp */
    const int   fs_tag    = 14;
    const int   fs_handle = 20;
    const float ih  = 36.0f;   /* altura por item */
    const float px  = 20.0f;   /* padding horizontal */
    const float py  = 12.0f;   /* padding vertical */
    const float gp  = 4.0f;    /* gap entre items */
    const float mg  = 20.0f;   /* margem exterior */

    /* Calcula largura máxima do painel */
    float panel_w = 0.0f, panel_h = 0.0f;
    for (int i = 0; i < count; i++) {
        int j        = items[i].idx;
        float tag_w  = get_src_width(ctx->ts_social_tag[j],    SOCIAL_TAGS[j],  fs_tag);
        float hdl_w  = get_src_width(ctx->ts_social_handle[j], items[i].handle, fs_handle);
        float iw     = tag_w + 8.0f + hdl_w + px * 2.0f;
        if (iw > panel_w) panel_w = iw;
        panel_h += ih + gp;
    }
    panel_h -= gp;
    panel_h += py * 2.0f;
    if (panel_w < 180.0f) panel_w = 180.0f;

    const float W = (float)DEFAULT_SOURCE_WIDTH;
    const float H = (float)DEFAULT_SOURCE_HEIGHT;

    float bx, by;
    switch (ctx->social_position) {
        case SOCIAL_TOP_LEFT:
            bx = mg;             by = mg; break;
        case SOCIAL_TOP_RIGHT:
            bx = W - panel_w - mg; by = mg; break;
        case SOCIAL_BOTTOM_LEFT:
            bx = mg;             by = H - panel_h - mg - TICKER_BAR_HEIGHT - 10.0f; break;
        default: /* SOCIAL_BOTTOM_RIGHT */
            bx = W - panel_w - mg; by = H - panel_h - mg - TICKER_BAR_HEIGHT - 10.0f; break;
    }

    /* Fundo glass (horizontal) */
    draw_rect_glass(ctx, bx, by, panel_w, panel_h, ctx->color_bg, false);

    float cy = by + py;
    for (int i = 0; i < count; i++) {
        int   j   = items[i].idx;
        float tx  = bx + px;
        float ty  = cy + (ih - get_src_height(ctx->ts_social_handle[j], fs_handle)) * 0.5f;

        /* Tag em bold */
        draw_text_source(ctx->ts_social_tag[j], tx, ty + 2.0f);

        /* Handle em regular, à direita da tag */
        float tag_w = get_src_width(ctx->ts_social_tag[j], tags[j], fs_tag);
        draw_text_source(ctx->ts_social_handle[j], tx + tag_w + 8.0f, ty);

        /* Separador (excepto no último item) */
        if (i < count - 1)
            draw_rect(bx + 10.0f, cy + ih, panel_w - 20.0f, 1.0f, 0x33FFFFFF);

        cy += ih + gp;
    }
}
