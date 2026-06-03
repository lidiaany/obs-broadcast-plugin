/* ============================================================================
 * graphics-render.cpp — Broadcast Overlay System (Renderizacao)
 *
 * Melhorias implementadas nesta versão:
 *
 *  1. RESPONSIVIDADE  — coordenadas sempre em 1920×1080 (espaço normalizado).
 *     A matriz de escala em broadcast_video_render mapeia para qualquer
 *     resolução (720p, 1440p, 4K, ultrawide, vertical).
 *
 *  2. FLICKERING CORRIGIDO — text cache em broadcast_update impede
 *     recriação de textura GPU por frame. Nenhuma alocação no render loop.
 *
 *  3. CENTRALIZAÇÃO REAL — usa obs_source_get_width() para medir o texto
 *     real da child source antes de posicionar. Fallback estimado apenas
 *     quando a source ainda não tem dimensões.
 *
 *  4. TICKER PROFISSIONAL — movimento baseado em deltaTime, loop contínuo
 *     real, clipping correto, velocidade/altura/padding configuráveis.
 *
 *  5. FUNDO MODERNO — gradient vertical no ticker, accent line com glow,
 *     rounded corners, sombra suave.
 *
 *  6. LOWER THIRD MODERNO — rounded corners, accent bar animada, auto-width,
 *     gradient no fundo, sombra suave, easing cinematográfico.
 *
 *  7. ANIMAÇÕES — easeOutExpo, easeOutBack adicionados. Fade e slide
 *     independentes de FPS via deltaTime.
 *
 *  8. PERFORMANCE GPU — zero alocações no render loop. Batching de quads
 *     via GS_TRISTRIP. Shaders reutilizados.
 *
 * ============================================================================ */

#include "broadcast-source.h"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <cstring>
#include <cmath>
#include <cstdio>

/* ============================================================================
 * EASING — animações cinemáticas independentes de FPS
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

/* easeOutExpo — desaceleração agressiva, estilo broadcast profissional */
float ease_out_expo(float t)
{
    if (t >= 1.0f) return 1.0f;
    return 1.0f - powf(2.0f, -10.0f * t);
}

