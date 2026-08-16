/// @file apng_encoder.cpp
/// @brief APNG 编码器单元测试，
/// 校验输出文件块结构、CRC、帧序列号、延迟与像素还原

#include "apng_encoder.h"

#include <zlib.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
/// 测试用临时目录，测试结束后整体删除
class TempDir {
		std::filesystem::path dir;

	public:
		TempDir() {
			dir = std::filesystem::temp_directory_path() / "aegisub_apng_test";
			std::filesystem::create_directories(dir);
		}
		~TempDir() {
			std::error_code ec;
			std::filesystem::remove_all(dir, ec);
		}
		std::wstring File(const wchar_t *name) const { return (dir / name).wstring(); }
};

/// 从字节流读取 32 位大端整数
uint32_t ReadBE32(const std::vector<uint8_t> &buf, size_t offset) {
	return (static_cast<uint32_t>(buf[offset]) << 24) |
		(static_cast<uint32_t>(buf[offset + 1]) << 16) |
		(static_cast<uint32_t>(buf[offset + 2]) << 8) |
		static_cast<uint32_t>(buf[offset + 3]);
}

/// 从字节流读取 16 位大端整数
uint16_t ReadBE16(const std::vector<uint8_t> &buf, size_t offset) {
	return static_cast<uint16_t>((static_cast<uint16_t>(buf[offset]) << 8) | buf[offset + 1]);
}

struct Chunk {
		std::string type;
		std::vector<uint8_t> data;
		uint32_t stored_crc = 0;
};

/// 解析 PNG 文件中的全部块，并校验 CRC
std::vector<Chunk> ParseChunks(const std::vector<uint8_t> &file) {
	EXPECT_GE(file.size(), 8u);
	const uint8_t signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
	for (int i = 0; i < 8; ++i)
		EXPECT_EQ(signature[i], file[i]);

	std::vector<Chunk> chunks;
	size_t pos = 8;
	while (pos + 12 <= file.size()) {
		const uint32_t len = ReadBE32(file, pos);
		if (pos + 12 + len > file.size()) {
			ADD_FAILURE() << "truncated chunk at offset " << pos;
			break;
		}
		Chunk chunk;
		chunk.type.assign(file.begin() + pos + 4, file.begin() + pos + 8);
		chunk.data.assign(file.begin() + pos + 8, file.begin() + pos + 8 + len);
		chunk.stored_crc = ReadBE32(file, pos + 8 + len);

		// 用 zlib 独立复核块 CRC（覆盖类型与数据）
		uLong crc = crc32(0L, Z_NULL, 0);
		crc = crc32(crc, file.data() + pos + 4, 4 + len);
		EXPECT_EQ(static_cast<uint32_t>(crc), chunk.stored_crc);

		chunks.push_back(std::move(chunk));
		pos += 12 + len;
	}
	return chunks;
}

/// 读取整个文件
std::vector<uint8_t> ReadFile(const std::wstring &path) {
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// 解压 zlib 数据
std::vector<uint8_t> ZlibDecompress(const std::vector<uint8_t> &input, size_t expected_size) {
	std::vector<uint8_t> output(expected_size);
	uLongf output_size = static_cast<uLongf>(expected_size);
	EXPECT_EQ(Z_OK, uncompress(output.data(), &output_size, input.data(), static_cast<uLong>(input.size())));
	output.resize(output_size);
	return output;
}

/// 按过滤类型还原扫描线，还原后与原始 RGBA 数据比对
void UnfilterAndCompare(const std::vector<uint8_t> &filtered, int width, int height, const std::vector<uint8_t> &expected_rgba) {
	const size_t stride = static_cast<size_t>(width) * 4;
	ASSERT_EQ((stride + 1) * static_cast<size_t>(height), filtered.size());

	std::vector<uint8_t> raw(stride);
	std::vector<uint8_t> prev(stride, 0);
	for (int y = 0; y < height; ++y) {
		const uint8_t filter = filtered[static_cast<size_t>(y) * (stride + 1)];
		const uint8_t *line = filtered.data() + static_cast<size_t>(y) * (stride + 1) + 1;
		for (size_t i = 0; i < stride; ++i) {
			const uint8_t left = (i >= 4) ? raw[i - 4] : 0;
			const uint8_t value = line[i];
			switch (filter) {
				case 0: raw[i] = value; break;
				case 1: raw[i] = static_cast<uint8_t>(value + left); break;
				case 2: raw[i] = static_cast<uint8_t>(value + prev[i]); break;
				default: FAIL() << "unexpected filter type " << filter;
			}
		}
		EXPECT_EQ(0, memcmp(raw.data(), expected_rgba.data() + y * stride, stride))
			<< "frame row " << y << " mismatch";
		prev = raw;
	}
}

/// 构造测试帧，返回 width*height*4 的 RGBA 数据
std::vector<uint8_t> MakeTestFrame(int width, int height, uint8_t seed) {
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
	for (size_t i = 0; i < rgba.size(); ++i)
		rgba[i] = static_cast<uint8_t>(seed + i * 7);
	return rgba;
}
}

