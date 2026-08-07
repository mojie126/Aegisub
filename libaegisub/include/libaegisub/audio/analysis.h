// Copyright (c) 2026, Aegisub contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file libaegisub/include/libaegisub/audio/analysis.h
/// @brief 音频波形聚合与频谱功率计算，供 MCP 工具与其他非 UI 场景使用
/// @ingroup audio

#pragma once

#include <cstdint>
#include <vector>

namespace agi {
class AudioProvider;

/// 波形聚合点：一段音频窗口内的峰值与均值
struct WaveformPoint {
	int16_t peak_min = 0;  ///< 窗口内最小采样值
	int16_t peak_max = 0;  ///< 窗口内最大采样值
	double avg_min = 0.0;  ///< 窗口内负半周均值
	double avg_max = 0.0;  ///< 窗口内正半周均值
};

/// @brief 计算音频波形聚合数据（每点覆盖一段采样窗口）
/// @param provider 音频提供者
/// @param start_sample 起始采样点（含）
/// @param end_sample 结束采样点（不含），需大于 start_sample
/// @param points 输出的聚合点数，上限 4096
/// @return 每点一个 WaveformPoint，长度不超过 points
std::vector<WaveformPoint> ComputeWaveform(AudioProvider const& provider, int64_t start_sample, int64_t end_sample, size_t points);

/// @brief 计算一段音频的频谱功率（低频在前）
/// @param provider 音频提供者
/// @param start_sample 起始采样点（含），读取 start_sample 起的 2*fft_size 个样本
/// @param fft_size FFT 点数（2 的幂，2^8 到 2^15）
/// @return fft_size 个功率值，静音为 0，满幅正弦峰值约 1.9~2.9（随 fft_size 变化，log10 归一化与 Aegisub 频谱渲染一致）
std::vector<float> ComputeSpectrum(AudioProvider const& provider, int64_t start_sample, size_t fft_size);
}
