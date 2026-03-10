/// @file image_video.cpp
/// @brief 图片视频提供者单元测试
/// @ingroup tests
///
/// 测试图片序列扫描逻辑、URI 生成和解析

#include <main.h>

#include <libaegisub/fs.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <algorithm>

namespace {

// 与 video_provider_image.cpp 中的 ScanImageSequence 使用相同的正则表达式
const std::regex digit_regex("^(.*\\D)?(\\d+)(\\.[^.]+)$");

/// @brief 序列扫描测试用的纯逻辑函数（不依赖 FFmpeg）
/// 复现 ImageVideoProvider::ScanImageSequence 的核心逻辑
std::vector<std::filesystem::path> ScanImageSequenceLogic(std::filesystem::path const& selected_file)
{
	std::vector<std::filesystem::path> result;

	if (!std::filesystem::exists(selected_file)) {
		result.push_back(selected_file);
		return result;
	}

	auto dir = selected_file.parent_path();
	auto filename = selected_file.filename().string();

	std::smatch match;
	if (!std::regex_match(filename, match, digit_regex)) {
		result.push_back(selected_file);
		return result;
	}

	std::string prefix = match[1].str();
	std::string digits = match[2].str();
	std::string suffix = match[3].str();
	size_t digit_width = digits.size();

	for (auto const& entry : std::filesystem::directory_iterator(dir)) {
		if (!entry.is_regular_file()) continue;
		std::string name = entry.path().filename().string();
		std::smatch file_match;
		if (!std::regex_match(name, file_match, digit_regex))
			continue;
		if (file_match[1].str() != prefix || file_match[3].str() != suffix)
			continue;
		if (file_match[2].str().size() != digit_width)
			continue;
		result.push_back(entry.path());
	}

	std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
		return a.filename().string() < b.filename().string();
	});

	if (result.empty())
		result.push_back(selected_file);

	return result;
}

/// @brief 复现 ImageVideoProvider::MakeFilename 的逻辑
std::string MakeFilenameLogic(std::string const& fps, std::string const& filepath) {
	return "?image:" + fps + ":" + filepath;
}

/// @brief 帮助函数：匹配正则并提取结果
struct RegexResult {
	bool matched;
	std::string prefix;
	std::string digits;
	std::string suffix;
};

RegexResult MatchFilename(const std::string& filename) {
	std::smatch match;
	if (!std::regex_match(filename, match, digit_regex))
		return {false, "", "", ""};
	return {true, match[1].str(), match[2].str(), match[3].str()};
}

// 创建临时目录及文件的辅助
class TempDir {
	std::filesystem::path dir;
public:
	TempDir(const std::string& name) {
		dir = std::filesystem::temp_directory_path() / ("aegisub_test_" + name);
		std::filesystem::create_directories(dir);
	}
	~TempDir() {
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
	std::filesystem::path path() const { return dir; }
	void touch(const std::string& filename) {
		std::ofstream(dir / filename).close();
	}
};

} // namespace

// ============================================================================
// 正则匹配测试
// ============================================================================

TEST(lagi_image_video, regex_simple_sequence) {
	auto r = MatchFilename("img_0042.png");
	EXPECT_TRUE(r.matched);
	EXPECT_EQ("img_", r.prefix);
	EXPECT_EQ("0042", r.digits);
	EXPECT_EQ(".png", r.suffix);
}

TEST(lagi_image_video, regex_pure_number) {
	auto r = MatchFilename("0001.jpg");
	EXPECT_TRUE(r.matched);
	EXPECT_EQ("", r.prefix);
	EXPECT_EQ("0001", r.digits);
	EXPECT_EQ(".jpg", r.suffix);
}

TEST(lagi_image_video, regex_complex_prefix) {
	auto r = MatchFilename("scene3_take2_frame0099.tiff");
	EXPECT_TRUE(r.matched);
	EXPECT_EQ("scene3_take2_frame", r.prefix);
	EXPECT_EQ("0099", r.digits);
	EXPECT_EQ(".tiff", r.suffix);
}

TEST(lagi_image_video, regex_no_digits) {
	auto r = MatchFilename("photo.png");
	EXPECT_FALSE(r.matched);
}

TEST(lagi_image_video, regex_single_digit) {
	auto r = MatchFilename("frame1.bmp");
	EXPECT_TRUE(r.matched);
	EXPECT_EQ("frame", r.prefix);
	EXPECT_EQ("1", r.digits);
	EXPECT_EQ(".bmp", r.suffix);
}

TEST(lagi_image_video, regex_multiple_dots) {
	auto r = MatchFilename("my.file.001.png");
	EXPECT_TRUE(r.matched);
	EXPECT_EQ("my.file.", r.prefix);
	EXPECT_EQ("001", r.digits);
	EXPECT_EQ(".png", r.suffix);
}

