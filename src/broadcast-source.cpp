/* ============================================================================
 * broadcast-source.cpp — Broadcast Overlay System (Ciclo de Vida + WebSocket)
 *
 * Implementa os callbacks de ciclo de vida da source, incluindo
 * gerenciamento do servidor WebSocket para controle remoto.
 *
 * ============================================================================
 */

#include "broadcast-source.h"
#include <obs-module.h>
#include <cstring>
#include <cstdio>

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
    ctx->gc_text    = bstrdup("");
    ctx->gc_enabled = false;

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
    ctx->effect_glass  = NULL;
    ctx->glass_loaded  = false;
    ctx->ep_glass_color    = NULL;
    ctx->ep_glass_highlight = NULL;
    ctx->ep_glass_top      = NULL;
    ctx->ep_glass_bot      = NULL;
    ctx->ep_glass_noise    = NULL;
    ctx->ep_glass_tint     = NULL;
    ctx->ep_glass_hl_width = NULL;

    /* Font cache */
    memset(ctx->font_cache, 0, sizeof(ctx->font_cache));
    memset(ctx->font_sizes, 0, sizeof(ctx->font_sizes));
    memset(ctx->font_cache_bold, 0, sizeof(ctx->font_cache_bold));
    ctx->font_dirty = true;

    /* Aplica configurações iniciais */
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

    /* Libera fontes cacheadas */
    for (int i = 0; i < FONT_CACHE_TOTAL; i++) {
        font_destroy_safe(ctx->font_cache[i]);
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
 *
 * Formato JSON esperado:
 *   { "command": "lower_third", "name": "...", "title": "...", "duration": 8.0 }
 *   { "command": "gc",          "text": "...", "enabled": true }
 *   { "command": "ticker",      "text": "...", "speed": 100.0, "enabled": true }
 *   { "command": "social",      "instagram": "@...", "tiktok": "@...", ... }
 */
void broadcast_process_ws_commands(void *data)
{
    if (!data) return;
    auto *ctx = (BroadcastContext *)data;
    if (!ctx->ws_server || !ctx->ws_server->is_running()) return;

    WSCommand cmd;
    while (ctx->ws_server->poll_command(cmd)) {
        /* Converte o JSON do comando em obs_data_t para parsing */
        obs_data_t *cmd_data = obs_data_create_from_json(cmd.data_json.c_str());
        if (!cmd_data) continue;

        blog(LOG_INFO, "[Broadcast Overlay] Comando WebSocket recebido: %s",
             cmd.type.c_str());

        bool needs_update = false;
        obs_data_t *settings = obs_data_create();

        /* Pré-popula settings com os valores atuais */
        const char *current_name = ctx->lt_name ? ctx->lt_name : "";
        const char *current_title = ctx->lt_title ? ctx->lt_title : "";
        const char *current_gc = ctx->gc_text ? ctx->gc_text : "";
        const char *current_ticker = ctx->ticker_text ? ctx->ticker_text : "";
        const char *current_ig = ctx->instagram ? ctx->instagram : "";
        const char *current_tt = ctx->tiktok ? ctx->tiktok : "";
        const char *current_fb = ctx->facebook ? ctx->facebook : "";
        const char *current_yt = ctx->youtube ? ctx->youtube : "";

        obs_data_set_string(settings, "lt_name", current_name);
        obs_data_set_string(settings, "lt_title", current_title);
        obs_data_set_bool(settings, "lt_enabled", ctx->lt_enabled);
        obs_data_set_double(settings, "lt_duration", (double)ctx->lt_duration);
        obs_data_set_int(settings, "lt_alignment", (int64_t)ctx->lt_alignment);

        obs_data_set_string(settings, "gc_text", current_gc);
        obs_data_set_bool(settings, "gc_enabled", ctx->gc_enabled);

        obs_data_set_string(settings, "ticker_text", current_ticker);
        obs_data_set_bool(settings, "ticker_enabled", ctx->ticker_enabled);
        obs_data_set_double(settings, "ticker_speed", (double)ctx->ticker_speed);

        obs_data_set_bool(settings, "social_enabled", ctx->social_enabled);
        obs_data_set_int(settings, "social_position", (int64_t)ctx->social_position);
        obs_data_set_string(settings, "social_instagram", current_ig);
        obs_data_set_string(settings, "social_tiktok", current_tt);
        obs_data_set_string(settings, "social_facebook", current_fb);
        obs_data_set_string(settings, "social_youtube", current_yt);

        /* Aplica alterações baseadas no tipo de comando */
        if (cmd.type == "lower_third") {
            const char *name = obs_data_get_string(cmd_data, "name");
            const char *title = obs_data_get_string(cmd_data, "title");

            if (name && *name) {
                obs_data_set_string(settings, "lt_name", name);
            }
            if (title && *title) {
                obs_data_set_string(settings, "lt_title", title);
            }

            /* enabled (opcional) */
            if (obs_data_has_user_value(cmd_data, "enabled")) {
                obs_data_set_bool(settings, "lt_enabled",
                                  obs_data_get_bool(cmd_data, "enabled"));
            }
            /* duration (opcional) */
            if (obs_data_has_user_value(cmd_data, "duration")) {
                obs_data_set_double(settings, "lt_duration",
                                    obs_data_get_double(cmd_data, "duration"));
            }

            needs_update = true;

        } else if (cmd.type == "gc") {
            const char *text = obs_data_get_string(cmd_data, "text");
            if (text && *text) {
                obs_data_set_string(settings, "gc_text", text);
            }
            if (obs_data_has_user_value(cmd_data, "enabled")) {
                obs_data_set_bool(settings, "gc_enabled",
                                  obs_data_get_bool(cmd_data, "enabled"));
            }
            needs_update = true;

        } else if (cmd.type == "ticker") {
            const char *text = obs_data_get_string(cmd_data, "text");
            if (text && *text) {
                obs_data_set_string(settings, "ticker_text", text);
            }
            if (obs_data_has_user_value(cmd_data, "enabled")) {
                obs_data_set_bool(settings, "ticker_enabled",
                                  obs_data_get_bool(cmd_data, "enabled"));
            }
            if (obs_data_has_user_value(cmd_data, "speed")) {
                obs_data_set_double(settings, "ticker_speed",
                                    obs_data_get_double(cmd_data, "speed"));
            }
            needs_update = true;

        } else if (cmd.type == "social") {
            const char *ig = obs_data_get_string(cmd_data, "instagram");
            const char *tt = obs_data_get_string(cmd_data, "tiktok");
            const char *fb = obs_data_get_string(cmd_data, "facebook");
            const char *yt = obs_data_get_string(cmd_data, "youtube");

            if (ig && *ig) obs_data_set_string(settings, "social_instagram", ig);
            if (tt && *tt) obs_data_set_string(settings, "social_tiktok", tt);
            if (fb && *fb) obs_data_set_string(settings, "social_facebook", fb);
            if (yt && *yt) obs_data_set_string(settings, "social_youtube", yt);

            if (obs_data_has_user_value(cmd_data, "enabled")) {
                obs_data_set_bool(settings, "social_enabled",
                                  obs_data_get_bool(cmd_data, "enabled"));
            }
            if (obs_data_has_user_value(cmd_data, "position")) {
                obs_data_set_int(settings, "social_position",
                                 obs_data_get_int(cmd_data, "position"));
            }
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
    const char *new_name = obs_data_get_string(settings, "lt_name");
    if (new_name && *new_name) {
        bfree_safe(ctx->lt_name);
        ctx->lt_name = bstrdup(new_name);
    }

    const char *new_title = obs_data_get_string(settings, "lt_title");
    if (new_title && *new_title) {
        bfree_safe(ctx->lt_title);
        ctx->lt_title = bstrdup(new_title);
    }

    ctx->lt_enabled   = obs_data_get_bool(settings, "lt_enabled");
    ctx->lt_duration  = (float)obs_data_get_double(settings, "lt_duration");
    ctx->lt_alignment = (int)obs_data_get_int(settings, "lt_alignment");

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
    ctx->ticker_enabled = obs_data_get_bool(settings, "ticker_enabled");
    ctx->ticker_speed   = (float)obs_data_get_double(settings, "ticker_speed");

    /* ── REDES SOCIAIS ─────────────────────────────────────────────────── */
    ctx->social_enabled  = obs_data_get_bool(settings, "social_enabled");
    ctx->social_position = (int)obs_data_get_int(settings, "social_position");

    const char *new_ig = obs_data_get_string(settings, "social_instagram");
    if (new_ig && *new_ig) {
        bfree_safe(ctx->instagram);
        ctx->instagram = bstrdup(new_ig);
    }

    const char *new_tt = obs_data_get_string(settings, "social_tiktok");
    if (new_tt && *new_tt) {
        bfree_safe(ctx->tiktok);
        ctx->tiktok = bstrdup(new_tt);
    }

    const char *new_fb = obs_data_get_string(settings, "social_facebook");
    if (new_fb && *new_fb) {
        bfree_safe(ctx->facebook);
        ctx->facebook = bstrdup(new_fb);
    }

    const char *new_yt = obs_data_get_string(settings, "social_youtube");
    if (new_yt && *new_yt) {
        bfree_safe(ctx->youtube);
        ctx->youtube = bstrdup(new_yt);
    }

    /* ── CORES ──────────────────────────────────────────────────────────── */
    ctx->color_primary   = (uint32_t)obs_data_get_int(settings, "color_primary");
    ctx->color_secondary = (uint32_t)obs_data_get_int(settings, "color_secondary");
    ctx->color_accent    = (uint32_t)obs_data_get_int(settings, "color_accent");
    ctx->color_bg        = (uint32_t)obs_data_get_int(settings, "color_bg");

    /* ── DIMENSÕES ──────────────────────────────────────────────────────── */
    ctx->width  = (uint32_t)obs_data_get_int(settings, "source_width");
    ctx->height = (uint32_t)obs_data_get_int(settings, "source_height");

    /* ── WEBSOCKET ──────────────────────────────────────────────────────── */
    bool new_ws_enabled = obs_data_get_bool(settings, "ws_enabled");
    int  new_ws_port    = (int)obs_data_get_int(settings, "ws_port");

    /* Gerencia o ciclo de vida do servidor WebSocket */
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
                ctx->ws_server = NULL;
                ctx->ws_enabled = false;
            } else {
                blog(LOG_INFO, "[Broadcast Overlay] WebSocket server iniciado na porta %d",
                     ctx->ws_port);
            }
        }
    }

    /* Reinicia animação do Lower Third */
    ctx->lt_anim_progress = 0.0f;
    ctx->lt_anim_state    = 1.0f;
    ctx->lt_is_visible    = true;
    ctx->lt_visible_time  = 0.0f;

    /* Marca cache de fontes como dirty */
    ctx->font_dirty = true;

    blog(LOG_DEBUG, "[Broadcast Overlay] Configurações atualizadas.");
}

