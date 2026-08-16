/// @file apng_encoder.cpp
/// @brief APNG 动画编码器实现

#include "apng_encoder.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace agi {
	namespace {
		/// PNG 文件签名
		constexpr std::array<uint8_t, 8> png_signature = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

		/// RGBA 像素字节数
		constexpr size_t kBytesPerPixel = 4;

		/// 扫描线过滤类型
		enum class LineFilter : uint8_t {
			NONE = 0, ///< 不过滤
			SUB = 1, ///< 减去左侧同通道字节
			UP = 2, ///< 减去上一行同位置字节
		};

		/// 向字节流末尾追加 32 位大端整数
		void AppendBE32(std::vector<uint8_t> &out, const uint32_t value) {
			out.push_back(static_cast<uint8_t>(value >> 24));
			out.push_back(static_cast<uint8_t>(value >> 16));
			out.push_back(static_cast<uint8_t>(value >> 8));
			out.push_back(static_cast<uint8_t>(value));
		}

		/// 向字节流末尾追加 16 位大端整数
		void AppendBE16(std::vector<uint8_t> &out, const uint16_t value) {
			out.push_back(static_cast<uint8_t>(value >> 8));
			out.push_back(static_cast<uint8_t>(value));
		}

		/// 计算过滤后扫描线的启发式得分，
		/// 按有符号字节解释取绝对值求和，值越小压缩效果越好
		uint64_t FilterScore(const uint8_t *line, const size_t size) {
			uint64_t score = 0;
			for (size_t i = 0; i < size; ++i)
				score += static_cast<uint64_t>(std::abs(static_cast<int8_t>(line[i])));
			return score;
		}
	}

	ApngEncoder::ApngEncoder(const std::wstring &output_path, const int width, const int height, const uint32_t total_frames, const uint32_t num_plays)
		: file(_wfopen(output_path.c_str(), L"wb"))
		, width(width)
		, height(height)
		, total_frames(total_frames)
		, num_plays(num_plays) {
		if (!file || width <= 0 || height <= 0 || total_frames == 0) {
			broken = true;
			return;
		}

		if (std::fwrite(png_signature.data(), 1, png_signature.size(), file) != png_signature.size()) {
			broken = true;
			return;
		}

		// IHDR 块，8 位 RGBA，无交错
		std::vector<uint8_t> ihdr;
		AppendBE32(ihdr, static_cast<uint32_t>(width));
		AppendBE32(ihdr, static_cast<uint32_t>(height));
		ihdr.push_back(8); // 位深
		ihdr.push_back(6); // 颜色类型 RGBA
		ihdr.push_back(0); // 压缩方法
		ihdr.push_back(0); // 过滤方法
		ihdr.push_back(0); // 交错方法
		if (!WriteChunk("IHDR", ihdr.data(), static_cast<uint32_t>(ihdr.size()))) {
			broken = true;
			return;
		}

		// acTL 块声明总帧数与循环次数，
		// 记录 num_frames 字段偏移，完成时实际帧数不足则回写修正
		actl_num_frames_offset = std::ftell(file) + 8;
		std::vector<uint8_t> actl;
		AppendBE32(actl, total_frames);
		AppendBE32(actl, num_plays);
		if (!WriteChunk("acTL", actl.data(), static_cast<uint32_t>(actl.size())))
			broken = true;
	}

