/* ============================================================================
 * broadcast-source.cpp — Broadcast Overlay System (Ciclo de Vida + WebSocket)
 *
 * Implementa os callbacks de ciclo de vida da source, incluindo
 * gerenciamento do servidor WebSocket para controle remoto.
 *
 * Texto renderizado via child sources oficiais do OBS.
 * fallback automatico: tenta text_gdiplus, text_ft2_source_v2 e
 * text_ft2_source em ordem de prioridade ate encontrar um disponivel.
 * Nao existe gs_font_t no OBS Studio.
 *
 * ============================================================================
 */

#include "broadcast-source.h"
#include <obs-module.h>
#include <cstring>
#include <cstdio>
#include <mutex>

/* ============================================================================
 * TEXT SOURCE HELPERS
 * ============================================================================
 *
 * O OBS Studio não expõe uma API directa de fontes (gs_font_t não existe).
 * A forma correta e oficial de renderizar texto num plugin OBS é criar
 * child sources do tipo text_gdiplus (Windows/macOS) ou
 * text_ft2_source_v2 (Linux) e chamar obs_source_video_render() sobre elas.
 *
 * Ciclo de vida:
 *   broadcast_create_text_src  — cria uma nova child source de texto
 *   broadcast_update_text_src  — actualiza texto/cor/fonte de uma child source
 *   broadcast_destroy_text_src — libera a child source
 */

/* Tags fixas das redes sociais — partilhado com graphics-render.cpp */
const char *SOCIAL_TAGS[4] = {"IG", "TK", "FB", "YT"};

/* Converte ARGB (formato interno do plugin) para ABGR (formato OBS text). */
static uint32_t argb_to_abgr(uint32_t argb)
{
    uint32_t a = (argb >> 24) & 0xFF;
    uint32_t r = (argb >> 16) & 0xFF;
    uint32_t g = (argb >>  8) & 0xFF;
    uint32_t b = (argb      ) & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/*
 * Lista priorizada de IDs de text source para cada plataforma.
 * Tenta criar com cada ID até um funcionar. Cachea o resultado.
 *
 * Prioridade:
 *   Windows:   text_gdiplus -> text_ft2_source_v2 -> text_ft2_source
 *   macOS/Linux: text_ft2_source_v2 -> text_ft2_source
 *
 * text_ft2_source_v2 é o ID moderno do FreeType2 (OBS 27+).
 * text_ft2_source é o ID legado (upgrades de cenas antigas).
 * text_gdiplus é Windows-only (GDI+).
 */
/* Cache do tipo de source de texto (thread-safe via std::call_once) */
static const char *g_text_src_type = NULL;
static std::once_flag g_text_src_probe_flag;

static void probe_text_src_type_impl(void)
{
#ifdef _WIN32
    static const char *candidates[] = {
        "text_gdiplus",
        "text_ft2_source_v2",
        "text_ft2_source",
        NULL
    };
#else
    /* macOS e Linux: FreeType2 */
    static const char *candidates[] = {
        "text_ft2_source_v2",
        "text_ft2_source",
        NULL
    };
#endif

    obs_data_t *s = obs_data_create();
    obs_data_set_string(s, "text", "probe");

    for (int i = 0; candidates[i]; i++) {
        const char *type = candidates[i];
        obs_source_t *probe = obs_source_create_private(
            type, "broadcast_text_probe", s);
        if (probe) {
            blog(LOG_INFO,
                 "[Broadcast Overlay] Text source type selecionado: %s",
                 type);
            obs_source_release(probe);
            obs_data_release(s);
            g_text_src_type = candidates[i];
            return;
        }
    }

    obs_data_release(s);

    blog(LOG_WARNING,
         "[Broadcast Overlay] Nenhum plugin de texto encontrado! "
         "Tente instalar text-freetype2 (Linux/macOS) ou "
         "verifique a instalação do OBS.");
    g_text_src_type = candidates[0];
}

static const char *get_text_src_type(void)
{
    std::call_once(g_text_src_probe_flag, probe_text_src_type_impl);
    return g_text_src_type;
}

/* Cria uma child source de texto privada com fallback automatico. */
obs_source_t *broadcast_create_text_src(const char *text,
                                         uint32_t    argb_color,
                                         int         font_size,
                                         bool        bold)
{
    obs_data_t *s = obs_data_create();
    obs_data_t *f = obs_data_create();

    obs_data_set_string(f, "face",  "Arial");
    obs_data_set_int   (f, "size",  (int64_t)font_size);
    obs_data_set_int   (f, "flags", bold ? 1 : 0);  /* 1 = Bold */
    obs_data_set_obj   (s, "font",  f);
    obs_data_release(f);

    obs_data_set_string(s, "text",         text ? text : "");
    obs_data_set_int   (s, "color1",       (int64_t)argb_to_abgr(argb_color));
    obs_data_set_int   (s, "color2",       (int64_t)argb_to_abgr(argb_color));
    obs_data_set_bool  (s, "outline",      false);
    obs_data_set_bool  (s, "drop_shadow",  false);
    obs_data_set_bool  (s, "antialiasing", true);

    const char *type = get_text_src_type();
    obs_source_t *src = obs_source_create_private(
        type, "broadcast_overlay_txt", s);

    obs_data_release(s);
    return src;
}

/* Actualiza (ou cria se NULL) uma child source de texto. */
void broadcast_update_text_src(obs_source_t **src_ptr,
                                const char    *text,
                                uint32_t       argb_color,
                                int            font_size,
                                bool           bold)
{
    if (!src_ptr) return;

    if (!*src_ptr) {
        *src_ptr = broadcast_create_text_src(text, argb_color, font_size, bold);
        return;
    }

    obs_data_t *s = obs_data_create();
    obs_data_t *f = obs_data_create();

    obs_data_set_string(f, "face",  "Arial");
    obs_data_set_int   (f, "size",  (int64_t)font_size);
    obs_data_set_int   (f, "flags", bold ? 1 : 0);
    obs_data_set_obj   (s, "font",  f);
    obs_data_release(f);

    obs_data_set_string(s, "text",   text ? text : "");
    obs_data_set_int   (s, "color1", (int64_t)argb_to_abgr(argb_color));
    obs_data_set_int   (s, "color2", (int64_t)argb_to_abgr(argb_color));

    obs_source_update(*src_ptr, s);
    obs_data_release(s);
}

/* Libera uma child source de texto. */
void broadcast_destroy_text_src(obs_source_t **src_ptr)
{
    if (!src_ptr || !*src_ptr) return;
    obs_source_release(*src_ptr);
    *src_ptr = NULL;
}

/* ============================================================================
 * broadcast_get_name
 * ============================================================================ */
const char *broadcast_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("BroadcastOverlay.Name");
}

