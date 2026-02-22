/// @file dovi_probe.h
/// @brief 流级Dolby Vision探测工具
/// @ingroup video_input

#pragma once

#include <string>

#ifdef WITH_FFMPEG

/// @brief Dolby Vision 流级探测结果
struct DoviProbeResult {
	bool has_dovi = false;     ///< 是否存在 DOVI 配置记录
	int dv_profile = 0;       ///< DV Profile（5=IPT-PQ-C2, 7/8=HDR10兼容 等）
	int transfer = -1;        ///< 流级传输特性（AVColorTransferCharacteristic）
	int color_primaries = -1; ///< 流级色域原色（AVColorPrimaries）
};

/// @brief 使用 libavformat 探测视频文件的 Dolby Vision 流级配置
/// @param filepath 视频文件路径
/// @return 探测结果，has_dovi 为 true 时表示存在 DOVI 配置记录
///
/// 通过读取容器中第一条视频流的 AV_PKT_DATA_DOVI_CONF 元数据实现，
/// 仅解析流级元数据，不解码任何帧，开销极低。
DoviProbeResult ProbeDolbyVision(const std::string &filepath);

#endif // WITH_FFMPEG
