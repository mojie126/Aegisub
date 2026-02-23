# Aegisub 构建指南

## 构建环境要求

- **OS**: Windows 10/11 x64
- **编译器**: MSVC 19.50+ (Visual Studio 2022 v17.14+)
- **构建系统**: Meson ≥ 1.0.0, Ninja ≥ 1.12
- **其他工具**: Python 3, NASM, CMake ≥ 4.0

## 完全重置与干净构建

从零开始（模拟全新 clone）:

```powershell
# 1. 删除所有下载/解压的子项目（保留项目自有的 csri、iconv、packagefiles 和 .wrap 文件）
Get-ChildItem subprojects -Directory |
    Where-Object { $_.Name -notin @('csri', 'iconv', 'packagefiles') } |
    ForEach-Object { Remove-Item -Recurse -Force $_.FullName }

# 2. 删除构建目录
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue

# 3. 配置（所有子项目将自动下载）
meson setup build -Dbuildtype=release -Ddefault_library=static `
    "-Dforce_fallback_for=zlib,harfbuzz,freetype2,fribidi,libpng" `
    "-Dfreetype2:harfbuzz=disabled" "-Dharfbuzz:freetype=disabled" `
    "-Dharfbuzz:cairo=disabled" "-Dharfbuzz:glib=disabled" `
    "-Dharfbuzz:gobject=disabled" "-Dharfbuzz:tests=disabled" `
    "-Dharfbuzz:docs=disabled" "-Dharfbuzz:icu=disabled" `
    "-Dfribidi:tests=false" "-Dfribidi:docs=false" `
    "-Dlibass:fontconfig=disabled" "-Dffmpeg:libdav1d=enabled" `
    "-Davisynth=enabled" "-Dbestsource=enabled" "-Dvapoursynth=enabled" `
    "-Dversion=3.4.2-RC2"

# 4. 编译
meson compile -C build
```

## 增量构建

配置已存在时直接编译：

```powershell
meson compile -C build
```

## 重新配置（保留子项目缓存）

```powershell
meson setup --reconfigure build <选项同上>
```

如需完全重置配置（删除 build 目录内容后重新生成）：

```powershell
meson setup --wipe build <选项同上>
```

## 打包

```powershell
# 安装包
meson compile -C build win-installer

# 便携版
meson compile -C build win-portable
```

## CLion Meson 配置选项

```
-Ddefault_library=static -Dforce_fallback_for=zlib,harfbuzz,freetype2,fribidi,libpng -Dfreetype2:harfbuzz=disabled -Dharfbuzz:freetype=disabled -Dharfbuzz:cairo=disabled -Dharfbuzz:glib=disabled -Dharfbuzz:gobject=disabled -Dharfbuzz:tests=disabled -Dharfbuzz:docs=disabled -Dharfbuzz:icu=disabled -Dfribidi:tests=false -Dfribidi:docs=false -Dlibass:fontconfig=disabled -Dffmpeg:libdav1d=enabled -Davisynth=enabled -Dbestsource=enabled -Dvapoursynth=enabled
```

## 子项目补丁说明

项目通过 Meson 的 `patch_directory` 机制对以下子项目应用补丁：

| 子项目 | 补丁目录 | 用途 |
|--------|---------|------|
| wxWidgets | `subprojects/packagefiles/wxWidgets/` | pngprefix.h — 解决 wxpng 与 libpng16 双 libpng SIMD 函数符号冲突 |
| ICU | `subprojects/packagefiles/icu/` | 修复 MSVC 下 icudata 构建排除问题；调整 stubdata 链接依赖 |

补丁文件在 `meson setup` 时自动覆盖到子项目目录。`diff_files` 补丁（ffmpeg、x264、dav1d）在子项目下载时自动应用。

## 子项目目录结构

```
subprojects/
├── *.wrap                  # Meson wrap 文件（项目管理，勿删）
├── csri/                   # 项目自有子项目（git 跟踪）
├── iconv/                  # 项目自有子项目（git 跟踪）
├── packagefiles/           # 补丁文件（git 跟踪）
│   ├── wxWidgets/          #   wxWidgets 补丁
│   └── icu/                #   ICU 补丁（含 wrapdb 原始构建文件 + 自定义修改）
├── packagecache/           # 下载缓存（自动生成，可安全删除）
└── <其他>/                 # 解压的子项目（自动生成，可安全删除）
```

## 开发环境 VapourSynth 插件

VapourSynth 视频解码需要 L-SMASH-Works 等插件。打包时（win-installer/win-portable）会自动下载，但开发构建需手动获取：

```powershell
# 方式一：通过 meson 目标
meson compile -C build setup-vs-plugins

# 方式二：直接运行脚本
.\tools\setup-vs-plugins.ps1 -OutputDir build
```

插件将下载到 `build/vapoursynth/`，程序通过 `?data/vapoursynth/` 路径自动加载。

> 注意：需要 7-Zip（`7z` 命令）解压部分插件包。

## 开发环境翻译文件

Meson 生成的 `.mo` 翻译文件位于 `build/po/<lang>/LC_MESSAGES/`。程序通过 `?data/po/` 搜索路径在开发环境中直接加载，无需手动复制到 `locale/` 目录。安装后则从标准 `locale/` 目录加载。