/* ============================================================================
 * broadcast_create
 * ============================================================================ */
void *broadcast_create(obs_data_t *settings, obs_source_t *source)
{
    auto *ctx = (BroadcastContext *)bzalloc(sizeof(BroadcastContext));
    if (!ctx) {
        blog(LOG_ERROR, "[Broadcast Overlay] Falha ao alocar contexto!");
        return NULL;
    }

    ctx->source = source;

    /* Lower Third */
    ctx->lt_name          = bstrdup("");
    ctx->lt_title         = bstrdup("");
    ctx->lt_enabled       = true;
    ctx->lt_duration      = LT_DISPLAY_DURATION;
    ctx->lt_alignment     = LT_ALIGN_LEFT;
    ctx->lt_is_visible    = true;
    ctx->lt_anim_progress = 1.0f;
    ctx->lt_anim_state    = 1.0f;

    /* GC */
    ctx->gc_text      = bstrdup("");
    ctx->gc_enabled   = false;
    ctx->gc_font_size = 72;

    /* Ticker */
    ctx->ticker_text    = bstrdup("");
    ctx->ticker_enabled = false;
    ctx->ticker_speed   = TICKER_DEFAULT_SPEED;
    ctx->ticker_offset  = 0.0f;

    /* Social */
    ctx->social_enabled  = false;
    ctx->social_position = SOCIAL_BOTTOM_RIGHT;
    ctx->instagram       = bstrdup("");
    ctx->tiktok          = bstrdup("");
    ctx->facebook        = bstrdup("");
    ctx->youtube         = bstrdup("");

    /* Cores */
    ctx->color_primary   = COLOR_PRIMARY;
    ctx->color_secondary = COLOR_SECONDARY;
    ctx->color_accent    = COLOR_ACCENT;
    ctx->color_bg        = COLOR_BG;

    /* Dimensões */
    ctx->width  = DEFAULT_SOURCE_WIDTH;
    ctx->height = DEFAULT_SOURCE_HEIGHT;

    /* Animação */
    ctx->elapsed        = 0.0f;
    ctx->lt_visible_time = 0.0f;

    /* WebSocket */
    ctx->ws_server  = NULL;
    ctx->ws_port    = WS_DEFAULT_PORT;
    ctx->ws_enabled = false;

    /* Shader effects */
    ctx->effect_glass      = NULL;
    ctx->glass_loaded      = false;
    ctx->ep_glass_color    = NULL;
    ctx->ep_glass_highlight = NULL;
    ctx->ep_glass_top      = NULL;
    ctx->ep_glass_bot      = NULL;
    ctx->ep_glass_noise    = NULL;
    ctx->ep_glass_tint     = NULL;
    ctx->ep_glass_hl_width = NULL;

    /* Text sources — inicializa a NULL (criadas em broadcast_update) */
    ctx->ts_lt_name  = NULL;
    ctx->ts_lt_title = NULL;
    ctx->ts_gc       = NULL;
    ctx->ts_ticker   = NULL;
    for (int i = 0; i < 4; i++) {
        ctx->ts_social_tag[i]    = NULL;
        ctx->ts_social_handle[i] = NULL;
    }

    /* Carrega os shaders */
    broadcast_load_effects(ctx);

    if (settings) {
        broadcast_update(ctx, settings);
    }

    blog(LOG_INFO, "[Broadcast Overlay] Source criada. Dimensões: %ux%u",
         ctx->width, ctx->height);

    return ctx;
}

