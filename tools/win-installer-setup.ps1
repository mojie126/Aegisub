#!/usr/bin/env powershell

param (
	[Parameter(Position = 0)]
	[string]$BuildRoot,
	[Parameter(Position = 1)]
	[string]$SourceRoot
)

$InstallerDir = Join-Path $SourceRoot "packages\win_installer" | Resolve-Path
$DepsDir = Join-Path $BuildRoot "installer-deps"
if (!(Test-Path $DepsDir))
{
	New-Item -ItemType Directory -Path $DepsDir
}

$Env:BUILD_ROOT = $BuildRoot
$Env:SOURCE_ROOT = $SourceRoot

Set-Location $DepsDir

$GitHeaders = @{ }
if (Test-Path 'Env:GITHUB_TOKEN')
{
	$GitHeaders = @{ 'Authorization' = 'Bearer ' + $Env:GITHUB_TOKEN }
}

# DepCtrl（下载最新 release，zip 内为标准 automation 布局，
# 重组为 macros/、modules/ 供安装包组件引用）
if (!(Test-Path DependencyControl))
{
	$dcReleases = Invoke-WebRequest "https://api.github.com/repos/TypesettingTools/DependencyControl/releases/latest" -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
	$dcAsset = $dcReleases.assets | Where-Object { $_.name -match '\.zip$' } | Select-Object -First 1
	Invoke-WebRequest $dcAsset.browser_download_url -OutFile DependencyControl.zip -UseBasicParsing
	Expand-Archive -LiteralPath DependencyControl.zip -DestinationPath DependencyControl
	Remove-Item DependencyControl.zip
	Move-Item DependencyControl\automation\autoload DependencyControl\macros
	Move-Item DependencyControl\automation\include\l0 DependencyControl\modules
	Remove-Item -Recurse DependencyControl\automation
	# v0.8.1 存在 getUpdaterErrorMsg 调用 bug：该方法定义在 UpdateTask 类，
	# 而 ModuleLoader 在 Updater 实例上调用（类上取实例方法为 nil），导致依赖模块
	# 加载失败时连锁报错，上游 master 已修复，此处仅对 v0.8.1 应用等价补丁，
	# 新版本发布后自动跳过（若新版本已修复 bug，可删除本段补丁代码）
	if ($dcReleases.tag_name -eq 'v0.8.1')
	{
		$mlPath = Join-Path $DepsDir "DependencyControl\modules\DependencyControl\ModuleLoader.moon"
		$mlContent = [System.IO.File]::ReadAllText($mlPath)
		$mlFixed = $mlContent.Replace('@@updater.__class.getUpdaterErrorMsg', '@@updater\getUpdaterErrorMsg')
		if ($mlFixed -ne $mlContent)
		{
			[System.IO.File]::WriteAllText($mlPath, $mlFixed,[System.Text.UTF8Encoding]::new($false))
		}
		else
		{
			Write-Warning "Patch pattern not found in ModuleLoader.moon: @@updater.__class.getUpdaterErrorMsg"
		}
		# UpdateTask 的模块返回值被 withTestExports 包装，无法直接取类方法，
		# 故在 Updater.moon 的 msgs 表补充 updateError 消息并内联方法实现
		$upPath = Join-Path $DepsDir "DependencyControl\modules\DependencyControl\Updater.moon"
		$upContent = [System.IO.File]::ReadAllText($upPath)
		$upPatchMsgs = "  updateError: {`n    [UpdateStatus.UpToDate]: `"Couldn't complete the %s of %s '%s' because of a paradox: module not found but updater says up-to-date (%s)`"`n    [UpdateStatus.UpdaterDisabled]: `"Couldn't complete the %s of %s '%s' because the updater is disabled.`"`n    [UpdateStatus.InvalidNamespace]: `"Skipping %s of %s '%s': namespace '%s' doesn't conform to rules.`"`n    [UpdateStatus.Unmanaged]: `"Skipping %s of unmanaged %s '%s'.`"`n    [UpdateStatus.AnotherUpdateRunning]: `"Skipped %s of %s '%s': another update initiated by %s is already running.`"`n    [UpdateStatus.NoSuitablePackage]: `"The %s of %s '%s' failed because no suitable package could be found %s.`"`n    [UpdateStatus.NoInternet]: `"Skipped %s of %s '%s': an internet connection is currently not available.`"`n    [UpdateStatus.InvalidVersion]: `"Couldn't complete the %s of %s '%s' because the requested version is invalid: %s`"`n    [UpdateStatus.ProtectedInstall]: `"Skipped %s of %s '%s' because its entry point (%s) is in Aegisub's data automation directory. If it's managed by a system package manager, please update it through that instead.`"`n    [UpdateStatus.TaskAlreadyRunning]: `"Skipped %s of %s '%s': the update task is already running.`"`n    [UpdateStatus.RequirementsUnmet]: `"Couldn't complete the %s of %s '%s' because its requirements could not be satisfied: %s`"`n    [UpdateStatus.UntrustedFeed]: `"Couldn't complete the %s of %s '%s' because a suitable package was only found in an untrusted feed (%s). Add it to your trusted feeds to proceed.`"`n    [UpdateStatus.PinnedUnavailable]: `"Couldn't complete the %s of %s '%s' because its pinned package source is no longer available. Update or clear the pin to proceed.`"`n    [UpdateStatus.UserAborted]: `"Aborted the %s of %s '%s' at your request.`"`n    [UpdateStatus.BlockedFeed]: `"Couldn't complete the %s of %s '%s' because you blocked the feed (%s) it would be installed from.`"`n    [UpdateStatus.TempDirFailed]: `"Couldn't complete the %s of %s '%s': failed to create temporary download directory %s`"`n    [UpdateStatus.PathTraversal]: `"Aborted the %s of %s '%s' because it attempted to deploy a file (%s) outside of its namespaced path.`"`n    [UpdateStatus.BadHash]: `"Aborted the %s of %s '%s' because the feed contained a missing or malformed SHA-1 hash for file %s.`"`n    [UpdateStatus.MoveFailed]: `"Couldn't finish the %s of %s '%s' because some files couldn't be moved to their target location: %s`"`n    [UpdateStatus.ModuleNotFound]: `"The %s of %s '%s' succeeded, but the module couldn't be located by the module loader.`"`n    [UpdateStatus.ModuleLoadFailed]: `"The %s of %s '%s' succeeded, but an error occurred while loading the module: %s`"`n    [UpdateStatus.MissingVersionRecord]: `"The %s of %s '%s' succeeded, but it's missing a version record.`"`n    [UpdateStatus.RecordCreateFailed]: `"The %s of unmanaged %s '%s' succeeded, but an error occurred while creating a DependencyControl record: %s`"`n    component: `"Error (%d) in component %s during the %s of %s '%s':`n— %s`"`n    unknown: `"Couldn't complete the %s of %s '%s' (unrecognized updater status: %s).`"`n  }`n  updaterErrorComponent: {`"DownloadManager (adding download)`", `"DownloadManager`"}`n  scheduleUpdate: {"
		$upPatchMethod = "  getUpdaterErrorMsg: (code, name, scriptType, isInstall, detailMsg) =>`n    isInstall or= false`n    detailMsg or= `"`"`n    if code and code <= -100`n      return msgs.updateError.component\format -code, msgs.updaterErrorComponent[math.floor(-code/100)],`n        domain.terms.isInstall[isInstall], domain.terms.scriptType.singular[scriptType], name, detailMsg`n    template = msgs.updateError[code]`n    unless template`n      return msgs.updateError.unknown\format domain.terms.isInstall[isInstall],`n        domain.terms.scriptType.singular[scriptType], name, tostring code`n    return template\format domain.terms.isInstall[isInstall],`n      domain.terms.scriptType.singular[scriptType],`n      name, detailMsg"
		$upFixed = $upContent.Replace('  scheduleUpdate: {', $upPatchMsgs)
		$upFixed = $upFixed.Replace('  @defaultOrphanTimeout = 50', $upPatchMethod.TrimEnd() + "`n  @defaultOrphanTimeout = 50")
		if ($upFixed -ne $upContent)
		{
			[System.IO.File]::WriteAllText($upPath, $upFixed,[System.Text.UTF8Encoding]::new($false))
		}
		else
		{
			Write-Warning "Patch pattern not found in Updater.moon: scheduleUpdate / @defaultOrphanTimeout"
		}
	}
	else
	{
		Write-Host "DependencyControl $( $dcReleases.tag_name ): no patch needed"
	}
}

