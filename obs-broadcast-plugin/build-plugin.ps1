<#
.SYNOPSIS
    Build script for OBS Broadcast Overlay System plugin
.DESCRIPTION
    Verifies all dependencies (Visual Studio, CMake, OBS SDK) and builds
    the OBS native plugin on Windows.
.PARAMETER BuildType
    Build configuration: Debug, Release, RelWithDebInfo (default), MinSizeRel
.PARAMETER OutDir
    Output directory for the compiled plugin (default: ./build/)
.EXAMPLE
    .\build-plugin.ps1
    .\build-plugin.ps1 -BuildType Release
    .\build-plugin.ps1 -BuildType Debug -OutDir "C:\plugins"
#>

param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$BuildType = "RelWithDebInfo",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

# ═══════════════════════════════════════════════════════════════════════════
# FUNÇÕES AUXILIARES
# ═══════════════════════════════════════════════════════════════════════════

function Write-Step   { Write-Host "`n>>> $Message" -ForegroundColor Cyan }
function Write-Success { Write-Host "  [OK] $Message" -ForegroundColor Green }
function Write-Warn   { Write-Host "  [!] $Message" -ForegroundColor Yellow }
function Write-Error  { Write-Host "  [ERRO] $Message" -ForegroundColor Red }

function Test-Command([string]$Command) {
    return (Get-Command $Command -ErrorAction SilentlyContinue) -ne $null
}

# ═══════════════════════════════════════════════════════════════════════════
# CABEÇALHO
# ═══════════════════════════════════════════════════════════════════════════

Write-Host @"

============================================
  OBS Broadcast Overlay System - Build Script
  Windows (PowerShell)
============================================

"@ -ForegroundColor Magenta

Write-Step "Verificando dependencias..."

# ═══════════════════════════════════════════════════════════════════════════
# 1. CMAKE
# ═══════════════════════════════════════════════════════════════════════════

$cmakePath = $null
if (Test-Command "cmake") {
    $cmakePath = (Get-Command "cmake").Source
} else {
    $cmakeCandidates = @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python310\Lib\site-packages\cmake\data\bin\cmake.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python311\Lib\site-packages\cmake\data\bin\cmake.exe",
        "$env:USERPROFILE\scoop\shims\cmake.exe",
        "$env:ProgramData\chocolatey\bin\cmake.exe"
    )
    foreach ($c in $cmakeCandidates) {
        if (Test-Path $c) { $cmakePath = $c; break }
    }
}

if (-not $cmakePath) {
    Write-Error "CMake nao encontrado! Instale em: https://cmake.org/download/"
    Write-Host "  Ou via: choco install cmake / scoop install cmake" -ForegroundColor Yellow
    exit 1
}

Write-Success "CMake: $cmakePath"
$cmakeVersionRaw = & $cmakePath --version | Select-Object -First 1
$cmakeVersion = [Version]([regex]::Match($cmakeVersionRaw, '(\d+\.\d+\.\d+)').Groups[1].Value)
Write-Success "Versao: $cmakeVersionRaw"

if ($cmakeVersion -lt [Version]"3.16") {
    Write-Error "CMake 3.16+ necessario. Versao atual: $cmakeVersion"
    exit 1
}

# ═══════════════════════════════════════════════════════════════════════════
# 2. VISUAL STUDIO / COMPILADOR
# ═══════════════════════════════════════════════════════════════════════════

$vsPath = $null
$vsGenerator = "Ninja"

# Procura vswhere.exe
$vswhere = Get-Command "vswhere" -ErrorAction SilentlyContinue
if (-not $vswhere) {
    foreach ($c in @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
                     "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe")) {
        if (Test-Path $c) { $vswhere = $c; break }
    }
}

if ($vswhere) {
    $vswherePath = if ($vswhere -is [string]) { $vswhere } else { $vswhere.Source }
    $vsInstallPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath

    if ($vsInstallPath) {
        $vsVersion = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop -property catalog_productLineVersion
        Write-Success "Visual Studio $vsVersion: $vsInstallPath"
        $vsPath = $vsInstallPath
        $vsGenerator = if ($vsVersion -eq "2022") { "Visual Studio 17 2022" } else { "Visual Studio 16 2019" }
    } else {
        Write-Warn "Visual Studio encontrado mas sem workload 'Desktop development with C++'"
        Write-Warn "  Execute o Visual Studio Installer e adicione o workload C++"
    }
}