/* ============================================================================
 * broadcast_destroy
 * ============================================================================ */
void broadcast_destroy(void *data)
{
    if (!data) return;

    auto *ctx = (BroadcastContext *)data;

    /* Para o WebSocket server */
    if (ctx->ws_server) {
        ctx->ws_server->stop();
        delete ctx->ws_server;
        ctx->ws_server = NULL;
    }

    /* Libera shaders */
    broadcast_unload_effects(ctx);

    /* Libera strings */
    bfree_safe(ctx->lt_name);
    bfree_safe(ctx->lt_title);
    bfree_safe(ctx->gc_text);
    bfree_safe(ctx->ticker_text);
    bfree_safe(ctx->instagram);
    bfree_safe(ctx->tiktok);
    bfree_safe(ctx->facebook);
    bfree_safe(ctx->youtube);

    /* Libera child text sources */
    broadcast_destroy_text_src(&ctx->ts_lt_name);
    broadcast_destroy_text_src(&ctx->ts_lt_title);
    broadcast_destroy_text_src(&ctx->ts_gc);
    broadcast_destroy_text_src(&ctx->ts_ticker);
    for (int i = 0; i < 4; i++) {
        broadcast_destroy_text_src(&ctx->ts_social_tag[i]);
        broadcast_destroy_text_src(&ctx->ts_social_handle[i]);
    }

    /* Libera contexto */
    bfree(ctx);

    blog(LOG_INFO, "[Broadcast Overlay] Source destruída.");
}

/* ============================================================================
 * broadcast_process_ws_commands
 * ============================================================================
 * Processa os comandos recebidos via WebSocket.
 * Chamado a cada tick para aplicar as alterações em tempo real.
 */
