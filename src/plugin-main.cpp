/* ============================================================================
plugin-main.cpp — OBS Broadcast Overlay System
============================================================================ */
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-broadcast-plugin", "en-US")

extern struct obs_source_info broadcast_overlay_source_info;

bool obs_module_load(void) {
    obs_register_source(&broadcast_overlay_source_info);
    return true;
}

void obs_module_unload(void) {}

const char *obs_module_author(void) { return "Broadcast Overlay Team"; }
const char *obs_module_name(void) { return "Broadcast Overlay System"; }
const char *obs_module_description(void) { return "Plugin profissional de broadcast com Lower Third, GC, Ticker e Redes Sociais."; }