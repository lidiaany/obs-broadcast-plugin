/* ============================================================================
 * plugin-main.cpp — OBS Broadcast Overlay System
 *
 * Módulo principal de inicialização do plugin nativo para OBS Studio.
 * Registra a nova Source "Broadcast Overlay System" no OBS.
 *
 * Este plugin cria uma source personalizada que renderiza:
 *   - Lower Third (estilo TV/ESPN) com nome e cargo
 *   - GC (Gerador de Caracteres) com texto dinâmico
 *   - Rodapé / Ticker (notícias contínuas estilo CNN)
 *   - Redes Sociais (Instagram, TikTok, Facebook, YouTube)
 *
 * API: libobs (OBS Studio SDK)
 * Linguagem: C++17
 * ============================================================================
 *
 * USO:
 *   1. Compile o plugin (veja CMakeLists.txt para instruções)
 *   2. Copie o binário gerado para a pasta de plugins do OBS
 *   3. No OBS, adicione uma nova Source → "Broadcast Overlay System"
 *   4. Configure as propriedades (nome, cargo, texto, etc.)
 *
 * ============================================================================
 */

#include <obs-module.h>

/* ── Declaração do módulo OBS ──────────────────────────────────────────── */
/* OBS_DECLARE_MODULE() é uma macro obrigatória que gera as funções de
 * identificação e metadados do plugin. Deve estar no escopo global. */
OBS_DECLARE_MODULE()

/* ── Localização (i18n) ──────────────────────────────────────────────────── */
/* OBS_MODULE_USE_DEFAULT_LOCALE() ativa suporte a arquivos .ini de tradução
 * localizados em data/locale/<idioma>.ini. O idioma padrão é "en-US". */
OBS_MODULE_USE_DEFAULT_LOCALE("obs-broadcast-plugin", "en-US")

/* ── Referência externa à estrutura obs_source_info ──────────────────────── */
/* O struct obs_source_info é definido em broadcast-source.cpp. Ele contém
 * todos os callbacks que o OBS chama para gerenciar a source (criar,
 * destruir, renderizar, propriedades, etc.). */
extern struct obs_source_info broadcast_overlay_source_info;

/* ── obs_module_load() ───────────────────────────────────────────────────── */
/* Chamado pelo OBS assim que o plugin é carregado. É aqui que registramos
 * todos os objetos que o plugin oferece (sources, outputs, encoders, etc.).
 *
 * Retorna: true se o módulo foi carregado com sucesso, false caso contrário.
 */
bool obs_module_load(void)
{
    /* Registra nossa source personalizada no OBS */
    obs_register_source(&broadcast_overlay_source_info);

    blog(LOG_INFO, "[Broadcast Overlay Plugin] v1.0.0 carregado com sucesso!");
    blog(LOG_INFO, "[Broadcast Overlay Plugin] Source 'Broadcast Overlay System' registrada.");

    return true;
}

/* ── obs_module_unload() ────────────────────────────────────────────────── */
/* Chamado pelo OBS quando o plugin é descarregado. Usado para limpeza
 * global. Este callback é opcional. */
void obs_module_unload(void)
{
    blog(LOG_INFO, "[Broadcast Overlay Plugin] Descarregado.");
}

/* ── obs_module_author() ─────────────────────────────────────────────────── */
/* Retorna o nome do autor do plugin. */
const char *obs_module_author(void)
{
    return "Broadcast Overlay Team";
}

/* ── obs_module_description() ────────────────────────────────────────────── */
/* Retorna uma descrição do plugin.
 * NOTA: OBS_DECLARE_MODULE() já gera implementações padrão para
 * obs_module_name(), obs_module_description() e obs_module_author().
 * As definições abaixo sobrescrevem os padrões com valores personalizados.
 */
const char *obs_module_name(void)
{
    return "Broadcast Overlay System";
}

const char *obs_module_description(void)
{
    return "Plugin profissional de broadcast com Lower Third, GC (Gerador de "
           "Caracteres), Ticker (rodapé) e Redes Sociais. Cria uma source "
           "personalizada para transmissões ao vivo estilo TV/ESPN.";
}