void broadcast_process_ws_commands(void *data)
{
    if (!data) return;
    auto *ctx = (BroadcastContext *)data;
    if (!ctx->ws_server || !ctx->ws_server->is_running()) return;

    WSCommand cmd;
    while (ctx->ws_server->poll_command(cmd)) {
        obs_data_t *cmd_data = obs_data_create_from_json(cmd.data_json.c_str());
        if (!cmd_data) continue;

        blog(LOG_INFO, "[Broadcast Overlay] Comando WebSocket recebido: %s",
             cmd.type.c_str());

        bool needs_update = false;
        obs_data_t *settings = obs_data_create();

        /* Pré-popula settings com os valores actuais */
        obs_data_set_string(settings, "lt_name",  ctx->lt_name  ? ctx->lt_name  : "");
        obs_data_set_string(settings, "lt_title", ctx->lt_title ? ctx->lt_title : "");
        obs_data_set_bool  (settings, "lt_enabled",   ctx->lt_enabled);
        obs_data_set_double(settings, "lt_duration",  (double)ctx->lt_duration);
        obs_data_set_int   (settings, "lt_alignment", (int64_t)ctx->lt_alignment);

        obs_data_set_string(settings, "gc_text",    ctx->gc_text ? ctx->gc_text : "");
        obs_data_set_bool  (settings, "gc_enabled", ctx->gc_enabled);

        obs_data_set_string(settings, "ticker_text",    ctx->ticker_text ? ctx->ticker_text : "");
        obs_data_set_bool  (settings, "ticker_enabled", ctx->ticker_enabled);
        obs_data_set_double(settings, "ticker_speed",   (double)ctx->ticker_speed);

        obs_data_set_bool(settings, "social_enabled",  ctx->social_enabled);
        obs_data_set_int (settings, "social_position", (int64_t)ctx->social_position);
        obs_data_set_string(settings, "social_instagram", ctx->instagram ? ctx->instagram : "");
        obs_data_set_string(settings, "social_tiktok",    ctx->tiktok    ? ctx->tiktok    : "");
        obs_data_set_string(settings, "social_facebook",  ctx->facebook  ? ctx->facebook  : "");
        obs_data_set_string(settings, "social_youtube",   ctx->youtube   ? ctx->youtube   : "");

        /* Aplica alterações baseadas no tipo de comando */
        if (cmd.type == "lower_third") {
            const char *name  = obs_data_get_string(cmd_data, "name");
            const char *title = obs_data_get_string(cmd_data, "title");
            if (name  && *name)  obs_data_set_string(settings, "lt_name",  name);
            if (title && *title) obs_data_set_string(settings, "lt_title", title);
            if (obs_data_has_user_value(cmd_data, "enabled"))
                obs_data_set_bool(settings, "lt_enabled", obs_data_get_bool(cmd_data, "enabled"));
            if (obs_data_has_user_value(cmd_data, "duration"))
                obs_data_set_double(settings, "lt_duration", obs_data_get_double(cmd_data, "duration"));
            needs_update = true;

        } else if (cmd.type == "gc") {
            const char *text = obs_data_get_string(cmd_data, "text");
            if (text && *text) obs_data_set_string(settings, "gc_text", text);
            if (obs_data_has_user_value(cmd_data, "enabled"))
                obs_data_set_bool(settings, "gc_enabled", obs_data_get_bool(cmd_data, "enabled"));
            needs_update = true;

        } else if (cmd.type == "ticker") {
            const char *text = obs_data_get_string(cmd_data, "text");
            if (text && *text) obs_data_set_string(settings, "ticker_text", text);
            if (obs_data_has_user_value(cmd_data, "enabled"))
                obs_data_set_bool(settings, "ticker_enabled", obs_data_get_bool(cmd_data, "enabled"));
            if (obs_data_has_user_value(cmd_data, "speed"))
                obs_data_set_double(settings, "ticker_speed", obs_data_get_double(cmd_data, "speed"));
            needs_update = true;

        } else if (cmd.type == "social") {
            const char *ig = obs_data_get_string(cmd_data, "instagram");
            const char *tt = obs_data_get_string(cmd_data, "tiktok");
            const char *fb = obs_data_get_string(cmd_data, "facebook");
            const char *yt = obs_data_get_string(cmd_data, "youtube");
            if (ig && *ig) obs_data_set_string(settings, "social_instagram", ig);
            if (tt && *tt) obs_data_set_string(settings, "social_tiktok",    tt);
            if (fb && *fb) obs_data_set_string(settings, "social_facebook",  fb);
            if (yt && *yt) obs_data_set_string(settings, "social_youtube",   yt);
            if (obs_data_has_user_value(cmd_data, "enabled"))
                obs_data_set_bool(settings, "social_enabled", obs_data_get_bool(cmd_data, "enabled"));
            if (obs_data_has_user_value(cmd_data, "position"))
                obs_data_set_int(settings, "social_position", obs_data_get_int(cmd_data, "position"));
            needs_update = true;
        }

        if (needs_update) {
            broadcast_update(ctx, settings);
        }

        obs_data_release(settings);
        obs_data_release(cmd_data);
    }
}

