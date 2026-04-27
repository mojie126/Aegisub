#pragma once

#include <libaegisub/vfr.h>

#include <vector>

namespace agi {
	/// @brief 判断多帧导出使用的起止帧范围是否有效
	/// @param start_frame 导出起始帧
	/// @param end_frame 导出结束帧
	/// @return 仅当结束帧严格大于起始帧时返回 true
	inline bool IsValidMultiFrameExportRange(int start_frame, int end_frame) {
		return end_frame > start_frame;
	}

	/// @brief 构建 GIF 各输出帧相对首帧的显示时间戳
	/// @param timecodes 视频时间码
	/// @param start_frame 导出起始帧
	/// @param end_frame 导出结束帧
	/// @return 以秒为单位、相对首帧归一化的 PTS 列表
	inline std::vector<double> BuildGifFramePresentationTimestamps(const vfr::Framerate& timecodes, int start_frame, int end_frame) {
		std::vector<double> timestamps;
		if (start_frame > end_frame)
			return timestamps;

		timestamps.reserve(static_cast<size_t>(end_frame - start_frame + 1));
		const int start_ms = timecodes.TimeAtFrame(start_frame, vfr::EXACT);
		for (int frame = start_frame; frame <= end_frame; ++frame) {
			const int frame_ms = timecodes.TimeAtFrame(frame, vfr::EXACT);
			timestamps.emplace_back((frame_ms - start_ms) / 1000.0);
		}
		return timestamps;
	}
}
