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

/// @file libaegisub/audio/analysis.cpp
/// @brief 音频波形聚合与频谱功率计算实现
/// @ingroup audio
///
/// 波形聚合算法与 AudioWaveformRenderer 一致（每输出点统计窗口内峰值与均值），
/// 频谱功率算法与 AudioSpectrumRenderer 的非 FFTW3 路径一致（log10 归一化功率）

#include "libaegisub/audio/analysis.h"

#include "libaegisub/audio/fft.h"
#include "libaegisub/audio/provider.h"
#include "libaegisub/exception.h"

#include <algorithm>
#include <cmath>

namespace {
/// 相邻输出点之间的采样窗口大小，向上取整保证至少 1 个样本
int64_t samples_per_point(int64_t span, size_t points) {
	int64_t spp = (span + static_cast<int64_t>(points) - 1) / static_cast<int64_t>(points);
	return std::max<int64_t>(1, spp);
}

/// 单次读取窗口的最大样本数，超出时分块聚合，避免大区间低点数时分配巨量内存
constexpr int64_t MAX_CHUNK_SAMPLES = 1 << 20;

/// 累计一个采样块的峰值与均值到聚合点
void accumulate_block(agi::WaveformPoint& pt, int16_t const* buf, int64_t count,
                      double& avg_min_accum, double& avg_max_accum,
                      int64_t& min_count, int64_t& max_count) {
	for (int64_t si = 0; si < count; ++si) {
		const int16_t v = buf[static_cast<size_t>(si)];
		if (v > 0) {
			pt.peak_max = std::max(pt.peak_max, v);
			avg_max_accum += v;
			++max_count;
		} else {
			pt.peak_min = std::min(pt.peak_min, v);
			avg_min_accum += v;
			++min_count;
		}
	}
}
}

namespace agi {

std::vector<WaveformPoint> ComputeWaveform(AudioProvider const& provider, int64_t start_sample, int64_t end_sample, size_t points) {
	if (end_sample <= start_sample)
		throw agi::InvalidInputException("waveform range must be positive");
	if (points == 0)
		throw agi::InvalidInputException("waveform point count must be positive");
	points = std::min<size_t>(points, 4096);

	const int64_t span = end_sample - start_sample;
	const int64_t spp = ::samples_per_point(span, points);
	// 总窗口数可能少于 points（span 不能被 spp 整除时），按实际窗口数输出
	const size_t out_points = static_cast<size_t>(std::min<int64_t>(points, (span + spp - 1) / spp));

	std::vector<WaveformPoint> result;
	result.reserve(out_points);
	std::vector<int16_t> buffer(static_cast<size_t>(std::min<int64_t>(spp, ::MAX_CHUNK_SAMPLES)));

	int64_t cursor = start_sample;
	for (size_t i = 0; i < out_points; ++i) {
		// 最后一个窗口允许缩短到剩余样本数
		const int64_t count = std::min<int64_t>(spp, end_sample - cursor);

		WaveformPoint pt;
		double avg_min_accum = 0, avg_max_accum = 0;
		int64_t min_count = 0, max_count = 0;
		// 窗口大于单块容量时分块累计，峰值/均值结果与一次性处理一致
		int64_t done = 0;
		while (done < count) {
			const int64_t chunk = std::min<int64_t>(count - done, ::MAX_CHUNK_SAMPLES);
			provider.GetInt16MonoAudio(buffer.data(), cursor + done, chunk);
			::accumulate_block(pt, buffer.data(), chunk, avg_min_accum, avg_max_accum, min_count, max_count);
			done += chunk;
		}
		if (max_count > 0) pt.avg_max = avg_max_accum / static_cast<double>(max_count);
		if (min_count > 0) pt.avg_min = avg_min_accum / static_cast<double>(min_count);
		result.emplace_back(pt);
		cursor += spp;
	}
	return result;
}

std::vector<float> ComputeSpectrum(AudioProvider const& provider, int64_t start_sample, size_t fft_size) {
	// FFT 尺寸限定为 2^8 ~ 2^15，避免过大的内存与耗时
	if (fft_size < 256 || fft_size > 32768 || !FFT{}.IsPowerOfTwo(static_cast<unsigned int>(fft_size)))
		throw agi::InvalidInputException("fft_size must be a power of two between 256 and 32768");
	if (start_sample < 0)
		throw agi::InvalidInputException("start_sample must not be negative");

	// 读取 2*fft_size 个样本（与频谱渲染器的窗长一致）
	const size_t n = fft_size * 2;
	std::vector<int16_t> audio(n);
	provider.GetInt16MonoAudio(audio.data(), start_sample, static_cast<int64_t>(n));

	// 转浮点并做 FFT，算法与 AudioSpectrumRenderer::FillBlock 的非 FFTW3 路径一致
	std::vector<float> input(n), real(n), imag(n);
	for (size_t i = 0; i < n; ++i)
		input[i] = static_cast<float>(audio[i]) / 32768.0f;

	FFT fft;
	fft.Transform(n, input.data(), real.data(), imag.data());

	const float scale_factor = 9.0f / std::sqrt(2.0f * static_cast<float>(n));
	std::vector<float> result(fft_size);
	for (size_t b = 0; b < fft_size; ++b) {
		const float mag = std::sqrt(real[b] * real[b] + imag[b] * imag[b]);
		result[b] = std::log10(mag * scale_factor + 1.0f);
	}
	return result;
}
}