if (-not $vsPath) {
    # Fallback: busca manual
    foreach ($base in @("${env:ProgramFiles}\Microsoft Visual Studio\2022",
                        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019")) {
        foreach ($ed in @("Community", "Professional", "Enterprise", "BuildTools")) {
            $p = "$base\$ed"
            if (Test-Path "$p\VC\Auxiliary\Build\vcvars64.bat") {
                $vsPath = $p
                $vsGenerator = if ($base -match "2022") { "Visual Studio 17 2022" } else { "Visual Studio 16 2019" }
                Write-Success "Visual Studio encontrado: $vsPath"
                break
            }
        }
        if ($vsPath) { break }
    }
}

if (-not $vsPath) {
    if (Test-Command "g++") {
        Write-Warn "Visual Studio nao encontrado. Usando MinGW/GCC."
        $vsGenerator = "MinGW Makefiles"
    } elseif (Test-Command "clang++") {
        Write-Warn "Visual Studio nao encontrado. Usando Clang + Ninja."
        $vsGenerator = "Ninja"
    } else {
        Write-Error "Nenhum compilador C++ encontrado!"
        Write-Host "  Instale o Visual Studio Community: https://visualstudio.microsoft.com/" -ForegroundColor Yellow
        Write-Host "  (selecione 'Desktop development with C++')" -ForegroundColor Yellow
        exit 1
    }
}

# ═══════════════════════════════════════════════════════════════════════════
# 3. OBS STUDIO SDK
# ═══════════════════════════════════════════════════════════════════════════

$obsRoot = $null
foreach ($p in @("$env:ProgramFiles\obs-studio", "${env:ProgramFiles(x86)}\obs-studio")) {
    if (Test-Path $p) { $obsRoot = $p; break }
}

if (-not $obsRoot) {
    Write-Error "OBS Studio nao encontrado! Instale em: https://obsproject.com/download"
    exit 1
}

Write-Success "OBS Studio SDK: $obsRoot"

$obsCmakeDir = "$obsRoot\lib\cmake"
$hasOBSConfig = Test-Path "$obsCmakeDir\libobs\libobsConfig.cmake"

if (-not $hasOBSConfig) {
    Write-Warn "SDK de desenvolvimento OBS nao encontrado em $obsCmakeDir"
    Write-Warn "  O plugin requer os cabeçalhos e config do libobs para compilar."
    Write-Warn "  Considere instalar o OBS com o componente 'Development' ou extrair"
    Write-Warn "  os arquivos .h e .cmake manualmente para $obsRoot\include\ e $obsCmakeDir\"
}

# ═══════════════════════════════════════════════════════════════════════════
# 4. DIRETORIOS
# ═══════════════════════════════════════════════════════════════════════════

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

if (-not (Test-Path "$projectDir\CMakeLists.txt")) {
    Write-Error "CMakeLists.txt nao encontrado em $projectDir"
    Write-Host "  Execute este script a partir da raiz do obs-broadcast-plugin" -ForegroundColor Yellow
    exit 1
}

$buildDir = if ($OutDir) { $OutDir } else { "$projectDir\build" }

if (Test-Path $buildDir) {
    Write-Warn "Diretorio de build existe. Removendo para configuracao limpa..."
    Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

Write-Success "Projeto: $projectDir"
Write-Success "Build:   $buildDir"

# ═══════════════════════════════════════════════════════════════════════════
# 5. CMAKE CONFIGURE
# ═══════════════════════════════════════════════════════════════════════════

Write-Step "Configurando CMake..."

$cmakeArgs = @("-S", "$projectDir", "-B", "$buildDir", "-G", "$vsGenerator")

# -A x64 é válido apenas para generators Visual Studio
if ($vsGenerator -match "Visual Studio") {
    $cmakeArgs += "-A", "x64", "-T", "host=x64"
}

# CMAKE_PREFIX_PATH aponta para o diretorio onde estao os .cmake do OBS
if ($hasOBSConfig) {
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=`"$obsCmakeDir`""
}

Write-Host "  cmake $($cmakeArgs -join ' ')" -ForegroundColor Gray
Write-Host ""

& $cmakePath @cmakeArgs 2>&1 | ForEach-Object { Write-Host "  $_" }

if ($LASTEXITCODE -ne 0) {
    Write-Error "Falha na configuracao do CMake (exit code: $LASTEXITCODE)"
    exit $LASTEXITCODE
}

Write-Success "CMake configurado com sucesso!"

# ═══════════════════════════════════════════════════════════════════════════
# 6. BUILD
# ═══════════════════════════════════════════════════════════════════════════

Write-Step "Compilando (BuildType: $BuildType)..."

$buildArgs = @("--build", "$buildDir", "--config", "$BuildType", "--parallel")

Write-Host "  cmake $($buildArgs -join ' ')" -ForegroundColor Gray
Write-Host ""

& $cmakePath @buildArgs 2>&1 | ForEach-Object { Write-Host "  $_" }

if ($LASTEXITCODE -ne 0) {
    Write-Error "Falha na compilacao (exit code: $LASTEXITCODE)"
    Write-Host "  Verifique os erros acima, corrija e tente novamente." -ForegroundColor Yellow
    exit $LASTEXITCODE
}

# ═══════════════════════════════════════════════════════════════════════════
# 7. LOCALIZAR BINARIO
# ═══════════════════════════════════════════════════════════════════════════

$binaryName = "obs-broadcast-plugin.dll"
$binaryPath = Get-ChildItem -Path $buildDir -Recurse -Filter $binaryName | Select-Object -First 1

if ($binaryPath) {
    $fullPath = $binaryPath.FullName
    $sizeKB = [math]::Round((Get-Item $fullPath).Length / 1KB, 1)
    Write-Success "Plugin compilado: $fullPath ($sizeKB KB)"
} else {
    Write-Warn "Binario $binaryName nao encontrado. Verifique: $buildDir"
}

# ═══════════════════════════════════════════════════════════════════════════
# 8. INSTRUCOES DE INSTALACAO
# ═══════════════════════════════════════════════════════════════════════════

Write-Step "Build concluido com sucesso!"

$obsPluginDir = "$obsRoot\obs-plugins\64bit"
$obsDataDir = "$obsRoot\data\obs-plugins\obs-broadcast-plugin"

Write-Host @"

Para instalar o plugin no OBS:

  1. Copie o .dll para a pasta de plugins:
     Copy-Item "$buildDir\**\$binaryName" "$obsPluginDir\" -Force

  2. Copie a pasta data/ (shaders + locale):
     Copy-Item "$projectDir\data" "$obsDataDir\" -Recurse -Force

  3. Reinicie o OBS Studio

  4. Adicione uma Source: Broadcast Overlay System

"@ -ForegroundColor Green