/* ============================================================================
 * broadcast_update
 * ============================================================================ */
void broadcast_update(void *data, obs_data_t *settings)
{
    if (!data || !settings) return;

    auto *ctx = (BroadcastContext *)data;

    /* ── LOWER THIRD ───────────────────────────────────────────────────── */
    /* Detecta se o conteudo do Lower Third mudou para controlar animacao */
    bool lt_content_changed = false;

    const char *new_name = obs_data_get_string(settings, "lt_name");
    if (new_name && *new_name) {
        if (strcmp(ctx->lt_name ? ctx->lt_name : "", new_name) != 0)
            lt_content_changed = true;
        bfree_safe(ctx->lt_name);
        ctx->lt_name = bstrdup(new_name);
    }

    const char *new_title = obs_data_get_string(settings, "lt_title");
    if (new_title && *new_title) {
        if (strcmp(ctx->lt_title ? ctx->lt_title : "", new_title) != 0)
            lt_content_changed = true;
        bfree_safe(ctx->lt_title);
        ctx->lt_title = bstrdup(new_title);
    }

    bool  new_lt_enabled   = obs_data_get_bool  (settings, "lt_enabled");
    float new_lt_duration  = (float)obs_data_get_double(settings, "lt_duration");
    int   new_lt_alignment = (int)  obs_data_get_int   (settings, "lt_alignment");

    if (new_lt_enabled   != ctx->lt_enabled ||
        new_lt_duration  != ctx->lt_duration ||
        new_lt_alignment != ctx->lt_alignment) {
        lt_content_changed = true;
    }

    ctx->lt_enabled   = new_lt_enabled;
    ctx->lt_duration  = new_lt_duration;
    ctx->lt_alignment = new_lt_alignment;

    /* ── GC ────────────────────────────────────────────────────────────── */
    const char *new_gc = obs_data_get_string(settings, "gc_text");
    if (new_gc && *new_gc) {
        bfree_safe(ctx->gc_text);
        ctx->gc_text = bstrdup(new_gc);
    }
    ctx->gc_enabled = obs_data_get_bool(settings, "gc_enabled");

    /* ── TICKER ─────────────────────────────────────────────────────────── */
    const char *new_ticker = obs_data_get_string(settings, "ticker_text");
    if (new_ticker && *new_ticker) {
        bfree_safe(ctx->ticker_text);
        ctx->ticker_text = bstrdup(new_ticker);
    }
    ctx->ticker_enabled = obs_data_get_bool  (settings, "ticker_enabled");
    ctx->ticker_speed   = (float)obs_data_get_double(settings, "ticker_speed");

    /* ── REDES SOCIAIS ─────────────────────────────────────────────────── */
    ctx->social_enabled  = obs_data_get_bool(settings, "social_enabled");
    ctx->social_position = (int)obs_data_get_int(settings, "social_position");

    const char *new_ig = obs_data_get_string(settings, "social_instagram");
    if (new_ig && *new_ig) { bfree_safe(ctx->instagram); ctx->instagram = bstrdup(new_ig); }

    const char *new_tt = obs_data_get_string(settings, "social_tiktok");
    if (new_tt && *new_tt) { bfree_safe(ctx->tiktok);    ctx->tiktok    = bstrdup(new_tt); }

    const char *new_fb = obs_data_get_string(settings, "social_facebook");
    if (new_fb && *new_fb) { bfree_safe(ctx->facebook);  ctx->facebook  = bstrdup(new_fb); }

    const char *new_yt = obs_data_get_string(settings, "social_youtube");
    if (new_yt && *new_yt) { bfree_safe(ctx->youtube);   ctx->youtube   = bstrdup(new_yt); }

    /* ── CORES ──────────────────────────────────────────────────────────── */
    ctx->color_primary   = (uint32_t)obs_data_get_int(settings, "color_primary");
    ctx->color_secondary = (uint32_t)obs_data_get_int(settings, "color_secondary");
    ctx->color_accent    = (uint32_t)obs_data_get_int(settings, "color_accent");
    ctx->color_bg        = (uint32_t)obs_data_get_int(settings, "color_bg");

    /* ── DIMENSÕES ──────────────────────────────────────────────────────── */
    ctx->width  = (uint32_t)obs_data_get_int(settings, "source_width");
    ctx->height = (uint32_t)obs_data_get_int(settings, "source_height");
    if (ctx->width  == 0) ctx->width  = DEFAULT_SOURCE_WIDTH;
    if (ctx->height == 0) ctx->height = DEFAULT_SOURCE_HEIGHT;

    /* ── WEBSOCKET ──────────────────────────────────────────────────────── */
    bool new_ws_enabled = obs_data_get_bool(settings, "ws_enabled");
    int  new_ws_port    = (int)obs_data_get_int(settings, "ws_port");

    if (new_ws_enabled != ctx->ws_enabled || new_ws_port != ctx->ws_port) {
        if (ctx->ws_server) {
            ctx->ws_server->stop();
            delete ctx->ws_server;
            ctx->ws_server = NULL;
        }
        ctx->ws_enabled = new_ws_enabled;
        ctx->ws_port    = new_ws_port;

        if (ctx->ws_enabled && ctx->ws_port > 0) {
            ctx->ws_server = new WebSocketServer();
            if (!ctx->ws_server->start(ctx->ws_port)) {
                blog(LOG_ERROR, "[Broadcast Overlay] Falha ao iniciar WebSocket na porta %d",
                     ctx->ws_port);
                delete ctx->ws_server;
                ctx->ws_server  = NULL;
                ctx->ws_enabled = false;
            } else {
                blog(LOG_INFO, "[Broadcast Overlay] WebSocket server iniciado na porta %d",
                     ctx->ws_port);
            }
        }
    }

    /* ── ACTUALIZA CHILD TEXT SOURCES ────────────────────────────────────
     *
     * Chamado no thread principal (video_tick / properties callback).
     * As sources são criadas aqui e renderizadas em broadcast_video_render.
     */

    /* Lower Third */
    broadcast_update_text_src(&ctx->ts_lt_name,  ctx->lt_name,  ctx->color_accent, 36, true);
    broadcast_update_text_src(&ctx->ts_lt_title, ctx->lt_title, ctx->color_accent, 24, false);

    /* Ticker */
    broadcast_update_text_src(&ctx->ts_ticker, ctx->ticker_text, ctx->color_accent, 24, false);

    /* GC — calcula tamanho de fonte óptimo baseado no comprimento do texto */
    {
        int len = ctx->gc_text ? (int)strlen(ctx->gc_text) : 0;
        int gc_fs;
        if      (len < 20)  gc_fs = 72;
        else if (len < 50)  gc_fs = 48;
        else if (len < 100) gc_fs = 36;
        else                gc_fs = 28;
        ctx->gc_font_size = gc_fs;
        broadcast_update_text_src(&ctx->ts_gc, ctx->gc_text, ctx->color_accent, gc_fs, true);
    }

    /* Social — tags fixas, cor do primário; handles em cor do accent */
    const char *handles[4] = {
        ctx->instagram ? ctx->instagram : "",
        ctx->tiktok    ? ctx->tiktok    : "",
        ctx->facebook  ? ctx->facebook  : "",
        ctx->youtube   ? ctx->youtube   : ""
    };
    for (int i = 0; i < 4; i++) {
        broadcast_update_text_src(&ctx->ts_social_tag[i],    SOCIAL_TAGS[i],  ctx->color_primary, 14, true);
        broadcast_update_text_src(&ctx->ts_social_handle[i], handles[i],      ctx->color_accent,  20, false);
    }

    /* Reinicia animacao do Lower Third apenas se o conteudo mudou */
    if (lt_content_changed) {
        ctx->lt_anim_progress = 0.0f;
        ctx->lt_anim_state    = 1.0f;
        ctx->lt_is_visible    = true;
        ctx->lt_visible_time  = 0.0f;
    }

    blog(LOG_DEBUG, "[Broadcast Overlay] Configurações actualizadas.");
}

