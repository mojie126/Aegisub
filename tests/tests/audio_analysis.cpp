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

/// @file tests/audio_analysis.cpp
/// @brief 音频波形聚合与频谱功率计算单元测试

#include <main.h>

#include <libaegisub/audio/analysis.h>
#include <libaegisub/audio/fft.h>
#include <libaegisub/audio/provider.h>

#include <cmath>
#include <algorithm>

namespace {
/// 圆周率常量（避免依赖 MSVC 的 _USE_MATH_DEFINES）
constexpr double kPi = 3.14159265358979323846;
/// 16-bit 单声道正弦波测试音频提供者
struct SineProvider final : agi::AudioProvider {
	int freq = 1000;
	SineProvider(int64_t samples = 48000, int rate = 48000) {
		channels = 1;
		num_samples = samples;
		decoded_samples = num_samples;
		sample_rate = rate;
		bytes_per_sample = 2;
		float_samples = false;
	}
	void FillBuffer(void* buf, int64_t start, int64_t count) const override {
		auto* out = static_cast<int16_t*>(buf);
		for (int64_t i = 0; i < count; ++i) {
			const int64_t idx = start + i;
			out[i] = static_cast<int16_t>(16000.0 * std::sin(2.0 * 3.14159265358979323846 * freq * idx / sample_rate));
		}
	}
};
}

TEST(lagi_audio_analysis, fft_is_power_of_two) {
	FFT fft;
	EXPECT_FALSE(fft.IsPowerOfTwo(0));
	EXPECT_FALSE(fft.IsPowerOfTwo(1));
	EXPECT_FALSE(fft.IsPowerOfTwo(3));
	EXPECT_TRUE(fft.IsPowerOfTwo(2));
	EXPECT_TRUE(fft.IsPowerOfTwo(256));
	EXPECT_TRUE(fft.IsPowerOfTwo(2048));
}

TEST(lagi_audio_analysis, fft_sine_peak) {
	// 48kHz 采样下 3kHz 正弦波，2048 点 FFT 的峰值 bin 应为 3k/48k*2048 = 128
	SineProvider provider(48000);
	const size_t n = 2048;
	std::vector<float> input(n), real(n), imag(n);
	for (size_t i = 0; i < n; ++i) {
		const double t = static_cast<double>(i) / provider.GetSampleRate();
		input[i] = static_cast<float>(16000.0 * std::sin(2.0 * 3.14159265358979323846 * 3000.0 * t) / 32768.0);
	}
	FFT fft;
	fft.Transform(n, input.data(), real.data(), imag.data());
	size_t peak_bin = 0;
	float peak_mag = -1;
	for (size_t b = 0; b < n; ++b) {
		const float mag = real[b] * real[b] + imag[b] * imag[b];
		if (mag > peak_mag) {
			peak_mag = mag;
			peak_bin = b;
		}
	}
	EXPECT_EQ(128u, peak_bin);
}

TEST(lagi_audio_analysis, waveform_sine_peak_and_average) {
	// 1kHz 正弦波，1 秒区间 100 点：每点窗口约 480 样本
	SineProvider provider(48000);
	auto pts = agi::ComputeWaveform(provider, 0, 48000, 100);
	ASSERT_EQ(100u, pts.size());
	int16_t max_peak = 0;
	for (auto const& p : pts) {
		max_peak = std::max(max_peak, p.peak_max);
		EXPECT_GT(p.peak_max, 0);
		EXPECT_LT(p.peak_min, 0);
		// 正半周均值约为 16000*2/pi，负半周为其相反数；
		// 窗口边界截断导致相位不完整，均值存在数百单位偏差，容差放宽
		EXPECT_NEAR(p.avg_max, 16000.0 * 2.0 / kPi, 800.0);
		EXPECT_NEAR(p.avg_min, -16000.0 * 2.0 / kPi, 800.0);
	}
	// 峰值接近正弦振幅
	EXPECT_NEAR(max_peak, 16000, 1000);
}

TEST(lagi_audio_analysis, waveform_silence) {
	auto provider = agi::CreateDummyAudioProvider("dummy-audio:", nullptr);
	auto pts = agi::ComputeWaveform(*provider, 0, 44100, 64);
	ASSERT_EQ(64u, pts.size());
	for (auto const& p : pts) {
		EXPECT_EQ(0, p.peak_min);
		EXPECT_EQ(0, p.peak_max);
		EXPECT_EQ(0.0, p.avg_min);
		EXPECT_EQ(0.0, p.avg_max);
	}
}

TEST(lagi_audio_analysis, waveform_partial_range) {
	SineProvider provider(48000);
	auto pts = agi::ComputeWaveform(provider, 1000, 5000, 8);
	ASSERT_EQ(8u, pts.size());
	for (auto const& p : pts) {
		EXPECT_GT(p.peak_max, 0);
		EXPECT_LT(p.peak_min, 0);
	}
}

TEST(lagi_audio_analysis, waveform_invalid_args) {
	SineProvider provider(48000);
	EXPECT_THROW(agi::ComputeWaveform(provider, 48000, 48000, 10), agi::InvalidInputException);
	EXPECT_THROW(agi::ComputeWaveform(provider, 48000, 0, 10), agi::InvalidInputException);
	EXPECT_THROW(agi::ComputeWaveform(provider, 0, 48000, 0), agi::InvalidInputException);
}

TEST(lagi_audio_analysis, spectrum_sine_peak_bin) {
	// 48kHz 采样下 3kHz 正弦波，fft_size=1024 时窗长为 2048 样本：
	// 峰值 bin 应为 3k/48k*2048 = 128
	SineProvider provider(48000);
	provider.freq = 3000;
	auto powers = agi::ComputeSpectrum(provider, 0, 1024);
	ASSERT_EQ(1024u, powers.size());
	size_t peak_bin = static_cast<size_t>(std::max_element(powers.begin(), powers.end()) - powers.begin());
	EXPECT_GE(peak_bin, 120u);
	EXPECT_LE(peak_bin, 136u);
	// 峰值明显大于相邻低频段
	EXPECT_GT(powers[peak_bin], 0.5f);
}

TEST(lagi_audio_analysis, spectrum_silence) {
	auto provider = agi::CreateDummyAudioProvider("dummy-audio:", nullptr);
	auto powers = agi::ComputeSpectrum(*provider, 0, 512);
	ASSERT_EQ(512u, powers.size());
	for (float v : powers)
		EXPECT_NEAR(0.0f, v, 1e-6f);
}

TEST(lagi_audio_analysis, spectrum_invalid_fft_size) {
	SineProvider provider(48000);
	EXPECT_THROW(agi::ComputeSpectrum(provider, 0, 1000), agi::InvalidInputException);
	EXPECT_THROW(agi::ComputeSpectrum(provider, 0, 128), agi::InvalidInputException);
	EXPECT_THROW(agi::ComputeSpectrum(provider, 0, 65536), agi::InvalidInputException);
	EXPECT_THROW(agi::ComputeSpectrum(provider, -1, 1024), agi::InvalidInputException);
}
