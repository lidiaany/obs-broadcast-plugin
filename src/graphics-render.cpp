/* ============================================================================
 * graphics-render.cpp — Broadcast Overlay System (Renderizacao)
 *
 * Implementa toda a renderizacao grafica dos componentes do overlay.
 *
 * TEXTO: renderizado via child sources do OBS.
 * GEOMETRIA: retangulos solidos, gradientes e cantos arredondados usam a
 * API grafics.h do libobs (OBS 31+):
 *   gs_render_start(bool), gs_vertex2f(), gs_color(uint32_t), gs_render_stop()
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
 * FUNCOES DE EASING
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
 * RENDERIZACAO DE TEXTO VIA CHILD SOURCES
 * ============================================================================
 */

static void draw_text_source(obs_source_t *src, float x, float y)
{
    if (!src) return;
    gs_matrix_push();
    gs_matrix_translate3f(x, y, 0.0f);
    obs_source_video_render(src);
    gs_matrix_pop();
}

static float get_src_width(obs_source_t *src, const char *text, int font_size)
{
    if (src) {
        uint32_t w = obs_source_get_width(src);
        if (w > 0) return (float)w;
    }
    if (!text || !*text) return 0.0f;
    return (float)(int)strlen(text) * (float)font_size * 0.55f;
}

static float get_src_height(obs_source_t *src, int font_size)
{
    if (src) {
        uint32_t h = obs_source_get_height(src);
        if (h > 0) return (float)h;
    }
    return (float)font_size * 1.2f;
}

/* Aplica opacidade (0.0-1.0) a uma cor ARGB. */
static uint32_t apply_opacity(uint32_t color, float mult)
{
    if (mult >= 1.0f) return color;
    if (mult <= 0.0f) return 0;
    uint32_t a = (uint32_t)((float)GET_ALPHA(color) * mult);
    return (a << 24) | (GET_RED(color) << 16)
         | (GET_GREEN(color) << 8) | GET_BLUE(color);
}

/* ============================================================================
 * draw_rect
 * ============================================================================
 * Retangulo solido via gs_render_start/stop (OBS 31 API).
 */
