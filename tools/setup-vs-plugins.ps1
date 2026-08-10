#!/usr/bin/env powershell
<#
.SYNOPSIS
    下载 VapourSynth 插件到指定目录，供开发环境运行时使用。

.DESCRIPTION
    从 GitHub 下载 L-SMASH-Works、BestSource、SCXVid、WWXD 插件 DLL，
    放置到 <OutputDir>/vapoursynth/ 目录。
    已存在的插件不会重复下载。

.PARAMETER OutputDir
    输出根目录，默认为脚本所在目录的上级的 build 目录。

.EXAMPLE
    .\setup-vs-plugins.ps1 -OutputDir build
#>

param (
    [Parameter(Position = 0)]
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"

$VsPluginDir = Join-Path $OutputDir "vapoursynth"
if (!(Test-Path $VsPluginDir)) {
    New-Item -ItemType Directory -Path $VsPluginDir -Force | Out-Null
}

$TempDir = Join-Path $OutputDir "vs-plugins-temp"
if (!(Test-Path $TempDir)) {
    New-Item -ItemType Directory -Path $TempDir -Force | Out-Null
}

$GitHeaders = @{}
if (Test-Path 'Env:GITHUB_TOKEN') {
    $GitHeaders = @{ 'Authorization' = 'Bearer ' + $Env:GITHUB_TOKEN }
}

Write-Host "VapourSynth plugin directory: $VsPluginDir"

# L-SMASH-Works（AkarinVS 分支 — libvslsmashsource.dll）
if (!(Test-Path (Join-Path $VsPluginDir "libvslsmashsource.dll"))) {
    Write-Host "Downloading L-SMASH-Works (AkarinVS)..."
    $rel = Invoke-WebRequest "https://api.github.com/repos/AkarinVS/L-SMASH-Works/releases/latest" `
        -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
    $url = "https://github.com/AkarinVS/L-SMASH-Works/releases/download/" +
        $rel.tag_name + "/release-x86_64-cachedir-cwd.zip"
    $zip = Join-Path $TempDir "lsmas-akarin.zip"
    Invoke-WebRequest $url -OutFile $zip -UseBasicParsing
    $extractDir = Join-Path $TempDir "lsmas-akarin"
    Expand-Archive -LiteralPath $zip -DestinationPath $extractDir -Force
    $dll = Get-ChildItem -Path $extractDir -Recurse -Filter "libvslsmashsource.dll" | Select-Object -First 1
    if ($dll) {
        Copy-Item $dll.FullName $VsPluginDir -Force
        Write-Host "  -> libvslsmashsource.dll installed"
    } else {
        Write-Warning "libvslsmashsource.dll not found in archive"
    }
} else {
    Write-Host "L-SMASH-Works (AkarinVS): already exists"
}

# L-SMASH-Works（HomeOfAviSynthPlusEvolution 分支 — LSMASHSource.dll）
if (!(Test-Path (Join-Path $VsPluginDir "LSMASHSource.dll"))) {
    Write-Host "Downloading L-SMASH-Works (HoAE)..."
    $rel = Invoke-WebRequest "https://api.github.com/repos/HomeOfAviSynthPlusEvolution/L-SMASH-Works/releases/latest" `
        -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
    $url = "https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works/releases/download/" +
        $rel.tag_name + "/L-SMASH-Works-r" + $rel.tag_name + ".7z"
    $archive = Join-Path $TempDir "lsmas-hoae.7z"
    Invoke-WebRequest $url -OutFile $archive -UseBasicParsing
    $extractDir = Join-Path $TempDir "lsmas-hoae"
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
    7z x $archive -o"$extractDir" -y | Out-Null
    $dll = Get-ChildItem -Path $extractDir -Recurse -Filter "LSMASHSource.dll" |
        Where-Object { $_.DirectoryName -match 'x64' } | Select-Object -First 1
    if ($dll) {
        Copy-Item $dll.FullName $VsPluginDir -Force
        Write-Host "  -> LSMASHSource.dll installed"
    } else {
        Write-Warning "LSMASHSource.dll not found in archive"
    }
} else {
    Write-Host "L-SMASH-Works (HoAE): already exists"
}

# BestSource
if (!(Test-Path (Join-Path $VsPluginDir "BestSource.dll"))) {
    Write-Host "Downloading BestSource..."
    $rel = Invoke-WebRequest "https://api.github.com/repos/vapoursynth/bestsource/releases/latest" `
        -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
    # R20 起有 clang/msvc 两个资产，优先 msvc 版
    $asset = $rel.assets | Where-Object { $_.name -match "msvc" } | Select-Object -First 1
    if (-not $asset) { $asset = $rel.assets[0] }
    $url = $asset.browser_download_url
    $archive = Join-Path $TempDir "bestsource.zip"
    Invoke-WebRequest $url -OutFile $archive -UseBasicParsing
    $extractDir = Join-Path $TempDir "bestsource"
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
    7z x $archive -o"$extractDir" -y | Out-Null
    # R20 资产为带版本后缀的单文件 DLL，通配匹配并重命名
    $dll = Get-ChildItem -Path $extractDir -Recurse -Filter "BestSource-*.dll" | Select-Object -First 1
    if ($dll) {
        Copy-Item $dll.FullName (Join-Path $VsPluginDir "BestSource.dll") -Force
        Write-Host "  -> BestSource.dll installed"
    } else {
        Write-Warning "BestSource.dll not found in archive"
    }
} else {
    Write-Host "BestSource: already exists"
}

# SCXVid
if (!(Test-Path (Join-Path $VsPluginDir "libscxvid.dll"))) {
    Write-Host "Downloading SCXVid..."
    # latest release (v3) 无资产，固定使用带资产的 v1
    $url = "https://github.com/dubhater/vapoursynth-scxvid/releases/download/v1/vapoursynth-scxvid-v1-win64.7z"
    $archive = Join-Path $TempDir "scxvid.7z"
    Invoke-WebRequest $url -OutFile $archive -UseBasicParsing
    $extractDir = Join-Path $TempDir "scxvid"
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
    7z x $archive -o"$extractDir" -y | Out-Null
    $dll = Get-ChildItem -Path $extractDir -Recurse -Filter "libscxvid.dll" | Select-Object -First 1
    if ($dll) {
        Copy-Item $dll.FullName $VsPluginDir -Force
        Write-Host "  -> libscxvid.dll installed"
    } else {
        Write-Warning "libscxvid.dll not found in archive"
    }
} else {
    Write-Host "SCXVid: already exists"
}

# WWXD
if (!(Test-Path (Join-Path $VsPluginDir "libwwxd64.dll"))) {
    Write-Host "Downloading WWXD..."
    $rel = Invoke-WebRequest "https://api.github.com/repos/dubhater/vapoursynth-wwxd/releases/latest" `
        -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
    $url = "https://github.com/dubhater/vapoursynth-wwxd/releases/download/" +
        $rel.tag_name + "/libwwxd64.dll"
    Invoke-WebRequest $url -OutFile (Join-Path $VsPluginDir "libwwxd64.dll") -UseBasicParsing
    Write-Host "  -> libwwxd64.dll installed"
} else {
    Write-Host "WWXD: already exists"
}

# 清理临时目录
if (Test-Path $TempDir) {
    Remove-Item -Recurse -Force $TempDir
}

Write-Host "`nDone. VapourSynth plugins installed to: $VsPluginDir"