# Aegisub-Motion
if (!(Test-Path Aegisub-Motion))
{
	git clone https://github.com/TypesettingTools/Aegisub-Motion.git
	Set-Location Aegisub-Motion
	git checkout DepCtrl
	Set-Location $DepsDir
}

# arch1t3cht-Aegisub-Scripts
if (!(Test-Path arch1t3cht-Aegisub-Scripts))
{
	git clone https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts.git
	Set-Location arch1t3cht-Aegisub-Scripts
	Set-Location $DepsDir
}

# Functional
if (!(Test-Path Functional))
{
	git clone https://github.com/TypesettingTools/Functional.git
}

# ASSFoundation
if (!(Test-Path ASSFoundation))
{
	git clone https://github.com/TypesettingTools/ASSFoundation.git
}

# SubInspector
if (!(Test-Path SubInspector))
{
	New-Item -ItemType Directory SubInspector
	$InspectorUrl = "https://raw.githubusercontent.com/TypesettingTools/SubInspector/refs/heads/master/examples/Aegisub/Inspector.moon"
	Invoke-WebRequest $InspectorUrl -OutFile SubInspector/Inspector.moon -UseBasicParsing

	New-Item -ItemType Directory SubInspector/Inspector
	$SubInspectorUrl = "https://github.com/TypesettingTools/SubInspector/releases/download/v0.5.1/SubInspector-win64.dll"
	Invoke-WebRequest $SubInspectorUrl -OutFile SubInspector/Inspector/SubInspector.dll -UseBasicParsing
}

