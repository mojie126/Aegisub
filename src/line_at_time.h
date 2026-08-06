// Copyright (c) 2026, Aegisub contributors
// 按时间查找字幕行的工具函数（头文件内联模板，便于单元测试）

#pragma once

namespace agi {
	/// @brief 查找指定时间对应的字幕行
	/// @tparam LineList 行指针列表容器，元素需具有 Start 与 End 成员（毫秒）
	/// @param lines 行指针列表（按网格行序排列）
	/// @param time_ms 目标时间（毫秒）
	/// @return 时间落在行区间内的行，优先返回包含该时间的行；
	///         若没有包含该时间的行则返回开始时间不晚于该时间的最后一行；
	///         若均不满足则返回空指针
	template <typename LineList>
	auto FindLineAtTime(LineList const& lines, int time_ms) -> typename LineList::value_type {
		typename LineList::value_type best = nullptr;
		for (auto line : lines) {
			if (line->Start <= time_ms && time_ms < line->End)
				return line;
			if (line->Start <= time_ms)
				best = line;
		}
		return best;
	}
}
