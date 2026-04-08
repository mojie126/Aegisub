/// @file dovi_probe.h
/// @brief 流级Dolby Vision探测工具
/// @ingroup video_input

#pragma once

#include <string>

/// @brief Dolby Vision 流级探测结果
struct DoviProbeResult {
	bool has_dovi = false;     ///< 是否存在 DOVI 配置记录
	int dv_profile = 0;       ///< DV Profile（5=IPT-PQ-C2, 7/8=HDR10兼容 等）
	int transfer = -1;        ///< 流级传输特性（AVColorTransferCharacteristic）
	int color_primaries = -1; ///< 流级色域原色（AVColorPrimaries）
};

/// @brief 将流级 Dolby Vision Profile 回填到已判定为 Dolby Vision 的 provider
/// @param is_dolby_vision 当前 provider 是否已判定为 Dolby Vision
/// @param current_profile 当前已持有的 Dolby Vision Profile
/// @param probe 流级探测结果
/// @return 若当前尚无 Profile 且流级探测已给出有效 Profile，则返回探测值；否则保持原值
inline int MergeDolbyVisionProfileFromProbe(bool is_dolby_vision, int current_profile, const DoviProbeResult &probe) {
	if (!is_dolby_vision || current_profile > 0)
		return current_profile;
	if (!probe.has_dovi || probe.dv_profile <= 0)
		return current_profile;
	return probe.dv_profile;
}

#ifdef WITH_FFMPEG

/// @brief 使用 libavformat 探测视频文件的 Dolby Vision 流级配置
/// @param filepath 视频文件路径
/// @return 探测结果，has_dovi 为 true 时表示存在 DOVI 配置记录
///
/// 通过读取容器中第一条视频流的 AV_PKT_DATA_DOVI_CONF 元数据实现，
/// 仅解析流级元数据，不解码任何帧，开销极低。
DoviProbeResult ProbeDolbyVision(const std::string &filepath);

#endif // WITH_FFMPEG
