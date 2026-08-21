#!/usr/bin/env powershell
<#
.SYNOPSIS
    安装 automation 脚本到指定目录，供开发环境运行时使用。

.DESCRIPTION
    将源码 automation 目录中的 autoload、demos、include、vapoursynth 子目录
    复制到 <OutputDir>/automation/，并从 GitHub 下载安装外部自动化脚本
    （DependencyControl、Aegisub-Motion、arch1t3cht-Aegisub-Scripts、
    Functional、ASSFoundation、YUtils、luajson、SubInspector）的最新版本，
    合并到同一目录，确保 dev 运行时可通过 ?data/automation/ 找到脚本。
    已存在的文件会直接覆盖，目标内残留的 meson.build 会被移除。

.PARAMETER OutputDir
    输出根目录（构建根目录），默认为脚本所在目录的上级的 build 目录。

.EXAMPLE
    .\setup-automation.ps1 -OutputDir build
#>

param (
	[Parameter(Position = 0)]
	[string]$OutputDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"

$SrcRoot = Join-Path $PSScriptRoot "..\automation"
$AutomationDir = Join-Path $OutputDir "automation"
if (!(Test-Path $AutomationDir))
{
	New-Item -ItemType Directory -Path $AutomationDir -Force | Out-Null
}

Write-Host "Automation directory: $AutomationDir"

# 1. 复制源码自带的 automation 脚本
foreach ($SubDir in @("autoload", "demos", "include", "vapoursynth"))
{
	$Src = Join-Path $SrcRoot $SubDir
	$Dst = Join-Path $AutomationDir $SubDir
	if (!(Test-Path $Src))
	{
		Write-Warning "Source directory not found: $Src"
		continue
	}
	New-Item -ItemType Directory -Path $Dst -Force | Out-Null
	Copy-Item -Path (Join-Path $Src "*") -Destination $Dst -Recurse -Force
	Write-Host "  -> $SubDir copied"
}

# 2. 从 GitHub 下载安装外部自动化脚本的最新版本
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

$TempDir = Join-Path $OutputDir "automation-setup-temp"
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

# 按映射复制文件或目录，目录复制其内容到目标目录
# Items 元素格式为 "来源=目标"，来源相对下载目录，目标相对 automation 目录
function Copy-AutomationItems
{
	param(
		[string]$FromRoot,
		[string[]]$Items
	)
	foreach ($item in $Items)
	{
		$parts = $item.Split('=', 2)
		$src = Join-Path $FromRoot $parts[0]
		$dst = Join-Path $AutomationDir $parts[1]
		if (!(Test-Path $src))
		{
			Write-Warning "Source not found: $src"
			continue
		}
		if ((Get-Item $src).PSIsContainer)
		{
			New-Item -ItemType Directory -Path $dst -Force | Out-Null
			Copy-Item -Path (Join-Path $src "*") -Destination $dst -Recurse -Force
		}
		else
		{
			New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
			Copy-Item -Path $src -Destination $dst -Force
		}
	}
}

# 下载 release zip 资产并解压合并，zip 内 automation 根目录优先 automation/ 子目录
# PatchTag 声明补丁适用的 release tag，仅版本匹配时应用补丁，
# 上游发布新版本后自动跳过（若新版本已修复 bug，可移除对应补丁参数）
# PatchFiles/PatchFroms/PatchTos 按索引配对，任一不匹配则整组补丁不应用
function Install-ReleaseScript
{
	param(
		[string]$Name,
		[string]$Repo,
		[string]$Pattern,
		[string]$PatchTag,
		[string[]]$PatchFiles,
		[string[]]$PatchFroms,
		[string[]]$PatchTos
	)
	# 统一走 api.github.com 的 release 资产端点（github.com 的 releases/download
	# 域名在部分网络环境下连接不稳定），latest release 即最新版本
	$rel = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers $GitHeaders @ReqArgs
	$asset = $rel.assets | Where-Object { $_.name -match $Pattern } | Select-Object -First 1
	if (-not $asset)
	{
		throw "No asset matching '$Pattern' in $Repo release $( $rel.tag_name )"
	}
	Write-Host "  downloading $Name $( $rel.tag_name ): $( $asset.name )"
	$assetHeaders = $GitHeaders.Clone()
	$assetHeaders['Accept'] = 'application/octet-stream'
	$zip = Join-Path $TempDir "$Name.zip"
	Invoke-WebRequest "https://api.github.com/repos/$Repo/releases/assets/$( $asset.id )" -Headers $assetHeaders @ReqArgs -OutFile $zip
	$extractDir = Join-Path $TempDir $Name
	Expand-Archive -LiteralPath $zip -DestinationPath $extractDir -Force
	$pkgRoot = Join-Path $extractDir "automation"
	if (!(Test-Path $pkgRoot))
	{
		$pkgRoot = $extractDir
	}
	# 合并到 automation 目录，跳过 tests 测试用例
	foreach ($item in Get-ChildItem -Path $pkgRoot | Where-Object { $_.Name -ne 'tests' })
	{
		Copy-Item -Path $item.FullName -Destination $AutomationDir -Recurse -Force
	}
	# 应用补丁，修复发布版本中上游已修复的 bug
	if ($PatchTag -and $rel.tag_name -ne $PatchTag)
	{
		Write-Host "  skipping patches for $Name $( $rel.tag_name ) (patches target $PatchTag)"
	}
	elseif ($PatchFiles.Count -gt 0)
	{
		# 原子性：先校验所有目标存在且查找串均能匹配，任一失败整组不应用
		$allMatch = $true
		$targets = @()
		for ($i = 0; $i -lt $PatchFiles.Count; $i++)
		{
			$target = Join-Path $AutomationDir $PatchFiles[$i]
			if (!(Test-Path $target))
			{
				Write-Warning "Patch target not found: $target"
				$allMatch = $false
				continue
			}
			$content = [System.IO.File]::ReadAllText($target)
			if (-not $content.Contains($PatchFroms[$i]))
			{
				$pattern = $PatchFroms[$i]
				$preview = if ($pattern.Length -gt 60)
				{
					$pattern.Substring(0, 60) + "..."
				}
				else
				{
					$pattern
				}
				Write-Warning "Patch pattern not found in $( $PatchFiles[$i] ): $preview"
				$allMatch = $false
			}
			$targets += @{ Path = $target; Content = $content }
		}
		if ($allMatch)
		{
			# 同一文件的多处补丁在内存中依次累积，最后一次写回，
			# 避免各补丁基于同一份预读内容写入时相互覆盖
			$patched = [ordered]@{}
			for ($i = 0; $i -lt $PatchFiles.Count; $i++)
			{
				$key = $PatchFiles[$i]
				if (!$patched.Contains($key))
				{
					$patched[$key] = $targets[$i].Content
				}
				$patched[$key] = $patched[$key].Replace($PatchFroms[$i], $PatchTos[$i])
				if ($patched[$key] -ne $targets[$i].Content)
				{
					Write-Host "  -> patched $( $key )"
				}
			}
			foreach ($key in $patched.Keys)
			{
				[System.IO.File]::WriteAllText((Join-Path $AutomationDir $key), $patched[$key], [System.Text.UTF8Encoding]::new($false))
			}
		}
		else
		{
			Write-Warning "Patches for $Name not applied (incomplete match)"
		}
	}
	Write-Host "  -> $Name installed"
}

# git clone 指定分支后按映射复制
function Install-GitScript
{
	param(
		[string]$Name,
		[string]$Repo,
		[string]$Branch,
		[string[]]$Copy
	)
	Write-Host "  cloning $Name..."
	$target = Join-Path $TempDir $Name
	$cloneArgs = @("clone", "--depth", "1")
	if ($Branch)
	{
		$cloneArgs += @("-b", $Branch)
	}
	$cloneArgs += @($Repo, $target)
	git @cloneArgs
	if ($LASTEXITCODE -ne 0)
	{
		throw "git clone failed for $Name"
	}
	Copy-AutomationItems -FromRoot $target -Items $Copy
	Write-Host "  -> $Name installed"
}

# 直接下载文件到指定位置
# Files 元素格式为 "URL=目标"，目标相对 automation 目录
function Install-RawFiles
{
	param(
		[string]$Name,
		[string[]]$Files
	)
	Write-Host "  downloading $Name..."
	foreach ($file in $Files)
	{
		$parts = $file.Split('=', 2)
		$dst = Join-Path $AutomationDir $parts[1]
		New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
		Invoke-WebRequest $parts[0] -OutFile $dst @ReqArgs
	}
	Write-Host "  -> $Name installed"
}

# 依次安装，安装布局与 packages/win_installer/fragment_automation.iss 保持一致
try
{

	# DependencyControl（release 最新版）
	# v0.8.2~v0.9.0 存在安装体验缺陷：Install Script 爬取 feed 期间未调用 aegisub.progress.task，
	# 进度对话框消息区一直空白（无反馈），且全部脚本已登记时列表为空且无提示，
	# 此处应用等价补丁（新版本若已修复可移除对应补丁参数）
	$ToolboxPatchScanning = @"
  macros, modules = {}, {}
  aegisub.progress.task "Scanning available feeds..."
  entries = crawlWithPrompt buildFeedInventory!, (fetched, known) ->
    aegisub.progress.set math.floor fetched * 100 / known
    -- 用户取消时中断 crawl，error 穿透到 crawlWithPrompt 的 pcall 静默退出
    error "cancelled", 0 if aegisub.progress.is_cancelled!
  -- 收尾到满格，抓取失败或被预算截断的 feed 不触发回调，进度会停在最后一次成功回调的值
  aegisub.progress.set 100
  return unless entries
"@
	$ToolboxPatchLoading = @"
  aegisub.progress.task "Loading feed data..."
  logger\log msgs.install.scanning, #entries
"@
	# FeedInventory 进度回调（crawl 每抓取一个 feed 通知调用方，
	# known 随 BFS 发现新 feed 动态增长，进度可能小幅回退属预期）
	$FeedInventoryPatchCrawl = @"
  crawl: (onProgress) =>
    inventoryEntriesByUrl = @__collectConfigFeeds!
    stats = @__crawlKnownFeeds inventoryEntriesByUrl, onProgress
"@
	$FeedInventoryPatchSink = @"
  __crawlKnownFeeds: (inventoryEntriesByUrl, onProgress) =>
"@
	$FeedInventoryPatchNotify = @"
      stats.fetched += 1
      inventoryEntriesByUrl[feedUrl].fetched = true
      onProgress stats.fetched, #queue - head + 1 + stats.fetched if onProgress
"@
	$ToolboxPatchCrawlWithPrompt = @"
crawlWithPrompt = (inventory, onProgress) ->
  feedTrust = DepCtrl.updater.feedTrust
  feedTrust\setPrompter promptUntrustedFeed
  ok, entries = pcall inventory.crawl, inventory, onProgress
  feedTrust\setPrompter nil
  if not ok and entries != "cancelled"
    error entries, 0
  return nil unless ok
  entries
"@
	$ToolboxPatchEmpty = @"
  moduleList, moduleMap = buildDlgList modules
  macroList, macroMap = buildDlgList macros

  if #moduleList == 0 and #macroList == 0
    unfetched = #[entry for entry in *entries when not entry.fetched]
    message = unfetched > 0 and ("No scripts are available to install (%d of %d known feeds could not be fetched).")\format(unfetched, #entries) or "All available scripts are already installed."
    aegisub.dialog.display {{class: "label", x: 0, y: 0, width: 1, height: 1, label: message}},
      {buttons.close}, {ok: buttons.close, cancel: buttons.close}
    return

  btn, res = aegisub.dialog.display getScriptListDlg macroList, moduleList
"@
	Install-ReleaseScript -Name "DependencyControl" -Repo "TypesettingTools/DependencyControl" -Pattern "\.zip$" -PatchTag "v0.9.0" -PatchFiles @("autoload/l0.DependencyControl.Toolbox.moon", "autoload/l0.DependencyControl.Toolbox.moon", "autoload/l0.DependencyControl.Toolbox.moon", "autoload/l0.DependencyControl.Toolbox.moon", "include/l0/DependencyControl/FeedInventory.moon", "include/l0/DependencyControl/FeedInventory.moon", "include/l0/DependencyControl/FeedInventory.moon") -PatchFroms @("  macros, modules = {}, {}`n  entries = crawlWithPrompt buildFeedInventory!", "  logger\log msgs.install.scanning, #entries", "crawlWithPrompt = (inventory) ->`n  feedTrust = DepCtrl.updater.feedTrust`n  feedTrust\setPrompter promptUntrustedFeed`n  entries = inventory\crawl!`n  feedTrust\setPrompter nil`n  entries", "  moduleList, moduleMap = buildDlgList modules`n  macroList, macroMap = buildDlgList macros`n`n  btn, res = aegisub.dialog.display getScriptListDlg macroList, moduleList", "  crawl: =>`n    inventoryEntriesByUrl = @__collectConfigFeeds!`n    stats = @__crawlKnownFeeds inventoryEntriesByUrl", "  __crawlKnownFeeds: (inventoryEntriesByUrl) =>", "      stats.fetched += 1`n      inventoryEntriesByUrl[feedUrl].fetched = true") -PatchTos @($ToolboxPatchScanning, $ToolboxPatchLoading, $ToolboxPatchCrawlWithPrompt, $ToolboxPatchEmpty, $FeedInventoryPatchCrawl, $FeedInventoryPatchSink, $FeedInventoryPatchNotify)

	# Aegisub-Motion（DepCtrl 分支）
	Install-GitScript -Name "Aegisub-Motion" -Repo "https://github.com/TypesettingTools/Aegisub-Motion.git" -Branch "DepCtrl" -Copy @("a-mo.Aegisub-Motion.moon=autoload", "src=include/a-mo")

	# arch1t3cht-Aegisub-Scripts（PerspectiveMotion、Resample 及 arch 模块）
	Install-GitScript -Name "arch1t3cht-Aegisub-Scripts" -Repo "https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts.git" -Copy @("macros/arch.PerspectiveMotion.moon=autoload", "macros/arch.Resample.moon=autoload", "modules/arch=include/arch")

	# Functional
	Install-GitScript -Name "Functional" -Repo "https://github.com/TypesettingTools/Functional.git" -Copy @("Functional.moon=include/l0")

	# ASSFoundation
	Install-GitScript -Name "ASSFoundation" -Repo "https://github.com/TypesettingTools/ASSFoundation.git" -Copy @("l0=include/l0")

	# YUtils（v1.0.0 起按 DepCtrl 命名空间安装到 include/tstools/，
	# 通过 provides 别名注册 Yutils 供旧脚本 require）
	Install-GitScript -Name "YUtils" -Repo "https://github.com/TypesettingTools/YUtils.git" -Copy @("src/Yutils.lua=include/tstools")

	# 移除 YUtils 旧布局残留（include/Yutils.lua），避免绕过 DepCtrl 记录
	Remove-Item -Force (Join-Path $AutomationDir "include/Yutils.lua") -ErrorAction SilentlyContinue

	# luajson
	Install-GitScript -Name "luajson" -Repo "https://github.com/harningt/luajson.git" -Copy @("lua=include")

	# SubInspector，Inspector.moon 取 master 最新版，
	# v0.5.2 起仅有 mac 版 dylib，win64 dll 固定使用 v0.5.1
	# 按 DepCtrl 模块布局安装（include/SubInspector/），dll 按 requireffi 的
	# 搜索规则放 Inspector 子目录，并补装其依赖 requireffi，
	# 避免 updater 禁用时 DepCtrl 无法自动安装导致依赖解析失败
	Install-RawFiles -Name "SubInspector" -Files @("https://raw.githubusercontent.com/TypesettingTools/SubInspector/refs/heads/master/examples/Aegisub/Inspector.moon=include/SubInspector/Inspector.moon", "https://github.com/TypesettingTools/SubInspector/releases/download/v0.5.1/SubInspector-win64.dll=include/SubInspector/Inspector/SubInspector.dll", "https://github.com/TypesettingTools/ffi-experiments/releases/download/r3/requireffi.lua=include/requireffi/requireffi.lua")

	# 移除 SubInspector 旧布局残留（include/Inspector.moon、include/Inspector/）
	Remove-Item -Force (Join-Path $AutomationDir "include/Inspector.moon") -ErrorAction SilentlyContinue
	Remove-Item -Recurse -Force (Join-Path $AutomationDir "include/Inspector") -ErrorAction SilentlyContinue
}
finally
{
	# 清理临时目录，任一步骤失败也保证清理
	Remove-Item -Recurse -Force $TempDir
}

# 移除复制过程中带入的构建文件
Get-ChildItem -Path $AutomationDir -Recurse -Filter "meson.build" | Remove-Item -Force

Write-Host "`nDone. Automation scripts installed to: $AutomationDir"