# YUtils
if (!(Test-Path YUtils))
{
	git clone https://github.com/TypesettingTools/YUtils.git
}

# luajson
if (!(Test-Path luajson))
{
	git clone https://github.com/harningt/luajson.git
}

# Avisynth
if (!(Test-Path AviSynthPlus64))
{
	$avsReleases = Invoke-WebRequest "https://api.github.com/repos/AviSynth/AviSynthPlus/releases/latest" -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
	$avsUrl = $avsReleases.assets[0].browser_download_url
	Invoke-WebRequest $avsUrl -OutFile AviSynthPlus.7z -UseBasicParsing
	7z x AviSynthPlus.7z
	Rename-Item (Get-ChildItem -Filter "AviSynthPlus_*" -Directory) AviSynthPlus64
	Remove-Item AviSynthPlus.7z
}

# VSFilter
if (!(Test-Path VSFilter))
{
	$vsFilterDir = New-Item -ItemType Directory VSFilter
	Set-Location $vsFilterDir
	$vsFilterReleases = Invoke-WebRequest "https://api.github.com/repos/pinterf/xy-VSFilter/releases/latest" -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
	$vsFilterUrl = $vsFilterReleases.assets[0].browser_download_url
	Invoke-WebRequest $vsFilterUrl -OutFile VSFilter.7z -UseBasicParsing
	7z x VSFilter.7z
	Remove-Item VSFilter.7z
	Set-Location $DepsDir
}

### VapourSynth plugins

# L-SMASH-Works
if (!(Test-Path L-SMASH-Works))
{
	New-Item -ItemType Directory L-SMASH-Works
	$lsmasReleases = Invoke-WebRequest "https://api.github.com/repos/AkarinVS/L-SMASH-Works/releases/latest" -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
	$lsmasUrl = "https://github.com/AkarinVS/L-SMASH-Works/releases/download/" + $lsmasReleases.tag_name + "/release-x86_64-cachedir-cwd.zip"
	Invoke-WebRequest $lsmasUrl -OutFile release-x86_64-cachedir-cwd.zip -UseBasicParsing
	Expand-Archive -LiteralPath release-x86_64-cachedir-cwd.zip -DestinationPath L-SMASH-Works
	Remove-Item release-x86_64-cachedir-cwd.zip
}
if (!(Test-Path L-SMASH-Works-new))
{
	$newLSMASHDir = New-Item -ItemType Directory L-SMASH-Works-new
	Set-Location $newLSMASHDir
	$lsmasReleases = Invoke-WebRequest "https://api.github.com/repos/HomeOfAviSynthPlusEvolution/L-SMASH-Works/releases/latest" -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
	$lsmasUrl = $lsmasReleases.assets[0].browser_download_url
	Invoke-WebRequest $lsmasUrl -OutFile L-SMASH-Works-new.7z -UseBasicParsing
	7z x L-SMASH-Works-new.7z
	Remove-Item L-SMASH-Works-new.7z
	Set-Location $DepsDir
}