/* ============================================================================
 * broadcast_load_effects
 * ============================================================================ */
void broadcast_load_effects(BroadcastContext *ctx)
{
    if (!ctx) return;

    broadcast_unload_effects(ctx);

    char *effect_path = obs_module_file("shaders/frosted_glass.effect");
    if (!effect_path) {
        blog(LOG_WARNING, "[Broadcast Overlay] Caminho do shader não encontrado");
        return;
    }

    ctx->effect_glass = gs_effect_create_from_file(effect_path, NULL);
    bfree(effect_path);

    if (!ctx->effect_glass) {
        blog(LOG_WARNING, "[Broadcast Overlay] Falha ao carregar shader frosted_glass.effect");
        ctx->glass_loaded = false;
        return;
    }

    ctx->ep_glass_color    = gs_effect_get_param_by_name(ctx->effect_glass, "color");
    ctx->ep_glass_highlight = gs_effect_get_param_by_name(ctx->effect_glass, "highlight_color");
    ctx->ep_glass_top      = gs_effect_get_param_by_name(ctx->effect_glass, "gradient_top");
    ctx->ep_glass_bot      = gs_effect_get_param_by_name(ctx->effect_glass, "gradient_bot");
    ctx->ep_glass_noise    = gs_effect_get_param_by_name(ctx->effect_glass, "noise_strength");
    ctx->ep_glass_tint     = gs_effect_get_param_by_name(ctx->effect_glass, "glass_tint");
    ctx->ep_glass_hl_width = gs_effect_get_param_by_name(ctx->effect_glass, "highlight_width");

    ctx->glass_loaded = true;

    blog(LOG_INFO, "[Broadcast Overlay] Shader frosted_glass.effect carregado com sucesso");
}

