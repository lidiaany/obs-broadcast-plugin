# 📺 Broadcast Overlay System — Plugin Nativo para OBS Studio

<div align="center">

[![CI/CD](https://github.com/lidiany/obs-broadcast-plugin/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/lidiany/obs-broadcast-plugin/actions/workflows/build.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE)
[![OBS Studio](https://img.shields.io/badge/OBS%20Studio-29%2B-%234A7BB6?logo=obs-studio)](https://obsproject.com/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-%2300599C?logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![GitHub Release](https://img.shields.io/github/v/release/lidiany/obs-broadcast-plugin?include_prereleases&label=release)](https://github.com/lidiany/obs-broadcast-plugin/releases)

</div>

**Broadcast Overlay System** é um plugin nativo C++ para OBS Studio que adiciona uma Source profissional de broadcast com:

- **Lower Third** — Barra estilo TV/ESPN com nome e cargo
- **GC (Gerador de Caracteres)** — Texto dinâmico centralizado
- **Ticker / Rodapé** — Texto corrido infinito estilo CNN
- **Redes Sociais** — Painel com Instagram, TikTok, Facebook, YouTube
- **Efeito Vidro Fosco** — Background com shader `.effect` estilo frosted glass
- **WebSocket** — Controle remoto via rede (JSON sobre WebSocket)

## 📋 Pré-requisitos

| Ferramenta | Versão Mínima |
|---|---|
| [OBS Studio](https://obsproject.com/) | 29+ (com SDK de desenvolvimento) |
| [CMake](https://cmake.org/) | 3.16+ |
| Compilador C++17 | MSVC 2022 / GCC 11+ / Clang 14+ |

## 🔧 Build e Instalação

### Windows (Visual Studio 2022)

```bash
# No "Developer Command Prompt for VS 2022":
cd obs-broadcast-plugin
mkdir build && cd build

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="C:\Program Files\obs-studio\lib\cmake"

cmake --build . --config RelWithDebInfo
```

### Linux (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake libobs-dev

cd obs-broadcast-plugin
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc)
```

### macOS

```bash
brew install cmake obs-studio

cd obs-broadcast-plugin
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(sysctl -n hw.logicalcpu)
```

### Instalação no OBS

Copie o binário gerado para a pasta de plugins:

**Windows**: `obs-broadcast-plugin.dll` → `C:\Program Files\obs-studio\obs-plugins\64bit\`
**Linux**: `libobs-broadcast-plugin.so` → `/usr/lib/obs-plugins/`
**macOS**: `obs-broadcast-plugin.so` → `~/Library/Application Support/obs-studio/plugins/obs-broadcast-plugin/bin/`

Copie também a pasta `data/`:
**Windows**: → `C:\Program Files\obs-studio\data\obs-plugins\obs-broadcast-plugin\`
**Linux**: → `/usr/share/obs/obs-plugins/obs-broadcast-plugin/data/`
**macOS**: → `~/Library/Application Support/obs-studio/plugins/obs-broadcast-plugin/data/`

## 🎬 Como Usar

1. Abra o OBS Studio
2. Na cena, clique em **+** (Adicionar Source)
3. Selecione **"Broadcast Overlay System"**
4. Configure as propriedades:

| Aba | Função |
|---|---|
| **Geral** | Dimensões, cores primária/secundária/destaque/fundo |
| **Lower Third** | Nome, cargo, duração, alinhamento (esquerda/direita/centro) |
| **GC** | Texto dinâmico (multilinha), habilitar/desabilitar |
| **Ticker** | Texto corrido, velocidade de rolagem |
| **Redes Sociais** | Instagram, TikTok, Facebook, YouTube, posição na tela |
| **WebSocket** | Habilitar servidor, porta (ex: 8080) |

## 🌐 Controle Remoto via WebSocket

Com o servidor WebSocket habilitado, conecte-se na porta configurada e envie comandos JSON:

### Lower Third
```json
{"command":"lower_third", "name":"Maria Silva", "title":"CEO", "duration":8.0}
```

### GC
```json
{"command":"gc", "text":"LIVE: Entrevista Exclusiva", "enabled":true}
```

### Ticker
```json
{"command":"ticker", "text":"Notícia urgent | Atualizada via WebSocket", "speed":120}
```

### Redes Sociais
```json
{"command":"social", "instagram":"@novo_insta", "tiktok":"@novo_tk", "enabled":true}
```

### Exemplo Python
```python
import asyncio, websockets, json

async def control():
    async with websockets.connect("ws://localhost:8080") as ws:
        await ws.send(json.dumps({
            "command": "lower_third",
            "name": "Convidado Especial",
            "title": "CEO & Founder"
        }))
        print("Resposta:", await ws.recv())

asyncio.run(control())
```

## 🏗️ Estrutura do Projeto

```
obs-broadcast-plugin/
├── CMakeLists.txt                  # Build system
├── plugin-main.cpp                 # Entry point (OBS_DECLARE_MODULE)
├── broadcast-source.h              # Context struct e declarações
├── broadcast-source.cpp            # Ciclo de vida (create, destroy, tick)
├── graphics-render.cpp             # Renderização (lower third, GC, ticker, social)
├── properties.cpp                  # Painel de propriedades do OBS
├── websocket-server.h              # Servidor WebSocket (header)
├── websocket-server.cpp            # Servidor WebSocket (SHA1, Base64, TCP)
├── data/
│   ├── locale/
│   │   ├── en-US.ini               # Localização inglês
│   │   └── pt-BR.ini               # Localização português brasileiro
│   └── shaders/
│       └── frosted_glass.effect    # Shader de vidro fosco
├── LICENSE                         # GPL v2
└── README.md
```

## ✨ Funcionalidades Técnicas

- **Renderização 60fps**: Font caching (16 slots), sem alocações no hot-path
- **Animações suaves**: Ease-out cúbico para entrada/saída do Lower Third
- **Atualização em tempo real**: Sem reload do OBS ao alterar propriedades
- **Shader vidro fosco**: Gradiente vertical/horizontal, reflexo, ruído, tom azulado
- **WebSocket nativo**: SHA1 + Base64 implementados manualmente (zero dependências)
- **Thread-safe**: Fila de comandos protegida por mutex
- **Cantos arredondados**: Implementados com arcos triangulados (sem shader)

## 📄 Licença

Este plugin é distribuído sob a **GNU General Public License v2.0** (compatível com o OBS Studio).

Veja o arquivo [LICENSE](LICENSE) para mais detalhes.
