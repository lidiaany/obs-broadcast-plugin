/* ============================================================================
 * properties.cpp — Broadcast Overlay System (Painel de Propriedades)
 *
 * Constrói o painel de propriedades do OBS com suporte a:
 *   - Geral, Lower Third, GC, Ticker, Redes Sociais
 *   - WebSocket Server (controle remoto via rede)
 *
 * ============================================================================
 */

#include "broadcast-source.h"
#include <obs-module.h>
#include <cstring>

/* ============================================================================
 * broadcast_get_properties
 * ============================================================================ */
obs_properties_t *broadcast_get_properties(void *data)
{
    obs_properties_t *props = obs_properties_create();

    /* ══════════════════════════════════════════════════════════════════════ */
    /* SEÇÃO: GERAL                                                         */
    /* ══════════════════════════════════════════════════════════════════════ */
    obs_properties_t *general_group = obs_properties_create();
    obs_properties_add_group(props, "general_group",
                             obs_module_text("BroadcastOverly.GeneralGroup"),
                             OBS_GROUP_NORMAL, general_group);

    obs_properties_add_int(general_group, "source_width",
                           obs_module_text("BroadcastOverly.SourceWidth"),
                           320, 7680, 1);
    obs_properties_add_int(general_group, "source_height",
                           obs_module_text("BroadcastOverly.SourceHeight"),
                           240, 4320, 1);

    obs_properties_add_color(general_group, "color_primary",
                             obs_module_text("BroadcastOverly.ColorPrimary"));
    obs_properties_add_color(general_group, "color_secondary",
                             obs_module_text("BroadcastOverly.ColorSecondary"));
    obs_properties_add_color(general_group, "color_accent",
                             obs_module_text("BroadcastOverly.ColorAccent"));
    obs_properties_add_color(general_group, "color_bg",
                             obs_module_text("BroadcastOverly.ColorBg"));

    /* ══════════════════════════════════════════════════════════════════════ */
    /* SEÇÃO: LOWER THIRD                                                    */
    /* ══════════════════════════════════════════════════════════════════════ */
    obs_properties_t *lt_group = obs_properties_create();
    obs_properties_add_group(props, "lt_group",
                             obs_module_text("BroadcastOverly.LTGroup"),
                             OBS_GROUP_NORMAL, lt_group);

    obs_properties_add_bool(lt_group, "lt_enabled",
                            obs_module_text("BroadcastOverly.LTEnabled"));
    obs_properties_add_text(lt_group, "lt_name",
                            obs_module_text("BroadcastOverly.LTName"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_text(lt_group, "lt_title",
                            obs_module_text("BroadcastOverly.LTTitle"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_float_slider(lt_group, "lt_duration",
                                    obs_module_text("BroadcastOverly.LTDuration"),
                                    0.0f, 30.0f, 0.5f);

    obs_property_t *lt_align = obs_properties_add_list(lt_group, "lt_alignment",
                                                       obs_module_text("BroadcastOverly.LTAlignment"),
                                                       OBS_COMBO_TYPE_LIST,
                                                       OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(lt_align, obs_module_text("BroadcastOverly.LTAlignLeft"),   LT_ALIGN_LEFT);
    obs_property_list_add_int(lt_align, obs_module_text("BroadcastOverly.LTAlignRight"),  LT_ALIGN_RIGHT);
    obs_property_list_add_int(lt_align, obs_module_text("BroadcastOverly.LTAlignCenter"), LT_ALIGN_CENTER);

    /* ══════════════════════════════════════════════════════════════════════ */
    /* SEÇÃO: GC (GERADOR DE CARACTERES)                                     */
    /* ══════════════════════════════════════════════════════════════════════ */
    obs_properties_t *gc_group = obs_properties_create();
    obs_properties_add_group(props, "gc_group",
                             obs_module_text("BroadcastOverly.GCGroup"),
                             OBS_GROUP_NORMAL, gc_group);

    obs_properties_add_bool(gc_group, "gc_enabled",
                            obs_module_text("BroadcastOverly.GCEnabled"));
    obs_properties_add_text(gc_group, "gc_text",
                            obs_module_text("BroadcastOverly.GCText"),
                            OBS_TEXT_MULTILINE);

    /* ══════════════════════════════════════════════════════════════════════ */
    /* SEÇÃO: TICKER                                                         */
    /* ══════════════════════════════════════════════════════════════════════ */
    obs_properties_t *ticker_group = obs_properties_create();
    obs_properties_add_group(props, "ticker_group",
                             obs_module_text("BroadcastOverly.TickerGroup"),
                             OBS_GROUP_NORMAL, ticker_group);

    obs_properties_add_bool(ticker_group, "ticker_enabled",
                            obs_module_text("BroadcastOverly.TickerEnabled"));
    obs_properties_add_text(ticker_group, "ticker_text",
                            obs_module_text("BroadcastOverly.TickerText"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_float_slider(ticker_group, "ticker_speed",
                                    obs_module_text("BroadcastOverly.TickerSpeed"),
                                    10.0f, 400.0f, 5.0f);

    /* ══════════════════════════════════════════════════════════════════════ */
    /* SEÇÃO: REDES SOCIAIS                                                  */
    /* ══════════════════════════════════════════════════════════════════════ */
    obs_properties_t *social_group = obs_properties_create();
    obs_properties_add_group(props, "social_group",
                             obs_module_text("BroadcastOverly.SocialGroup"),
                             OBS_GROUP_NORMAL, social_group);

    obs_properties_add_bool(social_group, "social_enabled",
                            obs_module_text("BroadcastOverly.SocialEnabled"));

    obs_property_t *social_pos = obs_properties_add_list(social_group, "social_position",
                                                          obs_module_text("BroadcastOverly.SocialPosition"),
                                                          OBS_COMBO_TYPE_LIST,
                                                          OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(social_pos, obs_module_text("BroadcastOverly.SocialPosTopLeft"),      SOCIAL_TOP_LEFT);
    obs_property_list_add_int(social_pos, obs_module_text("BroadcastOverly.SocialPosTopRight"),     SOCIAL_TOP_RIGHT);
    obs_property_list_add_int(social_pos, obs_module_text("BroadcastOverly.SocialPosBottomLeft"),   SOCIAL_BOTTOM_LEFT);
    obs_property_list_add_int(social_pos, obs_module_text("BroadcastOverly.SocialPosBottomRight"),  SOCIAL_BOTTOM_RIGHT);

    obs_properties_add_text(social_group, "social_instagram",
                            obs_module_text("BroadcastOverly.SocialInstagram"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_text(social_group, "social_tiktok",
                            obs_module_text("BroadcastOverly.SocialTiktok"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_text(social_group, "social_facebook",
                            obs_module_text("BroadcastOverly.SocialFacebook"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_text(social_group, "social_youtube",
                            obs_module_text("BroadcastOverly.SocialYoutube"),
                            OBS_TEXT_DEFAULT);

    /* ══════════════════════════════════════════════════════════════════════ */
    /* SEÇÃO: WEBSOCKET (CONTROLE REMOTO)                                    */
    /* ══════════════════════════════════════════════════════════════════════ */
    obs_properties_t *ws_group = obs_properties_create();
    obs_properties_add_group(props, "ws_group",
                             obs_module_text("BroadcastOverly.WSGroup"),
                             OBS_GROUP_NORMAL, ws_group);

    /* Habilitar/Desabilitar o servidor WebSocket */
    obs_properties_add_bool(ws_group, "ws_enabled",
                            obs_module_text("BroadcastOverly.WSEnabled"));

    /* Porta do servidor WebSocket */
    obs_properties_add_int(ws_group, "ws_port",
                           obs_module_text("BroadcastOverly.WSPort"),
                           1024, 65535, 1);

    UNUSED_PARAMETER(data);
    return props;
}
