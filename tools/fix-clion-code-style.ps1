#!/usr/bin/env powershell
# Aegisub 项目的 CLion 代码风格批量修正脚本
# 用于修正 CLion 格式化（Braces Placement -> Blocks 设为 Same line）产生的两类问题：
#   1. 裸块左花括号与上一条语句同行（如 `std::string x; {`、`} {`），拆分到下一行
#   2. namespace 内代码被缩进一层，但顶格的 Doxygen 注释未跟随缩进，补齐一个制表符
# 脚本通用，可处理任意 C/C++ 源码文件，默认指向 MCP 模块相关三个文件，
# 处理过程保持 UTF-8 无 BOM 与 CRLF 行尾不变
# 注意: 运行前请关闭 CLion 中打开的相关文件，否则 IDE 的保存可能覆盖脚本写入结果

param (
	[Parameter(Mandatory = $false)]
	[Alias("p")]
	[string[]]$Path = @(
	"libaegisub/common/mcp/server.cpp",
	"libaegisub/include/libaegisub/mcp/server.h",
	"src/mcp_server.cpp"
),
	[Parameter(Mandatory = $false)]
	[Alias("r")]
	[string]$Root = "",
	[Parameter(Mandatory = $false)]
	[Alias("b")]
	[switch]$BraceOnly,
	[Parameter(Mandatory = $false)]
	[Alias("c")]
	[switch]$CommentOnly,
	[Parameter(Mandatory = $false)]
	[Alias("h")]
	[switch]$Help
)

# 展开 Path 参数，兼容命令行以逗号分隔传入多个文件（-p "a,b"）
$Path = @($Path | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" })

function Show-Help
{
	Write-Host "Aegisub 项目的 CLion 代码风格批量修正脚本" -ForegroundColor Cyan
	Write-Host ""
	Write-Host "用途: 修正 CLion 格式化产生的两类问题" -ForegroundColor Gray
	Write-Host "  1. 裸块左花括号与上一条语句同行 (如 'std::string x; {'、'} {')" -ForegroundColor Gray
	Write-Host "  2. namespace 内顶格 Doxygen 注释与已缩进的代码不对齐" -ForegroundColor Gray
	Write-Host ""
	Write-Host "用法: .\fix-clion-code-style.ps1 [-p 文件...] [-r 仓库根] [-b] [-c] [-h]" -ForegroundColor White
	Write-Host ""
	Write-Host "参数:" -ForegroundColor White
	Write-Host "  -p, -Path          要处理的文件（相对仓库根路径），默认三个 MCP 文件" -ForegroundColor Gray
	Write-Host "  -r, -Root          仓库根目录，默认取脚本所在目录的上一级" -ForegroundColor Gray
	Write-Host "  -b, -BraceOnly     仅做裸块左花括号拆分" -ForegroundColor Gray
	Write-Host "  -c, -CommentOnly   仅做注释缩进补齐" -ForegroundColor Gray
	Write-Host "  -h, -Help          显示本帮助" -ForegroundColor Gray
	Write-Host ""
	Write-Host "示例:" -ForegroundColor White
	Write-Host "  .\fix-clion-code-style.ps1" -ForegroundColor Gray
	Write-Host "  .\fix-clion-code-style.ps1 -p libaegisub/common/mcp/server.cpp -b" -ForegroundColor Gray
	Write-Host "  .\fix-clion-code-style.ps1 -p ""libaegisub/common/mcp/server.cpp,src/mcp_server.cpp""" -ForegroundColor Gray
	Write-Host ""
	Write-Host "说明: 脚本幂等，重复执行不会产生额外改动" -ForegroundColor Gray
	Write-Host "注意: 运行前请关闭 CLion 中打开的相关文件，防止 IDE 保存覆盖脚本写入结果" -ForegroundColor Yellow
}

if ($Help)
{
	Show-Help
	exit 0
}

if (-not $Root)
{
	# 脚本位于 tools/ 下，仓库根为其上一级目录
	$Root = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) ".."
}
$Root = [System.IO.Path]::GetFullPath($Root)