/* ============================================================================
 * broadcast_unload_effects
 * ============================================================================ */
void broadcast_unload_effects(BroadcastContext *ctx)
{
    if (!ctx) return;

    if (ctx->effect_glass) {
        gs_effect_destroy(ctx->effect_glass);
        ctx->effect_glass = NULL;
    }

    ctx->ep_glass_color    = NULL;
    ctx->ep_glass_highlight = NULL;
    ctx->ep_glass_top      = NULL;
    ctx->ep_glass_bot      = NULL;
    ctx->ep_glass_noise    = NULL;
    ctx->ep_glass_tint     = NULL;
    ctx->ep_glass_hl_width = NULL;
    ctx->glass_loaded = false;
}

/* ============================================================================
 * broadcast_get_defaults
 * ============================================================================ */
void broadcast_get_defaults(obs_data_t *settings)
{
    if (!settings) return;

    obs_data_set_default_string(settings, "lt_name",      "Nome do Convidado");
    obs_data_set_default_string(settings, "lt_title",     "Cargo / Especialidade");
    obs_data_set_default_bool  (settings, "lt_enabled",   true);
    obs_data_set_default_double(settings, "lt_duration",  LT_DISPLAY_DURATION);
    obs_data_set_default_int   (settings, "lt_alignment", LT_ALIGN_LEFT);

    obs_data_set_default_string(settings, "gc_text",    "TEXTO DINÂMICO - GC");
    obs_data_set_default_bool  (settings, "gc_enabled", false);

    obs_data_set_default_string(settings, "ticker_text",    "Notícia em destaque | Broadcast Overlay | OBS Studio");
    obs_data_set_default_bool  (settings, "ticker_enabled", false);
    obs_data_set_default_double(settings, "ticker_speed",   TICKER_DEFAULT_SPEED);

    obs_data_set_default_bool  (settings, "social_enabled",   false);
    obs_data_set_default_int   (settings, "social_position",  SOCIAL_BOTTOM_RIGHT);
    obs_data_set_default_string(settings, "social_instagram", "@instagram");
    obs_data_set_default_string(settings, "social_tiktok",    "@tiktok");
    obs_data_set_default_string(settings, "social_facebook",  "@facebook");
    obs_data_set_default_string(settings, "social_youtube",   "@youtube");

    obs_data_set_default_int(settings, "color_primary",   (int64_t)COLOR_PRIMARY);
    obs_data_set_default_int(settings, "color_secondary", (int64_t)COLOR_SECONDARY);
    obs_data_set_default_int(settings, "color_accent",    (int64_t)COLOR_ACCENT);
    obs_data_set_default_int(settings, "color_bg",        (int64_t)COLOR_BG);

    obs_data_set_default_int(settings, "source_width",  (int64_t)DEFAULT_SOURCE_WIDTH);
    obs_data_set_default_int(settings, "source_height", (int64_t)DEFAULT_SOURCE_HEIGHT);

    obs_data_set_default_bool(settings, "ws_enabled", false);
    obs_data_set_default_int (settings, "ws_port",    (int64_t)WS_DEFAULT_PORT);
}