void draw_rect(float x, float y, float w, float h, uint32_t color)
{
    gs_render_start(true);
    gs_vertex2f(x,     y);      gs_color(color);
    gs_vertex2f(x + w, y);      gs_color(color);
    gs_vertex2f(x,     y + h);  gs_color(color);
    gs_vertex2f(x + w, y + h);  gs_color(color);
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_rounded_rect
 * ============================================================================
 */
void draw_rounded_rect(float x, float y, float w, float h,
                        float radius, uint32_t color)
{
    if (radius <= 1.0f || radius > w / 2.0f || radius > h / 2.0f) {
        draw_rect(x, y, w, h, color);
        return;
    }

    /* Centro */
    gs_render_start(true);
    gs_vertex2f(x + radius,     y);      gs_color(color);
    gs_vertex2f(x + w - radius, y);      gs_color(color);
    gs_vertex2f(x + radius,     y + h);  gs_color(color);
    gs_vertex2f(x + w - radius, y + h);  gs_color(color);
    gs_render_stop(GS_TRISTRIP);

    /* Faixa esquerda */
    gs_render_start(true);
    gs_vertex2f(x,          y + radius);      gs_color(color);
    gs_vertex2f(x + radius, y + radius);      gs_color(color);
    gs_vertex2f(x,          y + h - radius);  gs_color(color);
    gs_vertex2f(x + radius, y + h - radius);  gs_color(color);
    gs_render_stop(GS_TRISTRIP);

    /* Faixa direita */
    gs_render_start(true);
    gs_vertex2f(x + w - radius, y + radius);      gs_color(color);
    gs_vertex2f(x + w,          y + radius);      gs_color(color);
    gs_vertex2f(x + w - radius, y + h - radius);  gs_color(color);
    gs_vertex2f(x + w,          y + h - radius);  gs_color(color);
    gs_render_stop(GS_TRISTRIP);

    /* Cantos arredondados (3 segmentos por canto) */
    const int   segs = 3;
    const float step = (float)(M_PI / 2.0 / segs);

    auto corner = [&](float cx, float cy, float start_angle) {
        gs_render_start(true);
        for (int i = 0; i < segs; i++) {
            float a0 = start_angle + (float)i       * step;
            float a1 = start_angle + (float)(i + 1) * step;
            gs_vertex2f(cx + cosf(a0) * radius, cy + sinf(a0) * radius);
            gs_color(color);
            gs_vertex2f(cx + cosf(a1) * radius, cy + sinf(a1) * radius);
            gs_color(color);
            gs_vertex2f(cx, cy);
            gs_color(color);
        }
        gs_render_stop(GS_TRISTRIP);
    };

    corner(x + radius,         y + radius,         (float)M_PI);
    corner(x + w - radius,     y + radius,         (float)M_PI / 2.0f);
    corner(x + radius,         y + h - radius,     (float)M_PI * 1.5f);
    corner(x + w - radius,     y + h - radius,     0.0f);
}

/* ============================================================================
 * draw_gradient_rect
 * ============================================================================
 */
void draw_gradient_rect(float x, float y, float w, float h,
                         uint32_t color_left, uint32_t color_right)
{
    gs_render_start(true);
    gs_vertex2f(x,     y);      gs_color(color_left);
    gs_vertex2f(x + w, y);      gs_color(color_right);
    gs_vertex2f(x,     y + h);  gs_color(color_left);
    gs_vertex2f(x + w, y + h);  gs_color(color_right);
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_rect_glass
 * ============================================================================
 * Efeito frosted glass via shader .effect. Fallback para solido se falhar.
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

    gs_render_start(true);
    gs_texcoord(0.0f, 0.0f, 0);  gs_vertex2f(x,     y);
    gs_texcoord(1.0f, 0.0f, 0);  gs_vertex2f(x + w, y);
    gs_texcoord(0.0f, 1.0f, 0);  gs_vertex2f(x,     y + h);
    gs_texcoord(1.0f, 1.0f, 0);  gs_vertex2f(x + w, y + h);
    gs_render_stop(GS_TRISTRIP);

    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

/* ============================================================================
 * LOWER THIRD
 * ============================================================================
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

    /* Determina tipo de animacao activo */
    bool lt_anim_active = (ctx->lt_anim_state != 0.0f || ctx->lt_anim_progress < 1.0f);
    const char *lt_type = "slide";
    if (lt_anim_active && ctx->lt_anim_type && *ctx->lt_anim_type)
        lt_type = ctx->lt_anim_type;

    float alpha_mult = 1.0f;
    float slide_off  = 0.0f;
    float scale_s    = 1.0f;

    gs_matrix_push();

    if (strcmp(lt_type, "fade") == 0) {
        /* Fade: opacidade cresce com o progresso */
        alpha_mult = anim;

    } else if (strcmp(lt_type, "scale") == 0) {
        /* Scale: escala do centro da barra */
        float nw   = get_src_width(ctx->ts_lt_name,  ctx->lt_name,  36);
        float tw   = (ctx->lt_title && strlen(ctx->lt_title) > 0)
                     ? get_src_width(ctx->ts_lt_title, ctx->lt_title, 24) : 0.0f;
        float maxw = (nw > tw) ? nw : tw;
        float bgw  = maxw + pad * 2.0f + side_w + 20.0f;
        if (bgw > W) bgw = W;

        float bgx;
        switch (ctx->lt_alignment) {
            case LT_ALIGN_LEFT:   bgx = 0.0f;            break;
            case LT_ALIGN_RIGHT:  bgx = W - bgw;         break;
            default:              bgx = (W - bgw) * 0.5f; break;
        }
        float cx = bgx + bgw * 0.5f;
        float cy = y_base + bar_h * 0.5f;
        scale_s = 0.3f + 0.7f * anim;
        gs_matrix_translate3f(cx, cy, 0.0f);
        gs_matrix_scale3f(scale_s, scale_s, 1.0f);
        gs_matrix_translate3f(-cx, -cy, 0.0f);

    } else {
        /* Slide (padrao): desliza da borda conforme alinhamento */
        if (ctx->lt_alignment == LT_ALIGN_LEFT) {
            slide_off = -(1.0f - anim) * (tx_off + 600.0f);
        } else if (ctx->lt_alignment == LT_ALIGN_RIGHT) {
            slide_off = (1.0f - anim) * (W - tx_off + 600.0f);
        } else {
            alpha_mult = anim;
        }
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

    float lt_opacity = ctx->opacity_global * ctx->opacity_lt;

    uint32_t bg_color = ctx->color_bg;
    float total_alpha_mult = alpha_mult * lt_opacity;
    if (total_alpha_mult < 1.0f) {
        uint32_t a = (uint32_t)((float)GET_ALPHA(bg_color) * total_alpha_mult);
        bg_color = (a << 24) | (GET_RED(bg_color) << 16)
                 | (GET_GREEN(bg_color) << 8) | GET_BLUE(bg_color);
    }

    draw_rect_glass(ctx, bgx, y_base, bgw, bar_h, bg_color, true);
    uint32_t bar_color = apply_opacity(ctx->color_primary, lt_opacity);
    draw_rect(bgx, y_base, side_w, bar_h, bar_color);

    draw_text_source(ctx->ts_lt_name, bgx + tx_off, y_base + 20.0f);
    if (ctx->lt_title && strlen(ctx->lt_title) > 0)
        draw_text_source(ctx->ts_lt_title, bgx + tx_off, y_base + 65.0f);

    gs_matrix_pop();
}

/* ============================================================================
 * GC (GERADOR DE CARACTERES)
 * ============================================================================
 */
void render_gc(BroadcastContext *ctx)
{
    if (!ctx || !ctx->gc_text || strlen(ctx->gc_text) == 0) return;

    const float W  = (float)DEFAULT_SOURCE_WIDTH;
    const float H  = (float)DEFAULT_SOURCE_HEIGHT;
    const int   fs = ctx->gc_font_size;

    float tw = get_src_width (ctx->ts_gc, ctx->gc_text, fs);
    float th = get_src_height(ctx->ts_gc, fs);

    float cx = (W - tw) * 0.5f;
    float cy = (H - th) * 0.5f;
    float bp = 30.0f;

    float anim_progress = ease_out_cubic(ctx->gc_anim_progress);
    bool gc_anim_active = (ctx->gc_anim_state != 0 || ctx->gc_anim_progress > 0.0f);
    const char *gc_type = gc_anim_active ? (ctx->gc_anim_type ? ctx->gc_anim_type : "fade") : "";

    gs_matrix_push();

    if (gc_anim_active) {
        if (strcmp(gc_type, "scale") == 0) {
            float s = 0.3f + 0.7f * anim_progress;
            gs_matrix_translate3f(cx + tw * 0.5f, cy + th * 0.5f, 0.0f);
            gs_matrix_scale3f(s, s, 1.0f);
            gs_matrix_translate3f(-(cx + tw * 0.5f), -(cy + th * 0.5f), 0.0f);
        } else if (strcmp(gc_type, "slide") == 0) {
            float slide_dist = 200.0f * (1.0f - anim_progress);
            const char *dir = ctx->gc_anim_dir ? ctx->gc_anim_dir : "up";
            float sx = 0.0f, sy = 0.0f;
            if (strcmp(dir, "up") == 0)        sy =  slide_dist;
            else if (strcmp(dir, "down") == 0)  sy = -slide_dist;
            else if (strcmp(dir, "left") == 0)  sx =  slide_dist;
            else if (strcmp(dir, "right") == 0) sx = -slide_dist;
            gs_matrix_translate3f(sx, sy, 0.0f);
        }
    }

    float gc_opacity = ctx->opacity_global * ctx->opacity_gc;
    if (gc_anim_active && strcmp(gc_type, "fade") == 0) {
        float fade_alpha = (anim_progress < 0.01f) ? 0.0f : anim_progress;
        gc_opacity *= fade_alpha;
    }
    uint32_t bg_color = apply_opacity(ctx->color_bg, gc_opacity);

    draw_rect(cx - bp, cy - bp, tw + bp * 2.0f, th + bp * 2.0f, bg_color);
    draw_text_source(ctx->ts_gc, cx, cy);

    gs_matrix_pop();
}

/* ============================================================================
 * TICKER (RODAPE)
 * ============================================================================
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

    float tk_opacity = ctx->opacity_global * ctx->opacity_ticker;
    uint32_t ticker_bg = apply_opacity(COLOR_TICKER_BG, tk_opacity);
    uint32_t line_color = apply_opacity(ctx->color_primary, tk_opacity);

    draw_rect(0.0f, H - bh, W, bh, ticker_bg);
    draw_rect(0.0f, H - bh, W, 3.0f, line_color);

    float th = get_src_height(ctx->ts_ticker, fs);
    float yt = H - bh + (bh - th) * 0.5f;

    float x1 = W - off;
    draw_text_source(ctx->ts_ticker, x1, yt);

    float x2 = x1 - uw;
    if (x2 + tw > 0.0f)
        draw_text_source(ctx->ts_ticker, x2, yt);

    float x3 = x1 + uw;
    if (x3 < W)
        draw_text_source(ctx->ts_ticker, x3, yt);
}

/* ============================================================================
 * REDES SOCIAIS
 * ============================================================================
 */
void render_social_media(BroadcastContext *ctx)
{
    if (!ctx) return;

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

    const int   fs_tag    = 14;
    const int   fs_handle = 20;
    const float ih  = 36.0f;
    const float px  = 20.0f;
    const float py  = 12.0f;
    const float gp  = 4.0f;
    const float mg  = 20.0f;

    float soc_opacity = ctx->opacity_global * ctx->opacity_social;

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
        default:
            bx = W - panel_w - mg; by = H - panel_h - mg - TICKER_BAR_HEIGHT - 10.0f; break;
    }

    uint32_t soc_bg = apply_opacity(ctx->color_bg, soc_opacity);
    draw_rect_glass(ctx, bx, by, panel_w, panel_h, soc_bg, false);

    float cy = by + py;
    for (int i = 0; i < count; i++) {
        int   j   = items[i].idx;
        float tx  = bx + px;
        float ty  = cy + (ih - get_src_height(ctx->ts_social_handle[j], fs_handle)) * 0.5f;

        draw_text_source(ctx->ts_social_tag[j], tx, ty + 2.0f);

        float tag_w = get_src_width(ctx->ts_social_tag[j], SOCIAL_TAGS[j], fs_tag);
        draw_text_source(ctx->ts_social_handle[j], tx + tag_w + 8.0f, ty);

        if (i < count - 1) {
            uint32_t sep_color = apply_opacity(0x33FFFFFF, soc_opacity);
            draw_rect(bx + 10.0f, cy + ih, panel_w - 20.0f, 1.0f, sep_color);
        }

        cy += ih + gp;
    }
}
