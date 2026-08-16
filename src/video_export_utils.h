#pragma once

#include <libaegisub/vfr.h>

#include <vector>

namespace agi {
	/// @brief 判断多帧导出使用的起止帧范围是否有效
	/// @param start_frame 导出起始帧
	/// @param end_frame 导出结束帧
	/// @return 仅当结束帧严格大于起始帧时返回 true
	inline bool IsValidMultiFrameExportRange(const int start_frame, const int end_frame) {
		return end_frame > start_frame;
	}

	/// @brief 构建 GIF 各输出帧相对首帧的显示时间戳
	/// @param timecodes 视频时间码
	/// @param start_frame 导出起始帧
	/// @param end_frame 导出结束帧
	/// @return 以秒为单位、相对首帧归一化的 PTS 列表
	inline std::vector<double> BuildGifFramePresentationTimestamps(const vfr::Framerate &timecodes, const int start_frame, const int end_frame) {
		std::vector<double> timestamps;
		if (start_frame > end_frame)
			return timestamps;

		timestamps.reserve(end_frame - start_frame + 1);
		const int start_ms = timecodes.TimeAtFrame(start_frame, vfr::EXACT);
		for (int frame = start_frame; frame <= end_frame; ++frame) {
			const int frame_ms = timecodes.TimeAtFrame(frame, vfr::EXACT);
			timestamps.emplace_back((frame_ms - start_ms) / 1000.0);
		}
		return timestamps;
	}

	/// @brief 将各帧相对首帧的显示时间戳转换为动画导出各帧展示时长
	/// @param pts 相对首帧归一化的 PTS 列表（秒），与输出帧一一对应
	/// @return 每帧展示时长列表（毫秒），
	/// 时长取相邻帧 PTS 差值，末帧复用前一帧时长，最小值钳制为 1 毫秒
	inline std::vector<uint32_t> BuildAnimationFrameDelaysMs(const std::vector<double> &pts) {
		std::vector<uint32_t> delays;
		if (pts.empty())
			return delays;

		delays.reserve(pts.size());
		uint32_t last_delay_ms = 1;
		for (size_t i = 0; i < pts.size(); ++i) {
			const double next_pts = i + 1 < pts.size() ? pts[i + 1] : pts[i] + last_delay_ms / 1000.0;
			auto delay_ms = static_cast<uint32_t>(llround((next_pts - pts[i]) * 1000.0));
			if (delay_ms < 1)
				delay_ms = 1;
			last_delay_ms = delay_ms;
			delays.push_back(delay_ms);
		}
		return delays;
	}
}