/// @brief 重算 acTL 块数据与 CRC，
/// 帧数被回写修正后同步更新块尾 CRC，保证块校验有效
	static std::vector<uint8_t> MakeActlChunkData(const uint32_t num_frames, const uint32_t num_plays) {
		std::vector<uint8_t> actl;
		AppendBE32(actl, num_frames);
		AppendBE32(actl, num_plays);

		uLong crc = crc32(0L, Z_NULL, 0);
		crc = crc32(crc, reinterpret_cast<const Bytef *>("acTL"), 4);
		crc = crc32(crc, actl.data(), static_cast<uInt>(actl.size()));
		AppendBE32(actl, static_cast<uint32_t>(crc));
		return actl;
	}

	ApngEncoder::~ApngEncoder() {
		if (file)
			std::fclose(file);
	}

	bool ApngEncoder::WriteChunk(const char type[4], const void *data, const uint32_t size) const {
		if (broken || !file)
			return false;

		const std::array length = {
			static_cast<uint8_t>(size >> 24),
			static_cast<uint8_t>(size >> 16),
			static_cast<uint8_t>(size >> 8),
			static_cast<uint8_t>(size),
		};
		if (std::fwrite(length.data(), 1, length.size(), file) != length.size())
			return false;
		if (std::fwrite(type, 1, 4, file) != 4)
			return false;

		uLong crc = crc32(0L, Z_NULL, 0);
		crc = crc32(crc, reinterpret_cast<const Bytef *>(type), 4);
		if (data && size > 0) {
			if (std::fwrite(data, 1, size, file) != size)
				return false;
			crc = crc32(crc, static_cast<const Bytef *>(data), size);
		}

		const std::array crc_be = {
			static_cast<uint8_t>(crc >> 24),
			static_cast<uint8_t>(crc >> 16),
			static_cast<uint8_t>(crc >> 8),
			static_cast<uint8_t>(crc),
		};
		if (std::fwrite(crc_be.data(), 1, crc_be.size(), file) != crc_be.size())
			return false;
		return true;
	}

	bool ApngEncoder::EncodeFrameData(const uint8_t *rgba, std::vector<uint8_t> &compressed) const {
		const size_t stride = static_cast<size_t>(width) * kBytesPerPixel;
		std::vector<uint8_t> filtered;
		filtered.reserve((stride + 1) * static_cast<size_t>(height));

		// 每条扫描线尝试三种过滤器，选取得分最小者
		std::vector<uint8_t> line_none(stride + 1);
		std::vector<uint8_t> line_sub(stride + 1);
		std::vector<uint8_t> line_up(stride + 1);
		for (int y = 0; y < height; ++y) {
			const uint8_t *cur = rgba + y * stride;
			const uint8_t *prev = y > 0 ? rgba + (y - 1) * stride : nullptr;

			line_none[0] = static_cast<uint8_t>(LineFilter::NONE);
			std::memcpy(line_none.data() + 1, cur, stride);

			line_sub[0] = static_cast<uint8_t>(LineFilter::SUB);
			for (size_t i = 0; i < stride; ++i) {
				const uint8_t left = i >= kBytesPerPixel ? cur[i - kBytesPerPixel] : 0;
				line_sub[1 + i] = static_cast<uint8_t>(cur[i] - left);
			}

			line_up[0] = static_cast<uint8_t>(LineFilter::UP);
			for (size_t i = 0; i < stride; ++i) {
				const uint8_t above = prev ? prev[i] : 0;
				line_up[1 + i] = static_cast<uint8_t>(cur[i] - above);
			}

			const uint8_t *best = line_none.data();
			uint64_t best_score = FilterScore(line_none.data() + 1, stride);
			if (const uint64_t score = FilterScore(line_sub.data() + 1, stride); score < best_score) {
				best = line_sub.data();
				best_score = score;
			}
			// Up 过滤得分最低时选 Up，比较后得分不再使用
			if (const uint64_t score = FilterScore(line_up.data() + 1, stride); score < best_score)
				best = line_up.data();
			filtered.insert(filtered.end(), best, best + stride + 1);
		}

		compressed.resize(compressBound(static_cast<uLong>(filtered.size())));
		auto compressed_size = static_cast<uLongf>(compressed.size());
		if (compress2(
				compressed.data(), &compressed_size, filtered.data(),
				static_cast<uLong>(filtered.size()), Z_DEFAULT_COMPRESSION
			) != Z_OK)
			return false;
		compressed.resize(compressed_size);
		return true;
	}

	bool ApngEncoder::AddFrame(const uint8_t *rgba, const uint32_t delay_ms) {
		if (broken || finished || !file || frames_written >= total_frames)
			return false;

		std::vector<uint8_t> compressed;
		if (!EncodeFrameData(rgba, compressed))
			return false;

		// fcTL 块描述本帧尺寸与展示时长，延迟以毫秒表示
		std::vector<uint8_t> fctl;
		AppendBE32(fctl, next_sequence_number++);
		AppendBE32(fctl, static_cast<uint32_t>(width));
		AppendBE32(fctl, static_cast<uint32_t>(height));
		AppendBE32(fctl, 0); // x 偏移
		AppendBE32(fctl, 0); // y 偏移
		AppendBE16(fctl, static_cast<uint16_t>(std::clamp<uint32_t>(delay_ms, 1, 0xFFFF)));
		AppendBE16(fctl, 1000); // 分母固定为 1000，即毫秒
		fctl.push_back(0); // dispose 操作 NONE
		fctl.push_back(0); // blend 操作 SOURCE
		if (!WriteChunk("fcTL", fctl.data(), static_cast<uint32_t>(fctl.size())))
			return false;

		// 首帧写入默认图像 IDAT（不带序列号），后续帧写入带序列号的 fdAT
		if (frames_written == 0) {
			if (!WriteChunk("IDAT", compressed.data(), static_cast<uint32_t>(compressed.size())))
				return false;
		} else {
			std::vector<uint8_t> fdat;
			AppendBE32(fdat, next_sequence_number++);
			fdat.insert(fdat.end(), compressed.begin(), compressed.end());
			if (!WriteChunk("fdAT", fdat.data(), static_cast<uint32_t>(fdat.size())))
				return false;
		}

		++frames_written;
		return true;
	}

	bool ApngEncoder::Finish() {
		if (broken || finished || !file)
			return false;

		// 实际帧数与预期不一致（如用户取消导出）时回写修正 acTL 帧数，
		// 同时重算块 CRC，未写入任何帧则输出无意义，直接失败
		if (frames_written != total_frames) {
			if (frames_written == 0)
				return false;
			const std::vector<uint8_t> actl = MakeActlChunkData(frames_written, num_plays);
			if (std::fseek(file, actl_num_frames_offset, SEEK_SET) != 0 ||
				std::fwrite(actl.data(), 1, actl.size(), file) != actl.size() ||
				std::fseek(file, 0, SEEK_END) != 0)
				return false;
		}

		if (!WriteChunk("IEND", nullptr, 0))
			return false;

		std::fclose(file);
		file = nullptr;
		finished = true;
		return true;
	}
}