/* ============================================================================
 * broadcast_get_width / broadcast_get_height
 * ============================================================================ */
uint32_t broadcast_get_width(void *data)
{
    if (!data) return DEFAULT_SOURCE_WIDTH;
    return ((BroadcastContext *)data)->width;
}

uint32_t broadcast_get_height(void *data)
{
    if (!data) return DEFAULT_SOURCE_HEIGHT;
    return ((BroadcastContext *)data)->height;
}

/* ============================================================================
 * broadcast_video_tick
 * ============================================================================ */
void broadcast_video_tick(void *data, float seconds)
{
    if (!data) return;

    auto *ctx = (BroadcastContext *)data;
    ctx->elapsed += seconds;

    /* Processa comandos WebSocket recebidos */
    broadcast_process_ws_commands(ctx);

    /* Animação do Lower Third */
    if (ctx->lt_enabled && ctx->lt_name && strlen(ctx->lt_name) > 0) {
        if (ctx->lt_anim_state > 0.0f) {
            ctx->lt_anim_progress += seconds / LT_ANIM_DURATION;
            if (ctx->lt_anim_progress >= 1.0f) {
                ctx->lt_anim_progress = 1.0f;
                ctx->lt_anim_state    = 0.0f;
                ctx->lt_is_visible    = true;
            }
        } else if (ctx->lt_anim_state < 0.0f) {
            ctx->lt_anim_progress -= seconds / LT_ANIM_DURATION;
            if (ctx->lt_anim_progress <= 0.0f) {
                ctx->lt_anim_progress = 0.0f;
                ctx->lt_anim_state    = 0.0f;
                ctx->lt_is_visible    = false;
            }
        } else {
            ctx->lt_is_visible = true;
            if (ctx->lt_duration > 0.0f) {
                ctx->lt_visible_time += seconds;
                if (ctx->lt_visible_time >= ctx->lt_duration) {
                    ctx->lt_anim_state = -1.0f;
                }
            }
        }
    } else {
        ctx->lt_is_visible = false;
    }

    /* Ticker offset */
    if (ctx->ticker_enabled && ctx->ticker_text && strlen(ctx->ticker_text) > 0) {
        ctx->ticker_offset += ctx->ticker_speed * seconds;
        if (ctx->ticker_offset > 100000.0f) {
            ctx->ticker_offset = 0.0f;
        }
    }
}

/* ============================================================================
 * broadcast_video_render
 * ============================================================================ */
void broadcast_video_render(void *data, gs_effect_t *effect)
{
    if (!data) return;

    auto *ctx = (BroadcastContext *)data;

    gs_matrix_push();
    gs_matrix_scale3f((float)ctx->width  / (float)DEFAULT_SOURCE_WIDTH,
                      (float)ctx->height / (float)DEFAULT_SOURCE_HEIGHT,
                      1.0f);

    if (ctx->gc_enabled)                              render_gc(ctx);
    if (ctx->lt_enabled && ctx->lt_is_visible)        render_lower_third(ctx);
    if (ctx->social_enabled)                          render_social_media(ctx);
    if (ctx->ticker_enabled)                          render_ticker(ctx);

    gs_matrix_pop();
    UNUSED_PARAMETER(effect);
}

/* ============================================================================
 * DEFINIÇÃO DO OBS_SOURCE_INFO
 * ============================================================================ */
struct obs_source_info broadcast_overlay_source_info = {
    .id           = "broadcast_overlay_system",
    .type         = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO,

    .get_name     = broadcast_get_name,
    .create       = broadcast_create,
    .destroy      = broadcast_destroy,

    .get_defaults  = broadcast_get_defaults,
    .get_properties = broadcast_get_properties,
    .update        = broadcast_update,

    .get_width  = broadcast_get_width,
    .get_height = broadcast_get_height,

    .video_render = broadcast_video_render,
    .video_tick   = broadcast_video_tick,

    .icon_type = OBS_ICON_TYPE_CUSTOM,
};
