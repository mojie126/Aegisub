#!/usr/bin/env powershell
<#
.SYNOPSIS
    下载 VapourSynth 插件到指定目录，供开发环境运行时使用。

.DESCRIPTION
    从 GitHub 下载 L-SMASH-Works、BestSource、SCXVid、WWXD 插件 DLL，
    放置到 <OutputDir>/vapoursynth/ 目录。
    已存在的插件不会重复下载。
    下载统一走 api.github.com 的 release 资产端点（github.com 的
    releases/download 域名在部分网络环境下连接不稳定）。

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
if (!(Test-Path $VsPluginDir))
{
	New-Item -ItemType Directory -Path $VsPluginDir -Force | Out-Null
}

$TempDir = Join-Path $OutputDir "vs-plugins-temp"
if (!(Test-Path $TempDir))
{
	New-Item -ItemType Directory -Path $TempDir -Force | Out-Null
}

$GitHeaders = @{ }
if (Test-Path 'Env:GITHUB_TOKEN')
{
	$GitHeaders = @{ 'Authorization' = 'Bearer ' + $Env:GITHUB_TOKEN }
}

# 支持通过 HTTPS_PROXY/HTTP_PROXY 环境变量使用代理下载（如本地代理软件），
# 未设置环境变量时保持直连
$Proxy = if ($Env:HTTPS_PROXY)
{
	$Env:HTTPS_PROXY
}
elseif ($Env:HTTP_PROXY)
{
	$Env:HTTP_PROXY
}
else
{
	$null
}
$ReqArgs = @{ UseBasicParsing = $true }
if ($Proxy)
{
	$ReqArgs['Proxy'] = $Proxy
}

