// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪配置持久化
// 复用 aegisub-motion.json 的 "persp" section，与 MotionConfig 共享同一个配置文件

#pragma once

#include "perspective_processor.h"

namespace mocha {
	/// 透视追踪配置持久化
	/// 与 MotionConfig 共享 aegisub-motion.json，读写 "persp" section
	class PerspectiveConfig {
	public:
		/// 加载配置：从 ?user/aegisub-motion.json 读取 persp section
		/// @param opts 输出目标，只更新匹配的已知字段
		/// @return 是否成功读取
		static bool Load(PerspectiveOptions &opts);

		/// 保存配置：将 PerspectiveOptions 写入 persp section
		/// @param opts 要保存的选项
		/// @return 是否成功写入
		static bool Save(const PerspectiveOptions &opts);
	};
} // namespace mocha
