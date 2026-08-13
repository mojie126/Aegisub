#!/usr/bin/env powershell

# Aegisub 项目级 meson 配置脚本。
# 等效于在 Aegisub 仓库根目录执行 `meson setup`。

param (
	[Parameter(Mandatory = $false)]
	[Alias("v")]
	[string]$Version = "3.5.2",
	[Parameter(Mandatory = $false)]
	[Alias("e")]
	[ValidateSet("debug", "release")]
	[string]$BuildEnv = "debug",
	[Parameter(Mandatory = $false)]
	[Alias("c")]
	[ValidateSet("--wipe", "--reconfigure")]
	[string]$ConfigMethod = "--reconfigure",
	[Parameter(Mandatory = $false)]
	[Alias("h")]
	[switch]$Help,
	[Parameter(Mandatory = $false)]
	[Alias("b")]
	[string]$BuildDir,
	[Parameter(Mandatory = $false)]
	[Alias("x")]
	[switch]$Clean
)

function Show-Help
{
	Write-Host "Aegisub 项目级 meson 配置脚本" -ForegroundColor Cyan
	Write-Host ""
	Write-Host "等效于在 Aegisub 仓库根目录执行 meson setup，并按参数生成构建目录。" -ForegroundColor Gray
	Write-Host "脚本会固定附加项目必须的 -D 配置：其中 -Dbuildtype 由 -e 决定、-Dversion 由 -v 决定，其余为固定值，无需额外传参。" -ForegroundColor Gray
	Write-Host ""
	Write-Host "用法:" -ForegroundColor White
	Write-Host "  .\setup-aegisub.ps1 [-v 版本] [-e 环境] [-c 方式] [-b 目录] [-x 清理] [-h]"
	Write-Host ""
	Write-Host "参数:" -ForegroundColor White
	Write-Host "  -v, -Version       构建版本标识，用于构建目录命名 (默认: 3.5.2)"
	Write-Host "  -e, -BuildEnv      构建环境: debug 或 release (默认: debug)"
	Write-Host "  -c, -ConfigMethod  配置方式: --wipe 或 --reconfigure (默认: --reconfigure)"
	Write-Host "  -b, -BuildDir      自定义构建目录名称 (默认: build-<版本>-<环境>)"
	Write-Host "  -x, -Clean         仅清理 subprojects 下未追踪文件/文件夹（不执行 meson setup）"
	Write-Host "  -h, -Help          显示本帮助信息并退出"
	Write-Host ""
	Write-Host "示例:" -ForegroundColor White
	Write-Host "  .\setup-aegisub.ps1                                    # 默认 3.5.2 / debug / --reconfigure"
	Write-Host "  .\setup-aegisub.ps1 -e release                         # release 环境配置"
	Write-Host "  .\setup-aegisub.ps1 -c --wipe                          # 清除旧构建目录后重新配置"
	Write-Host "  .\setup-aegisub.ps1 -b build                           # 自定义构建目录"
	Write-Host "  .\setup-aegisub.ps1 -x                                 # 仅清理子项目，不执行配置"
	Write-Host "  .\setup-aegisub.ps1 -v 3.5.2 -e debug -c --reconfigure"
}

function Invoke-SubprojectClean
{
	Write-Host "清理子项目中的未追踪文件/文件夹..." -ForegroundColor Cyan
	Write-Warning "即将使用 'git clean -ffdx' 移除 subprojects 下所有未追踪文件与目录，此操作不可恢复！"

	$subDir = Join-Path $repoRoot "subprojects"
	if (-not (Test-Path $subDir))
	{
		Write-Host "未找到 subprojects 目录，跳过清理。" -ForegroundColor Yellow
		return
	}

	Push-Location $subDir
	try
	{
		git clean -ffdx
		if ($LASTEXITCODE -ne 0)
		{
			throw "git clean 失败 (exit code $LASTEXITCODE)"
		}
		Write-Host "子项目清理完成。" -ForegroundColor Green
	}
	finally
	{
		Pop-Location
	}
}

# 帮助优先：显示后直接退出
if ($Help)
{
	Show-Help
	exit 0
}

# 构建环境 -> meson buildtype
$buildtype = if ($BuildEnv -eq "release")
{
	"release"
}
else
{
	"debug"
}

# 构建目录：未显式指定时，按 版本 + 环境 组合出唯一目录，避免不同配置互相覆盖
$buildDir = if ($BuildDir)
{
	$BuildDir
}
else
{
	"build-$Version-$BuildEnv"
}

# 解析到 Aegisub 仓库根目录（脚本位于 tools/ 下）
$repoRoot = Join-Path $PSScriptRoot .. | Resolve-Path -ErrorAction SilentlyContinue
if (-not $repoRoot)
{
	throw "未找到 Aegisub 仓库根目录: $( Join-Path $PSScriptRoot .. )"
}

Write-Host "Aegisub 项目配置" -ForegroundColor Cyan
Write-Host "  版本 : $Version"
Write-Host "  环境 : $BuildEnv (buildtype=$buildtype)"
Write-Host "  方式 : $ConfigMethod"
Write-Host "  目录 : $buildDir"

Push-Location $repoRoot
try
{
	if ($Clean)
	{
		Invoke-SubprojectClean
		# 仅清理模式：完成子项目清理后直接退出，不执行 meson setup
		Write-Host "clean 模式：仅执行清理，已跳过 meson setup。" -ForegroundColor Green
		exit 0
	}

	# 项目必须的 meson -D 配置（固定值，无需每次传参，等价于手动 meson setup）
	$projectOptions = @(
		"-Dbuildtype=$buildtype"
		"-Ddefault_library=static"
		"-Dforce_fallback_for=zlib,harfbuzz,freetype2,fribidi,libpng"
		"-Dfreetype2:harfbuzz=disabled"
		"-Dharfbuzz:freetype=disabled"
		"-Dharfbuzz:cairo=disabled"
		"-Dharfbuzz:glib=disabled"
		"-Dharfbuzz:gobject=disabled"
		"-Dharfbuzz:tests=disabled"
		"-Dharfbuzz:docs=disabled"
		"-Dharfbuzz:icu=disabled"
		"-Dfribidi:tests=false"
		"-Dfribidi:docs=false"
		"-Dlibass:fontconfig=disabled"
		"-Dffmpeg:libdav1d=enabled"
		"-Davisynth=enabled"
		"-Dbestsource=enabled"
		"-Dvapoursynth=enabled"
		"-Dversion=$Version"
	)

	# 等效于在 Aegisub 根目录执行: meson setup <配置方式> <构建目录> <项目固定 -D 选项>
	# 用参数数组 + 调用运算符，避免 Invoke-Expression 把 -Dforce_fallback_for 中的逗号解析成数组而报错
	$mesonArgs = @($ConfigMethod, $buildDir) + $projectOptions
	Write-Host "执行: meson setup $( $mesonArgs -join ' ' )" -ForegroundColor Yellow
	& meson setup @mesonArgs
	if ($LASTEXITCODE -ne 0)
	{
		throw "meson setup 失败 (exit code $LASTEXITCODE)"
	}

	Write-Host "Aegisub 配置完成 (目录: $buildDir)" -ForegroundColor Green
}
finally
{
	Pop-Location
}