TEST(lagi_apng_encoder, writes_valid_chunk_structure) {
	TempDir tmp;
	constexpr int W = 4, H = 4;
	const auto frame0 = MakeTestFrame(W, H, 0);
	const auto frame1 = MakeTestFrame(W, H, 64);

	agi::ApngEncoder encoder(tmp.File(L"basic.apng"), W, H, 2);
	ASSERT_TRUE(encoder.IsOk());
	ASSERT_TRUE(encoder.AddFrame(frame0.data(), 42));
	ASSERT_TRUE(encoder.AddFrame(frame1.data(), 84));
	ASSERT_TRUE(encoder.Finish());

	const auto file = ReadFile(tmp.File(L"basic.apng"));
	const auto chunks = ParseChunks(file);
	ASSERT_GE(chunks.size(), 7u);
	EXPECT_EQ("IHDR", chunks[0].type);
	EXPECT_EQ("acTL", chunks[1].type);
	EXPECT_EQ("fcTL", chunks[2].type);
	EXPECT_EQ("IDAT", chunks[3].type);
	EXPECT_EQ("fcTL", chunks[4].type);
	EXPECT_EQ("fdAT", chunks[5].type);
	EXPECT_EQ("IEND", chunks[6].type);

	// IHDR 字段：宽、高、8 位深、RGBA、无交错
	ASSERT_EQ(13u, chunks[0].data.size());
	EXPECT_EQ(static_cast<uint32_t>(W), ReadBE32(chunks[0].data, 0));
	EXPECT_EQ(static_cast<uint32_t>(H), ReadBE32(chunks[0].data, 4));
	EXPECT_EQ(8, chunks[0].data[8]);
	EXPECT_EQ(6, chunks[0].data[9]);
	EXPECT_EQ(0, chunks[0].data[10]);
	EXPECT_EQ(0, chunks[0].data[11]);
	EXPECT_EQ(0, chunks[0].data[12]);

	// acTL 字段：总帧数与循环次数（0 表示无限循环）
	ASSERT_EQ(8u, chunks[1].data.size());
	EXPECT_EQ(2u, ReadBE32(chunks[1].data, 0));
	EXPECT_EQ(0u, ReadBE32(chunks[1].data, 4));

	// 首帧 fcTL：序列号 0，全帧区域，延迟 42 毫秒，dispose NONE，blend SOURCE
	ASSERT_EQ(26u, chunks[2].data.size());
	EXPECT_EQ(0u, ReadBE32(chunks[2].data, 0));
	EXPECT_EQ(static_cast<uint32_t>(W), ReadBE32(chunks[2].data, 4));
	EXPECT_EQ(static_cast<uint32_t>(H), ReadBE32(chunks[2].data, 8));
	EXPECT_EQ(0u, ReadBE32(chunks[2].data, 12));
	EXPECT_EQ(0u, ReadBE32(chunks[2].data, 16));
	EXPECT_EQ(42u, ReadBE16(chunks[2].data, 20));
	EXPECT_EQ(1000u, ReadBE16(chunks[2].data, 22));
	EXPECT_EQ(0, chunks[2].data[24]);
	EXPECT_EQ(0, chunks[2].data[25]);

	// 次帧 fcTL 序列号 1，延迟 84 毫秒，fdAT 序列号 2
	ASSERT_EQ(26u, chunks[4].data.size());
	EXPECT_EQ(1u, ReadBE32(chunks[4].data, 0));
	EXPECT_EQ(84u, ReadBE16(chunks[4].data, 20));
	EXPECT_GE(chunks[5].data.size(), 4u);
	EXPECT_EQ(2u, ReadBE32(chunks[5].data, 0));

	// 解压首帧 IDAT 还原像素
	const auto filtered = ZlibDecompress(chunks[3].data, static_cast<size_t>(H) * (1 + static_cast<size_t>(W) * 4));
	UnfilterAndCompare(filtered, W, H, frame0);

	// 解压次帧 fdAT（跳过 4 字节序列号）还原像素
	const std::vector<uint8_t> fdat_payload(chunks[5].data.begin() + 4, chunks[5].data.end());
	const auto filtered1 = ZlibDecompress(fdat_payload, static_cast<size_t>(H) * (1 + static_cast<size_t>(W) * 4));
	UnfilterAndCompare(filtered1, W, H, frame1);
}

TEST(lagi_apng_encoder, rewrites_frame_count_when_cancelled) {
	TempDir tmp;
	constexpr int W = 2, H = 2;
	const auto frame0 = MakeTestFrame(W, H, 10);

	// 声明 3 帧但仅写入 1 帧（模拟用户取消），Finish 应回写 acTL 帧数
	agi::ApngEncoder encoder(tmp.File(L"cancel.apng"), W, H, 3);
	ASSERT_TRUE(encoder.IsOk());
	ASSERT_TRUE(encoder.AddFrame(frame0.data(), 100));
	ASSERT_TRUE(encoder.Finish());

	const auto file = ReadFile(tmp.File(L"cancel.apng"));
	const auto chunks = ParseChunks(file);
	ASSERT_GE(chunks.size(), 4u);
	EXPECT_EQ("acTL", chunks[1].type);
	EXPECT_EQ(1u, ReadBE32(chunks[1].data, 0));
}

TEST(lagi_apng_encoder, fails_without_any_frame) {
	TempDir tmp;
	agi::ApngEncoder encoder(tmp.File(L"empty.apng"), 2, 2, 2);
	ASSERT_TRUE(encoder.IsOk());
	EXPECT_FALSE(encoder.Finish());
}

TEST(lagi_apng_encoder, rejects_extra_frames) {
	TempDir tmp;
	constexpr int W = 2, H = 2;
	const auto frame = MakeTestFrame(W, H, 0);

	agi::ApngEncoder encoder(tmp.File(L"extra.apng"), W, H, 1);
	ASSERT_TRUE(encoder.IsOk());
	ASSERT_TRUE(encoder.AddFrame(frame.data(), 16));
	EXPECT_FALSE(encoder.AddFrame(frame.data(), 16));
	ASSERT_TRUE(encoder.Finish());
}