# 从指定 release 下载第一个名称匹配的资产
# github.com 的 releases/download 直链在部分网络环境下会连接重置，
# 改为通过 api.github.com 的资产端点下载（重定向由 Invoke-WebRequest 自动跟随）
function Invoke-GitHubAssetDownload
{
	param(
		[string]$Repo,
		[string]$ReleasePath,
		[string]$NamePattern,
		[string]$OutFile
	)
	$rel = Invoke-WebRequest "https://api.github.com/repos/$Repo/releases/$ReleasePath" `
		-Headers $GitHeaders @ReqArgs | ConvertFrom-Json
	$asset = $rel.assets | Where-Object { $_.name -match $NamePattern } | Select-Object -First 1
	if (-not $asset)
	{
		throw "No asset matching '$NamePattern' in $Repo release $ReleasePath"
	}
	Write-Host "  downloading asset: $( $asset.name )"
	$assetHeaders = $GitHeaders.Clone()
	$assetHeaders['Accept'] = 'application/octet-stream'
	$url = "https://api.github.com/repos/$Repo/releases/assets/$( $asset.id )"
	Invoke-WebRequest $url -Headers $assetHeaders @ReqArgs -OutFile $OutFile
}

Write-Host "VapourSynth plugin directory: $VsPluginDir"

# L-SMASH-Works（AkarinVS 分支 — libvslsmashsource.dll）
if (!(Test-Path (Join-Path $VsPluginDir "libvslsmashsource.dll")))
{
	Write-Host "Downloading L-SMASH-Works (AkarinVS)..."
	$zip = Join-Path $TempDir "lsmas-akarin.zip"
	Invoke-GitHubAssetDownload "AkarinVS/L-SMASH-Works" "latest" 'release-x86_64-cachedir-cwd\.zip$' $zip
	$extractDir = Join-Path $TempDir "lsmas-akarin"
	Expand-Archive -LiteralPath $zip -DestinationPath $extractDir -Force
	$dll = Get-ChildItem -Path $extractDir -Recurse -Filter "libvslsmashsource.dll" | Select-Object -First 1
	if ($dll)
	{
		Copy-Item $dll.FullName $VsPluginDir -Force
		Write-Host "  -> libvslsmashsource.dll installed"
	}
	else
	{
		Write-Warning "libvslsmashsource.dll not found in archive"
	}
}
else
{
	Write-Host "L-SMASH-Works (AkarinVS): already exists"
}

# L-SMASH-Works（HomeOfAviSynthPlusEvolution 分支 — LSMASHSource.dll）
if (!(Test-Path (Join-Path $VsPluginDir "LSMASHSource.dll")))
{
	Write-Host "Downloading L-SMASH-Works (HoAE)..."
	$archive = Join-Path $TempDir "lsmas-hoae.7z"
	Invoke-GitHubAssetDownload "HomeOfAviSynthPlusEvolution/L-SMASH-Works" "latest" '\.7z$' $archive
	$extractDir = Join-Path $TempDir "lsmas-hoae"
	New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
	7z x $archive -o"$extractDir" -y | Out-Null
	$dll = Get-ChildItem -Path $extractDir -Recurse -Filter "LSMASHSource.dll" |
			Where-Object { $_.DirectoryName -match 'x64' } | Select-Object -First 1
	if ($dll)
	{
		Copy-Item $dll.FullName $VsPluginDir -Force
		Write-Host "  -> LSMASHSource.dll installed"
	}
	else
	{
		Write-Warning "LSMASHSource.dll not found in archive"
	}
}
else
{
	Write-Host "L-SMASH-Works (HoAE): already exists"
}

# BestSource
if (!(Test-Path (Join-Path $VsPluginDir "BestSource.dll")))
{
	Write-Host "Downloading BestSource..."
	# R20 起有 clang/msvc 两个资产，优先 msvc 版
	$archive = Join-Path $TempDir "bestsource.zip"
	Invoke-GitHubAssetDownload "vapoursynth/bestsource" "latest" 'msvc.*\.zip$' $archive
	$extractDir = Join-Path $TempDir "bestsource"
	New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
	7z x $archive -o"$extractDir" -y | Out-Null
	# R20 资产为带版本后缀的单文件 DLL，通配匹配并重命名
	$dll = Get-ChildItem -Path $extractDir -Recurse -Filter "BestSource-*.dll" | Select-Object -First 1
	if ($dll)
	{
		Copy-Item $dll.FullName (Join-Path $VsPluginDir "BestSource.dll") -Force
		Write-Host "  -> BestSource.dll installed"
	}
	else
	{
		Write-Warning "BestSource.dll not found in archive"
	}
}
else
{
	Write-Host "BestSource: already exists"
}

# SCXVid
if (!(Test-Path (Join-Path $VsPluginDir "libscxvid.dll")))
{
	Write-Host "Downloading SCXVid..."
	# latest release (v3) 无资产，固定使用带资产的 v1
	$archive = Join-Path $TempDir "scxvid.7z"
	Invoke-GitHubAssetDownload "dubhater/vapoursynth-scxvid" "tags/v1" 'win64\.7z$' $archive
	$extractDir = Join-Path $TempDir "scxvid"
	New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
	7z x $archive -o"$extractDir" -y | Out-Null
	$dll = Get-ChildItem -Path $extractDir -Recurse -Filter "libscxvid.dll" | Select-Object -First 1
	if ($dll)
	{
		Copy-Item $dll.FullName $VsPluginDir -Force
		Write-Host "  -> libscxvid.dll installed"
	}
	else
	{
		Write-Warning "libscxvid.dll not found in archive"
	}
}
else
{
	Write-Host "SCXVid: already exists"
}

# WWXD
if (!(Test-Path (Join-Path $VsPluginDir "libwwxd64.dll")))
{
	Write-Host "Downloading WWXD..."
	$dllPath = Join-Path $VsPluginDir "libwwxd64.dll"
	Invoke-GitHubAssetDownload "dubhater/vapoursynth-wwxd" "tags/v1.0" '^libwwxd64\.dll$' $dllPath
	Write-Host "  -> libwwxd64.dll installed"
}
else
{
	Write-Host "WWXD: already exists"
}

# 清理临时目录
if (Test-Path $TempDir)
{
	Remove-Item -Recurse -Force $TempDir
}

Write-Host "`nDone. VapourSynth plugins installed to: $VsPluginDir"