# BestSource
if (!(Test-Path BestSource))
{
	$bsDir = New-Item -ItemType Directory BestSource
	Set-Location $bsDir
	# $basReleases = Invoke-WebRequest "https://api.github.com/repos/vapoursynth/bestsource/releases/latest" -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
	# $bsUrl = $basReleases.assets[0].browser_download_url
	# R20 起资产为带版本后缀的单文件 zip（R19 为 7z 且 DLL 名为 BestSource.dll）
	$bsUrl = "https://github.com/vapoursynth/bestsource/releases/download/R20/BestSource-R20-win64-msvc.zip"
	Invoke-WebRequest $bsUrl -OutFile bestsource.zip -UseBasicParsing
	7z x bestsource.zip -y
	Remove-Item bestsource.zip
	# 重命名为 BestSource.dll 供安装包组件引用
	Get-ChildItem -Filter "BestSource-*.dll" | Rename-Item -NewName "BestSource.dll" -Force
	Set-Location $DepsDir
}

# SCXVid
if (!(Test-Path SCXVid))
{
	$scxDir = New-Item -ItemType Directory SCXVid
	Set-Location $scxDir
	# latest release (v3) 无资产，固定使用带资产的 v1
	$scxUrl = "https://github.com/dubhater/vapoursynth-scxvid/releases/download/v1/vapoursynth-scxvid-v1-win64.7z"
	Invoke-WebRequest $scxUrl -OutFile vapoursynth-scxvid-v1-win64.7z -UseBasicParsing
	7z x vapoursynth-scxvid-v1-win64.7z
	Remove-Item vapoursynth-scxvid-v1-win64.7z
	Set-Location $DepsDir
}

# WWXD
if (!(Test-Path WWXD))
{
	New-Item -ItemType Directory WWXD
	$wwxdReleases = Invoke-WebRequest "https://api.github.com/repos/dubhater/vapoursynth-wwxd/releases/latest" -Headers $GitHeaders -UseBasicParsing | ConvertFrom-Json
	$wwxdUrl = "https://github.com/dubhater/vapoursynth-wwxd/releases/download/" + $wwxdReleases.tag_name + "/libwwxd64.dll"
	Invoke-WebRequest $wwxdUrl -OutFile WWXD/libwwxd64.dll -UseBasicParsing
}


# ffi-experiments
if (!(Test-Path ffi-experiments))
{
	Get-Command "moonc" # check to ensure Moonscript is present
	git clone https://github.com/TypesettingTools/ffi-experiments.git
	Set-Location ffi-experiments
	meson build -Ddefault_library=static
	if (!$?)
	{
		Exit $LASTEXITCODE
	}
	meson compile -C build
	if (!$?)
	{
		Exit $LASTEXITCODE
	}
	Set-Location $DepsDir
}

# VC++ redistributable
if (!(Test-Path VC_redist))
{
	$redistDir = New-Item -ItemType Directory VC_redist
	Invoke-WebRequest https://aka.ms/vs/18/release/VC_redist.x64.exe -OutFile "$redistDir\VC_redist.x64.exe" -UseBasicParsing
}

# XAudio2 redistributable
if (!(Test-Path XAudio2_redist))
{
	New-Item -ItemType Directory XAudio2_redist
	Invoke-WebRequest https://www.nuget.org/api/v2/package/Microsoft.XAudio2.Redist/1.2.13 -OutFile XAudio2Redist.zip
	Expand-Archive -LiteralPath XAudio2Redist.zip -DestinationPath XAudio2_redist
	Remove-Item XAudio2Redist.zip
}

# dictionaries
if (!(Test-Path dictionaries))
{
	New-Item -ItemType Directory dictionaries
	Invoke-WebRequest https://raw.githubusercontent.com/TypesettingTools/Aegisub-dictionaries/master/dicts/en_US.aff -OutFile dictionaries/en_US.aff -UseBasicParsing
	Invoke-WebRequest https://raw.githubusercontent.com/TypesettingTools/Aegisub-dictionaries/master/dicts/en_US.dic -OutFile dictionaries/en_US.dic -UseBasicParsing
}

# localization
Set-Location $BuildRoot
meson compile aegisub-gmo
if (!$?)
{
	Exit $LASTEXITCODE
}

# Invoke InnoSetup
$IssUrl = Join-Path $InstallerDir "aegisub_depctrl.iss"
iscc $IssUrl
if (!$?)
{
	Exit $LASTEXITCODE
}
