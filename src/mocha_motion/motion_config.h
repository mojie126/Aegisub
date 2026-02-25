// Copyright (c) 2024-2026, Aegisub contributors
// 运动追踪配置持久化
// 对应 MoonScript 版 Aegisub-Motion 的 ConfigHandler 模块
// 配置文件路径与字段名与 MoonScript 版保持一致，确保双向兼容
//
// 配置文件：?user/aegisub-motion.json（JSON 格式）
// JSON 结构：{ "main": { "xPosition": true, ... }, "__version": "..." }
// 字段名使用 camelCase 与 MoonScript 版保持一致

#pragma once

#include "motion_common.h"

#include <libaegisub/fs.h>

namespace mocha {

/// 配置持久化处理器
/// 与 MoonScript 插件版 ConfigHandler 兼容
///
/// 读取逻辑：加载已有 JSON → 仅更新已知字段（新字段保留默认值，废弃字段忽略）
/// 写入逻辑：先加载已有 JSON 保留其他 section → 更新 main section → 注入 __version → 写回
///
/// MoonScript 版配置键名（main section）：
///   xPosition, yPosition, origin, absPos, xScale, border, shadow, blur,
///   blurScale, xRotation, yRotation, zRotation, zPosition,
///   writeConf, relative, startFrame, linear, clipOnly,
///   rectClip, vectClip, rcToVc, killTrans
///
/// C++ 扩展键名（MoonScript 版会忽略）：
///   preview, reverseTracking
class MotionConfig {
public:
	/// 配置文件名（与 MoonScript 版一致）
	static constexpr auto CONFIG_FILENAME = "aegisub-motion.json";

	/// 加载配置：从 ?user/aegisub-motion.json 读取 main section
	/// @param opts  输出目标，只更新匹配的已知字段
	/// @return 是否成功读取（文件不存在返回 false）
	static bool Load(MotionOptions& opts);

	/// 加载 clip 配置：从 ?user/aegisub-motion.json 读取 clip section
	/// @param opts  输出目标
	/// @return 是否成功读取
	static bool LoadClip(ClipTrackOptions& opts);

	/// 保存配置：将 MotionOptions 写入 ?user/aegisub-motion.json 的 main section
	/// 会保留 JSON 中已有的其他 section（clip、trim 等）
	/// @param opts           要保存的选项
	/// @param version        版本号，写入 __version 字段
	static bool Save(const MotionOptions& opts, const std::string& version = "1.0.0");

	/// 保存 clip 配置：将 ClipTrackOptions 写入 clip section
	/// @param opts  要保存的 clip 选项
	static bool SaveClip(const ClipTrackOptions& opts);

	/// 删除配置文件
	static void Remove();

private:
	/// 获取配置文件的完整路径
	static agi::fs::path GetConfigPath();
};

} // namespace mocha