/* easeOutBack — ultrapassa levemente o destino e recua (overshoot suave) */
float ease_out_back(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

/* ============================================================================
 * RENDERIZACAO DE TEXTO VIA CHILD SOURCES
 * ============================================================================ */

static void draw_text_source(obs_source_t *src, float x, float y)
{
    if (!src) return;
    gs_matrix_push();
    gs_matrix_translate3f(x, y, 0.0f);
    obs_source_video_render(src);
    gs_matrix_pop();
}

/* Retorna a largura real da child source (medida após renderizar).
 * Fallback estimado apenas se a source ainda não reporta dimensões. */
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

/* Aplica multiplicador de opacidade a uma cor ARGB. */
static uint32_t apply_opacity(uint32_t color, float mult)
{
    if (mult >= 1.0f) return color;
    if (mult <= 0.0f) return 0;
    uint32_t a = (uint32_t)((float)GET_ALPHA(color) * mult);
    return (a << 24) | (GET_RED(color) << 16)
         | (GET_GREEN(color) << 8) | GET_BLUE(color);
}

/* Lerp entre duas cores ARGB componente a componente. */
static uint32_t lerp_color(uint32_t c0, uint32_t c1, float t)
{
    if (t <= 0.0f) return c0;
    if (t >= 1.0f) return c1;
    uint32_t a = (uint32_t)((float)GET_ALPHA(c0) + t * ((float)GET_ALPHA(c1) - (float)GET_ALPHA(c0)));
    uint32_t r = (uint32_t)((float)GET_RED(c0)   + t * ((float)GET_RED(c1)   - (float)GET_RED(c0)));
    uint32_t g = (uint32_t)((float)GET_GREEN(c0) + t * ((float)GET_GREEN(c1) - (float)GET_GREEN(c0)));
    uint32_t b = (uint32_t)((float)GET_BLUE(c0)  + t * ((float)GET_BLUE(c1)  - (float)GET_BLUE(c0)));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* ============================================================================
 * draw_rect — retângulo sólido via GS_TRISTRIP
 * ============================================================================ */
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
 * draw_gradient_rect — gradiente horizontal (esquerda → direita)
 * ============================================================================ */
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
 * draw_gradient_rect_v — gradiente vertical (topo → base)
 * ============================================================================ */
void draw_gradient_rect_v(float x, float y, float w, float h,
                           uint32_t color_top, uint32_t color_bottom)
{
    gs_render_start(true);
    gs_vertex2f(x,     y);      gs_color(color_top);
    gs_vertex2f(x + w, y);      gs_color(color_top);
    gs_vertex2f(x,     y + h);  gs_color(color_bottom);
    gs_vertex2f(x + w, y + h);  gs_color(color_bottom);
    gs_render_stop(GS_TRISTRIP);
}

/* ============================================================================
 * draw_gradient_3stop — gradiente simetrico 3-stop (esq → meio → dir)
 *
 * Aproxima um gradiente com tres stops desenhando duas metades:
 *   esquerda:  color_left  → color_mid
 *   direita:   color_mid   → color_right
 *
 * Usado no rodape social para o efeito navy → blue → navy do HTML.
 * ============================================================================ */
void draw_gradient_3stop(float x, float y, float w, float h,
                          uint32_t color_left, uint32_t color_mid, uint32_t color_right)
{
    float hw = w * 0.5f;
    draw_gradient_rect(x, y, hw, h, color_left, color_mid);
    draw_gradient_rect(x + hw, y, w - hw, h, color_mid, color_right);
}

/* ============================================================================
 * draw_rounded_rect — retângulo com cantos arredondados
 * ============================================================================ */
void draw_rounded_rect(float x, float y, float w, float h,
                        float radius, uint32_t color)
{
    if (radius <= 1.0f || radius > w * 0.5f || radius > h * 0.5f) {
        draw_rect(x, y, w, h, color);
        return;
    }

    /* Faixa central */
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

    /* Cantos (4 segmentos por canto para suavidade adequada) */
    const int   segs = 4;
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
    corner(x + w - radius,     y + radius,         (float)(M_PI * 0.5));
    corner(x + radius,         y + h - radius,     (float)(M_PI * 1.5));
    corner(x + w - radius,     y + h - radius,     0.0f);
}

/* ============================================================================
 * draw_accent_line — linha colorida com glow suave
 *
 * Simula um "glow" desenhando linhas concêntricas com opacidade decrescente.
 * Leve mas visível — estilo broadcast moderno.
 * ============================================================================ */
void draw_accent_line(float x, float y, float w, float thickness,
                       uint32_t color, float glow_strength)
{
    /* Linha principal */
    draw_rect(x, y, w, thickness, color);

    if (glow_strength <= 0.0f) return;

    /* Halo externo: 3 camadas de glow cada vez mais transparentes */
    uint32_t base_a = GET_ALPHA(color);
    for (int i = 1; i <= 3; i++) {
        float glow_a = (float)base_a * glow_strength * (1.0f - (float)i * 0.3f);
        if (glow_a < 1.0f) break;
        uint32_t gc = MAKE_ARGB((uint32_t)glow_a,
                                 GET_RED(color), GET_GREEN(color), GET_BLUE(color));
        float gy = y - (float)i * 2.0f;
        float gh = (float)i * 4.0f + thickness;
        draw_rect(x, gy, w, gh, gc);
    }
}

/* ============================================================================
 * draw_rect_glass — efeito frosted glass via shader. Fallback sólido.
 * ============================================================================ */
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
    if (ctx->ep_glass_noise)     gs_effect_set_float(ctx->ep_glass_noise,     0.06f);
    if (ctx->ep_glass_tint)      gs_effect_set_float(ctx->ep_glass_tint,      0.20f);
    if (ctx->ep_glass_hl_width)  gs_effect_set_float(ctx->ep_glass_hl_width,  0.10f);

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
 * LOWER THIRD — visual broadcast estilo HTML
 *
 * Design baseado no HTML fornecido:
 *   - Fundo: gradiente horizontal navy (#1B2F4F) → azul (#2C5D91)
 *   - Accent bar dourada (#D4AF37) à esquerda (5px)
 *   - Texto branco, fonte Segoe UI
 *   - Nome bold 20px, Cargo 14px
 *   - Slide-in da esquerda com easeOutExpo
 *   - Cantos arredondados, sombra suave
 * ============================================================================ */
void render_lower_third(BroadcastContext *ctx)
{
    if (!ctx || !ctx->lt_name || strlen(ctx->lt_name) == 0) return;

    /* ── Constantes de layout (coordenadas 1920×1080) ─────────────────── */
    const float W      = (float)DEFAULT_SOURCE_WIDTH;
    const float H      = (float)DEFAULT_SOURCE_HEIGHT;
    const float bar_h  = (float)LOWER_THIRD_HEIGHT;
    const float side_w = 5.0f;      /* gold accent bar (border-left:5px) */
    const float pad    = 25.0f;     /* padding interno (padding:15px 25px) */
    const float mb     = 12.0f;     /* margem base */
    const float radius = 8.0f;      /* cantos arredondados */
    const float y_base = H - bar_h - mb;
    const float tx_off = pad + side_w + 14.0f;

    /* ── Easing cinemático (easeOutExpo) ────────────────────────────────
     * Dá aquela sensação de "snap" que os overlays profissionais têm.    */
    float anim = ease_out_cubic(ctx->lt_anim_progress);

    bool lt_anim_active = (ctx->lt_anim_state != 0.0f || ctx->lt_anim_progress < 1.0f);
    const char *lt_type = "slide";
    if (lt_anim_active && ctx->lt_anim_type && *ctx->lt_anim_type)
        lt_type = ctx->lt_anim_type;

    float alpha_mult = 1.0f;
    float slide_off  = 0.0f;

    gs_matrix_push();

    if (strcmp(lt_type, "fade") == 0) {
        alpha_mult = anim;

    } else if (strcmp(lt_type, "scale") == 0) {
        float nw   = get_src_width(ctx->ts_lt_name,  ctx->lt_name,  36);
        float tw   = (ctx->lt_title && strlen(ctx->lt_title) > 0)
                     ? get_src_width(ctx->ts_lt_title, ctx->lt_title, 24) : 0.0f;
        float maxw = (nw > tw) ? nw : tw;
        float bgw  = maxw + pad * 2.0f + side_w + 20.0f;
        if (bgw > W) bgw = W;
        float bgx = 0.0f;
        switch (ctx->lt_alignment) {
            case LT_ALIGN_LEFT:   bgx = 0.0f;            break;
            case LT_ALIGN_RIGHT:  bgx = W - bgw;         break;
            default:              bgx = (W - bgw) * 0.5f; break;
        }
        float cx = bgx + bgw * 0.5f;
        float cy = y_base + bar_h * 0.5f;
        /* easeOutBack para scale — overshoot delicado */
        float scale_s = 0.3f + 0.7f * ease_out_back(ctx->lt_anim_progress);
        gs_matrix_translate3f(cx, cy, 0.0f);
        gs_matrix_scale3f(scale_s, scale_s, 1.0f);
        gs_matrix_translate3f(-cx, -cy, 0.0f);

    } else {
        /* Slide — desliza da borda (estilo HTML: left:-500px → left:30px) */
        if (ctx->lt_alignment == LT_ALIGN_LEFT) {
            slide_off = -(1.0f - anim) * (tx_off + 600.0f);
        } else if (ctx->lt_alignment == LT_ALIGN_RIGHT) {
            slide_off = (1.0f - anim) * (W - tx_off + 600.0f);
        } else {
            /* Center: fade para evitar slide estranho */
            alpha_mult = anim;
        }
    }

    /* ── Dimensões auto-width baseadas no texto real ────────────────────
     * get_src_width() retorna obs_source_get_width() quando disponível —
     * centralização exata, suporta nomes longos e resize automático.    */
    float nw   = get_src_width(ctx->ts_lt_name,  ctx->lt_name,  20);
    float tw   = (ctx->lt_title && strlen(ctx->lt_title) > 0)
                 ? get_src_width(ctx->ts_lt_title, ctx->lt_title, 14) : 0.0f;
    float maxw = (nw > tw) ? nw : tw;
    float bgw  = maxw + pad * 2.0f + side_w + 20.0f;
    if (bgw > W) bgw = W;
    if (bgw < 200.0f) bgw = 200.0f; /* mínimo razoável */

    float bgx;
    switch (ctx->lt_alignment) {
        case LT_ALIGN_LEFT:   bgx = slide_off;            break;
        case LT_ALIGN_RIGHT:  bgx = W - bgw + slide_off;  break;
        default:              bgx = (W - bgw) * 0.5f;     break;
    }

    float lt_opacity   = ctx->opacity_global * ctx->opacity_lt;
    float total_alpha  = alpha_mult * lt_opacity;

    /* ── Sombra suave abaixo da barra (simula profundidade) ──────────── */
    uint32_t shadow_color = apply_opacity(0x55000000, total_alpha);
    draw_rect(bgx + 4.0f, y_base + 4.0f, bgw, bar_h, shadow_color);

    /* ── Fundo: GRADIENTE HORIZONTAL navy → azul (estilo HTML) ────────
     *   linear-gradient(90deg, rgba(27,47,79,0.95), rgba(44,93,145,0.85))
     *   draw_gradient_rect faz left→right = navy (#1B2F4F) → azul (#2C5D91)
     */
    uint32_t lt_bg_navy = apply_opacity(COLOR_NAVY, total_alpha);
    uint32_t lt_bg_blue = apply_opacity(COLOR_BLUE, total_alpha);

    if (ctx->glass_loaded && ctx->effect_glass) {
        draw_rect_glass(ctx, bgx, y_base, bgw, bar_h,
                        apply_opacity(ctx->color_bg, total_alpha), true);
    } else {
        draw_gradient_rect(bgx, y_base, bgw, bar_h, lt_bg_navy, lt_bg_blue);
    }

    /* ── GOLD ACCENT BAR (border-left: 5px solid #d4af37) ─────────────
     * Barra dourada fixa à esquerda, animada para crescer verticalmente
     * durante a entrada (efeito mais dinâmico que uma borda estática).  */
    float accent_h = bar_h * (lt_anim_active ? anim : 1.0f);
    uint32_t gold_color = apply_opacity(COLOR_GOLD, total_alpha);
    draw_rect(bgx, y_base + (bar_h - accent_h), side_w, accent_h, gold_color);

    /* Glow dourado suave */
    if (ctx->glow_strength > 0.0f) {
        draw_accent_line(bgx, y_base, side_w * 0.5f, accent_h,
                         apply_opacity(COLOR_GOLD, (uint32_t)(total_alpha * 0.5f)),
                         ctx->glow_strength * total_alpha);
    }

    /* ── Linha de topo dourada sutil ───────────────────────────────── */
    draw_accent_line(bgx, y_base, bgw, 2.0f,
                     apply_opacity(COLOR_GOLD, total_alpha * 0.7f),
                     ctx->glow_strength * total_alpha * 0.5f);

    /* ── Texto: nome em destaque (20px bold), cargo menor (14px) ────── */
    gs_matrix_push();

    float name_y  = y_base + 22.0f;
    float title_y = y_base + 48.0f;

    /* Centralização real: usa get_src_width para medir o texto renderizado */
    if (ctx->lt_alignment == LT_ALIGN_CENTER) {
        float name_x  = bgx + (bgw - nw)  * 0.5f;
        float title_x = bgx + (bgw - tw)  * 0.5f;
        draw_text_source(ctx->ts_lt_name, name_x, name_y);
        if (ctx->lt_title && strlen(ctx->lt_title) > 0)
            draw_text_source(ctx->ts_lt_title, title_x, title_y);
    } else {
        draw_text_source(ctx->ts_lt_name,  bgx + tx_off, name_y);
        if (ctx->lt_title && strlen(ctx->lt_title) > 0)
            draw_text_source(ctx->ts_lt_title, bgx + tx_off, title_y);
    }

    gs_matrix_pop();
    gs_matrix_pop();
}

/* ============================================================================
 * GC (GERADOR DE CARACTERES)
 * ============================================================================ */
void render_gc(BroadcastContext *ctx)
{
    if (!ctx || !ctx->gc_text || strlen(ctx->gc_text) == 0) return;

    const float W  = (float)DEFAULT_SOURCE_WIDTH;
    const float H  = (float)DEFAULT_SOURCE_HEIGHT;
    const int   fs = ctx->gc_font_size;

    float tw = get_src_width (ctx->ts_gc, ctx->gc_text, fs);
    float th = get_src_height(ctx->ts_gc, fs);

    /* Centralização real usando dimensão medida da textura */
    float cx = (W - tw) * 0.5f;
    float cy = (H - th) * 0.5f;
    float bp = 32.0f;
    float radius = 10.0f;

    float anim_progress = ease_out_expo(ctx->gc_anim_progress);
    bool gc_anim_active = (ctx->gc_anim_state != 0 || ctx->gc_anim_progress > 0.0f);
    const char *gc_type = gc_anim_active ? (ctx->gc_anim_type ? ctx->gc_anim_type : "fade") : "";

    gs_matrix_push();

    if (gc_anim_active) {
        if (strcmp(gc_type, "scale") == 0) {
            float s = 0.3f + 0.7f * ease_out_back(ctx->gc_anim_progress);
            gs_matrix_translate3f(cx + tw * 0.5f, cy + th * 0.5f, 0.0f);
            gs_matrix_scale3f(s, s, 1.0f);
            gs_matrix_translate3f(-(cx + tw * 0.5f), -(cy + th * 0.5f), 0.0f);
        } else if (strcmp(gc_type, "slide") == 0) {
            float slide_dist = 200.0f * (1.0f - anim_progress);
            const char *dir = ctx->gc_anim_dir ? ctx->gc_anim_dir : "up";
            float sx = 0.0f, sy = 0.0f;
            if      (strcmp(dir, "up")    == 0) sy =  slide_dist;
            else if (strcmp(dir, "down")  == 0) sy = -slide_dist;
            else if (strcmp(dir, "left")  == 0) sx =  slide_dist;
            else if (strcmp(dir, "right") == 0) sx = -slide_dist;
            gs_matrix_translate3f(sx, sy, 0.0f);
        }
    }

    float gc_opacity = ctx->opacity_global * ctx->opacity_gc;
    if (gc_anim_active && strcmp(gc_type, "fade") == 0) {
        float fade_alpha = (anim_progress < 0.01f) ? 0.0f : anim_progress;
        gc_opacity *= fade_alpha;
    }

    /* Sombra */
    uint32_t shadow_color = apply_opacity(0x55000000, gc_opacity);
    draw_rounded_rect(cx - bp + 4.0f, cy - bp + 4.0f,
                      tw + bp * 2.0f, th + bp * 2.0f,
                      radius, shadow_color);

    /* Fundo com gradient sutil */
    uint32_t bg_top = apply_opacity(ctx->color_bg, gc_opacity);
    uint32_t bg_bot = apply_opacity(
        lerp_color(ctx->color_bg,
                   MAKE_ARGB(GET_ALPHA(ctx->color_bg),
                              (uint32_t)(GET_RED(ctx->color_bg)   * 0.8f),
                              (uint32_t)(GET_GREEN(ctx->color_bg) * 0.8f),
                              (uint32_t)(GET_BLUE(ctx->color_bg)  * 0.8f)),
                   0.2f),
        gc_opacity);

    if (ctx->glass_loaded && ctx->effect_glass) {
        draw_rect_glass(ctx, cx - bp, cy - bp,
                        tw + bp * 2.0f, th + bp * 2.0f,
                        apply_opacity(ctx->color_bg, gc_opacity), true);
    } else {
        draw_gradient_rect_v(cx - bp, cy - bp,
                             tw + bp * 2.0f, th + bp * 2.0f,
                             bg_top, bg_bot);
    }

    /* Accent line no topo do GC */
    draw_accent_line(cx - bp, cy - bp, tw + bp * 2.0f, 3.0f,
                     apply_opacity(ctx->color_primary, gc_opacity),
                     ctx->glow_strength);

    /* Texto centralizado (dimensão real) */
    draw_text_source(ctx->ts_gc, cx, cy);

    gs_matrix_pop();
}

/* ============================================================================
 * TICKER (RODAPÉ) — broadcast profissional
 *
 * Melhorias:
 *   - altura, font_size e padding configuráveis via propriedades OBS
 *   - gradient vertical no fundo (mais escuro em cima → ligeiramente mais
 *     claro em baixo, estilo telejornal)
 *   - accent line no topo com glow
 *   - "bullet" separador entre repetições do texto
 *   - loop contínuo real sem teleporte brusco
 *   - velocidade baseada em deltaTime (já era — mantido)
 * ============================================================================ */
void render_ticker(BroadcastContext *ctx)
{
    if (!ctx || !ctx->ticker_text || strlen(ctx->ticker_text) == 0) return;

    const float W   = (float)DEFAULT_SOURCE_WIDTH;
    const float H   = (float)DEFAULT_SOURCE_HEIGHT;
    const float bh  = ctx->ticker_height > 0.0f ? ctx->ticker_height : (float)TICKER_BAR_HEIGHT;
    const int   fs  = ctx->ticker_font_size > 0 ? ctx->ticker_font_size : 24;
    const float pad = ctx->ticker_padding  > 0.0f ? ctx->ticker_padding  : 50.0f;

    float tw  = get_src_width (ctx->ts_ticker, ctx->ticker_text, fs);
    float th  = get_src_height(ctx->ts_ticker, fs);

    /* Unidade de repetição: texto + padding separador */
    float unit_w = tw + pad;
    float off    = (unit_w > 0.0f) ? fmodf(ctx->ticker_offset, unit_w) : 0.0f;

    float tk_opacity  = ctx->opacity_global * ctx->opacity_ticker;

    /* ── Fundo com gradient vertical ───────────────────────────────── */
    uint32_t bg_top = apply_opacity(COLOR_TICKER_BG, tk_opacity);
    /* Ligeiramente mais saturado em baixo */
    uint32_t bg_bot = apply_opacity(
        MAKE_ARGB(0xEE,
                  (uint32_t)(GET_RED(COLOR_TICKER_BG)   * 0.9f),
                  (uint32_t)(GET_GREEN(COLOR_TICKER_BG) * 0.9f),
                  GET_BLUE(COLOR_TICKER_BG) + 20),
        tk_opacity);
    draw_gradient_rect_v(0.0f, H - bh, W, bh, bg_top, bg_bot);

    /* ── Accent line no topo com glow ──────────────────────────────── */
    draw_accent_line(0.0f, H - bh, W, 3.0f,
                     apply_opacity(ctx->color_primary, tk_opacity),
                     ctx->glow_strength);

    /* ── Bullet separador (ponto colorido entre repetições) ────────── */
    /* Desenha no início de cada cópia do texto como marcador visual.  */

    float yt = H - bh + (bh - th) * 0.5f;

    /* Posição X da primeira cópia visível */
    float x1 = W - off;
    draw_text_source(ctx->ts_ticker, x1, yt);

    /* Cópia anterior (para loop contínuo sem gap) */
    float x2 = x1 - unit_w;
    if (x2 + tw > 0.0f)
        draw_text_source(ctx->ts_ticker, x2, yt);

    /* Cópia seguinte (para preencher a tela quando necessário) */
    float x3 = x1 + unit_w;
    if (x3 < W)
        draw_text_source(ctx->ts_ticker, x3, yt);

    /* Cópia +2 para resoluções ultrawide ou ticker curto */
    float x4 = x1 + unit_w * 2.0f;
    if (x4 < W)
        draw_text_source(ctx->ts_ticker, x4, yt);

    /* ── Bullet separador entre cópias ─────────────────────────────── */
    uint32_t bullet_color = apply_opacity(ctx->color_primary, tk_opacity * 0.8f);
    float bul_r = bh * 0.12f;   /* raio proporcional à altura da barra */
    float bul_y = H - bh * 0.5f - bul_r;

    /* Para cada posição de início de cópia visível, desenha um bullet */
    for (float bx = x1; bx < W + unit_w; bx += unit_w) {
        float px = bx - pad * 0.5f;
        if (px > -bul_r * 2.0f && px < W + bul_r * 2.0f)
            draw_rounded_rect(px - bul_r, bul_y, bul_r * 2.0f, bul_r * 2.0f,
                              bul_r, bullet_color);
    }
}

/* ============================================================================
 * RODAPÉ SOCIAL — carrossel automático (estilo HTML)
 *
 * Design baseado no HTML fornecido:
 *   - Barra full-width no fundo, 65px altura
 *   - Gradiente 3-stop: navy → azul → navy
 *   - Borda dourada no topo (2px solid #d4af37)
 *   - Conteúdo centralizado: tag (bold, dourado) + handle (branco)
 *   - Cicla a cada 5s com fade-in de 0.8s
 *   - Ícone substituído pela tag textual (IG, TK, FB, YT) em destaque
 * ============================================================================ */
void render_social_carousel(BroadcastContext *ctx)
{
    if (!ctx) return;

    const float W = (float)DEFAULT_SOURCE_WIDTH;
    const float H = (float)DEFAULT_SOURCE_HEIGHT;
    const float bar_h = (float)SOCIAL_FOOTER_HEIGHT;

    const char *handles[4] = {
        ctx->instagram, ctx->tiktok, ctx->facebook, ctx->youtube
    };

    /* Só renderiza se o handle actual for vazio — tenta achar o primeiro */
    int idx = ctx->social_carousel_index;
    if (!handles[idx] || strlen(handles[idx]) == 0) {
        /* Procura o primeiro handle válido */
        for (int i = 0; i < 4; i++) {
            if (handles[i] && strlen(handles[i]) > 0) {
                idx = i;
                break;
            }
        }
        if (!handles[idx] || strlen(handles[idx]) == 0)
            return; /* nenhuma rede social configurada */
    }

    float soc_opacity = ctx->opacity_global * ctx->opacity_social;

    /* ── Fade animation (0.8s) — igual ao CSS: @keyframes fade 0.8s ease ─ */
    float fade_progress = ctx->social_carousel_timer / SOCIAL_CAROUSEL_FADE_DUR;
    if (fade_progress > 1.0f) fade_progress = 1.0f;
    float fade_opacity = fade_progress;
    float slide_up     = 5.0f * (1.0f - fade_progress); /* translateY(5px) */

    float carousel_alpha = soc_opacity * fade_opacity;

    /* ── Fundo: frosted glass (se disponível) ou gradiente 3-stop ────
     * O shader glass dá aquele visual moderno de vidro fosco com
     * reflexo suave e textura procedural. Fallback para gradiente.      */
    uint32_t soc_bg_color = apply_opacity(COLOR_NAVY, carousel_alpha);
    if (ctx->glass_loaded && ctx->effect_glass) {
        draw_rect_glass(ctx, 0.0f, H - bar_h, W, bar_h,
                        soc_bg_color, false);  /* horizontal = false → usa HorizontalGlass */
    } else {
        uint32_t bg_left  = apply_opacity(COLOR_NAVY, carousel_alpha);
        uint32_t bg_mid   = apply_opacity(COLOR_BLUE, carousel_alpha);
        uint32_t bg_right = apply_opacity(COLOR_NAVY, carousel_alpha);
        draw_gradient_3stop(0.0f, H - bar_h, W, bar_h, bg_left, bg_mid, bg_right);
    }

    /* ── Borda dourada no topo (border-top: 2px solid #d4af37) ────────── */
    uint32_t gold_border = apply_opacity(COLOR_GOLD, carousel_alpha);
    draw_rect(0.0f, H - bar_h, W, 2.0f, gold_border);

    /* ── Conteúdo centralizado: tag + handle com fade ────────────────── */
    float tag_h    = get_src_height(ctx->ts_social_tag[idx], 14);
    float handle_h = get_src_height(ctx->ts_social_handle[idx], 20);

    float tag_w    = get_src_width(ctx->ts_social_tag[idx], SOCIAL_TAGS[idx], 14);
    float handle_w = get_src_width(ctx->ts_social_handle[idx], handles[idx], 20);

    float total_w = tag_w + 12.0f + handle_w;  /* gap entre tag e handle */
    float content_x = (W - total_w) * 0.5f;
    float content_y = H - bar_h + (bar_h - handle_h) * 0.5f + slide_up;

    /* Tag dourada em destaque (simula o ícone) */
    draw_text_source(ctx->ts_social_tag[idx],
                     content_x, content_y + (handle_h - tag_h) * 0.5f);

    /* Handle branco ao lado */
    draw_text_source(ctx->ts_social_handle[idx],
                     content_x + tag_w + 12.0f, content_y);
}