/* ============================================================================
 * broadcast_load_effects
 * ============================================================================
 * Carrega os shaders .effect do disco usando obs_module_file() para
 * resolver o caminho dentro do diretório de dados do plugin.
 */
void broadcast_load_effects(BroadcastContext *ctx)
{
    if (!ctx) return;

    /* Libera efeitos anteriores se existirem */
    broadcast_unload_effects(ctx);

    /* Carrega o shader de vidro fosco */
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

    /* Obtém referências aos parâmetros do shader */
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
 * ============================================================================
 * Libera os shaders carregados.
 */
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
    obs_data_set_default_bool(settings,   "lt_enabled",   true);
    obs_data_set_default_double(settings, "lt_duration",  LT_DISPLAY_DURATION);
    obs_data_set_default_int(settings,    "lt_alignment", LT_ALIGN_LEFT);

    obs_data_set_default_string(settings, "gc_text",    "TEXTO DINÂMICO - GC");
    obs_data_set_default_bool(settings,   "gc_enabled", false);

    obs_data_set_default_string(settings, "ticker_text",  "Notícia em destaque | Broadcast Overlay System | OBS Studio");
    obs_data_set_default_bool(settings,   "ticker_enabled", false);
    obs_data_set_default_double(settings, "ticker_speed", TICKER_DEFAULT_SPEED);

    obs_data_set_default_bool(settings,   "social_enabled",   false);
    obs_data_set_default_int(settings,    "social_position",  SOCIAL_BOTTOM_RIGHT);
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

    /* WebSocket */
    obs_data_set_default_bool(settings,   "ws_enabled", false);
    obs_data_set_default_int(settings,    "ws_port",    (int64_t)WS_DEFAULT_PORT);
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
                ctx->lt_anim_state = 0.0f;
                ctx->lt_is_visible = true;
            }
        } else if (ctx->lt_anim_state < 0.0f) {
            ctx->lt_anim_progress -= seconds / LT_ANIM_DURATION;
            if (ctx->lt_anim_progress <= 0.0f) {
                ctx->lt_anim_progress = 0.0f;
                ctx->lt_anim_state = 0.0f;
                ctx->lt_is_visible = false;
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
    gs_matrix_scale3f((float)ctx->width / (float)DEFAULT_SOURCE_WIDTH,
                      (float)ctx->height / (float)DEFAULT_SOURCE_HEIGHT,
                      1.0f);

    if (ctx->gc_enabled)          render_gc(ctx);
    if (ctx->lt_enabled && ctx->lt_is_visible) render_lower_third(ctx);
    if (ctx->social_enabled)      render_social_media(ctx);
    if (ctx->ticker_enabled)      render_ticker(ctx);

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