TEST(lagi_image_video, regex_long_number) {
	auto r = MatchFilename("render_00000001.exr");
	EXPECT_TRUE(r.matched);
	EXPECT_EQ("render_", r.prefix);
	EXPECT_EQ("00000001", r.digits);
	EXPECT_EQ(".exr", r.suffix);
}

// ============================================================================
// MakeFilename 测试
// ============================================================================

TEST(lagi_image_video, make_filename_basic) {
	EXPECT_EQ("?image:24000/1001:C:/images/img_0001.png",
	          MakeFilenameLogic("24000/1001", "C:/images/img_0001.png"));
}

TEST(lagi_image_video, make_filename_integer_fps) {
	EXPECT_EQ("?image:30:D:/test/frame001.jpg",
	          MakeFilenameLogic("30", "D:/test/frame001.jpg"));
}

// ============================================================================
// URI 解析测试
// ============================================================================

TEST(lagi_image_video, uri_parse_valid) {
	std::string uri = "?image:24000/1001:C:/images/img_0001.png";
	ASSERT_TRUE(uri.starts_with("?image:"));

	auto fields = uri.substr(7);
	auto first_colon = fields.find(':');
	ASSERT_NE(first_colon, std::string::npos);

	std::string fps_str = fields.substr(0, first_colon);
	std::string filepath = fields.substr(first_colon + 1);

	EXPECT_EQ("24000/1001", fps_str);
	EXPECT_EQ("C:/images/img_0001.png", filepath);
}

TEST(lagi_image_video, uri_parse_with_drive_letter) {
	std::string uri = "?image:30:D:/path/to/file.png";
	auto fields = uri.substr(7);
	auto first_colon = fields.find(':');

	std::string fps_str = fields.substr(0, first_colon);
	std::string filepath = fields.substr(first_colon + 1);

	EXPECT_EQ("30", fps_str);
	EXPECT_EQ("D:/path/to/file.png", filepath);
}

// ============================================================================
// 文件系统序列扫描测试
// ============================================================================

TEST(lagi_image_video, scan_single_no_digits) {
	TempDir tmp("img_single");
	tmp.touch("photo.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "photo.png");
	ASSERT_EQ(1u, result.size());
	EXPECT_EQ("photo.png", result[0].filename().string());
}

TEST(lagi_image_video, scan_sequence_basic) {
	TempDir tmp("img_seq");
	tmp.touch("frame001.png");
	tmp.touch("frame002.png");
	tmp.touch("frame003.png");
	tmp.touch("frame004.png");
	tmp.touch("frame005.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "frame003.png");
	ASSERT_EQ(5u, result.size());
	EXPECT_EQ("frame001.png", result[0].filename().string());
	EXPECT_EQ("frame005.png", result[4].filename().string());
}

TEST(lagi_image_video, scan_sequence_ignores_different_prefix) {
	TempDir tmp("img_mix");
	tmp.touch("bg_001.png");
	tmp.touch("bg_002.png");
	tmp.touch("fg_001.png");
	tmp.touch("fg_002.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "bg_001.png");
	ASSERT_EQ(2u, result.size());
	EXPECT_EQ("bg_001.png", result[0].filename().string());
	EXPECT_EQ("bg_002.png", result[1].filename().string());
}

TEST(lagi_image_video, scan_sequence_ignores_different_extension) {
	TempDir tmp("img_ext");
	tmp.touch("img_001.png");
	tmp.touch("img_002.png");
	tmp.touch("img_001.jpg");

	auto result = ScanImageSequenceLogic(tmp.path() / "img_001.png");
	ASSERT_EQ(2u, result.size());
}

TEST(lagi_image_video, scan_sequence_ignores_different_digit_width) {
	TempDir tmp("img_dw");
	tmp.touch("frame01.png");
	tmp.touch("frame02.png");
	tmp.touch("frame001.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "frame01.png");
	ASSERT_EQ(2u, result.size());
	EXPECT_EQ("frame01.png", result[0].filename().string());
	EXPECT_EQ("frame02.png", result[1].filename().string());
}

TEST(lagi_image_video, scan_sorted_order) {
	TempDir tmp("img_sort");
	tmp.touch("img_003.png");
	tmp.touch("img_001.png");
	tmp.touch("img_002.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "img_002.png");
	ASSERT_EQ(3u, result.size());
	EXPECT_EQ("img_001.png", result[0].filename().string());
	EXPECT_EQ("img_002.png", result[1].filename().string());
	EXPECT_EQ("img_003.png", result[2].filename().string());
}

TEST(lagi_image_video, scan_nonexistent_file) {
	auto result = ScanImageSequenceLogic("Z:/nonexistent/frame001.png");
	ASSERT_EQ(1u, result.size());
	EXPECT_EQ("Z:/nonexistent/frame001.png", result[0].string());
}