# 拆分裸块左花括号
# 规则一: 块结束符与下一块开始符同行 "} {"，拆为两行
# 规则二: 函数签名与裸块同行 "void f() { {"，函数体花括号保持同行，裸块花括号换行
# 规则三: 语句与裸块同行 "xxx; {"（排除 else/do/try/catch/for/while/switch/if 等控制语句），拆为两行
function Fix-BareBraces($lines)
{
	$out = New-Object System.Collections.Generic.List[string]
	$count = 0
	foreach ($line in $lines)
	{
		# 规则二: 函数签名后跟裸块
		if ($line -match '^(\t*)[\w:<>*& ]+\([^)]*\) \{\s*\{$')
		{
			$head = $line -replace ' \{\s*\{$', ' {'
			$indent = $Matches[1]
			$out.Add($head)
			$out.Add($indent + "`t" + "{")
			$count++
			continue
		}
		# 规则一: 块结束接块开始
		if ($line -match '^(\t*)\} \{$')
		{
			$indent = $Matches[1]
			$out.Add($indent + "}")
			$out.Add($indent + "{")
			$count++
			continue
		}
		# 规则三: 语句后跟裸块
		if ($line -match '^(?<ind>\t*)(?<stmt>.+?); \{$' -and $line -notmatch '(else|do|try|catch|finally|for|while|switch|if) \{$')
		{
			$indent = $Matches['ind']
			$stmt = $Matches['stmt']
			$out.Add($indent + $stmt + ";")
			$out.Add($indent + "{")
			$count++
			continue
		}
		$out.Add($line)
	}
	return @{ Lines = $out; Count = $count }
}

# 补齐 namespace 内顶格 Doxygen 注释的缩进
# 以首个 namespace 行（namespace { 或 namespace agi::mcp {）为起点，
# 之后所有行首为 "///" 的注释行前加一个制表符，文件头的 @file/@brief 注释不受影响
function Fix-CommentIndent($lines)
{
	$start = -1
	for ($i = 0; $i -lt $lines.Count; $i++) {
		if ($lines[$i] -match '^namespace ')
		{
			$start = $i
			break
		}
	}
	if ($start -lt 0)
	{
		Write-Host "  WARN: 未找到 namespace 起点，跳过注释缩进" -ForegroundColor Yellow
		return @{ Lines = $lines; Count = 0 }
	}

	$count = 0
	for ($i = $start; $i -lt $lines.Count; $i++) {
		if ($lines[$i] -match '^///')
		{
			$lines[$i] = "`t" + $lines[$i]
			$count++
		}
	}
	return @{ Lines = $lines; Count = $count }
}

$totalFix = 0
$totalComment = 0
foreach ($rel in $Path)
{
	$full = [System.IO.Path]::GetFullPath((Join-Path $Root $rel))
	if (-not (Test-Path -LiteralPath $full))
	{
		Write-Host "SKIP: 文件不存在 $rel" -ForegroundColor Yellow
		continue
	}

	$lines = [System.IO.File]::ReadAllLines($full)
	$braceFix = 0
	$commentFix = 0

	if (-not $CommentOnly)
	{
		$result = Fix-BareBraces $lines
		$lines = $result.Lines
		$braceFix = $result.Count
	}
	if (-not $BraceOnly)
	{
		$result = Fix-CommentIndent $lines
		$lines = $result.Lines
		$commentFix = $result.Count
	}

	if ($braceFix -gt 0 -or $commentFix -gt 0)
	{
		# 保持 UTF-8 无 BOM 编码写入
		[System.IO.File]::WriteAllLines($full, $lines,[System.Text.UTF8Encoding]::new($false))
	}

	Write-Host ("{0}: 裸块拆分 {1} 处, 注释缩进 {2} 处" -f $rel, $braceFix, $commentFix)
	$totalFix += $braceFix
	$totalComment += $commentFix
}

Write-Host ""
Write-Host ("完成: 共处理 {0} 个文件, 裸块拆分 {1} 处, 注释缩进 {2} 处" -f $Path.Count, $totalFix, $totalComment) -ForegroundColor Green
