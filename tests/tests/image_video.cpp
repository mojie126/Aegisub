/// @file image_video.cpp
/// @brief 图片视频提供者单元测试
/// @ingroup tests
///
/// 测试图片序列扫描逻辑、URI 生成和解析、导入图片序列格式

#include <main.h>

#include <libaegisub/format.h>
#include <libaegisub/fs.h>
#include <libaegisub/vfr.h>
#include <libaegisub/ass/time.h>

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
		result.push_back(entry.path());
	}

	std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
		std::string na = a.filename().string();
		std::string nb = b.filename().string();
		std::smatch ma, mb;
		if (std::regex_match(na, ma, digit_regex) && std::regex_match(nb, mb, digit_regex)) {
			auto da = ma[2].str();
			auto db = mb[2].str();
			auto va = std::stoull(da);
			auto vb = std::stoull(db);
			if (va != vb) return va < vb;
			if (da.size() != db.size()) return da.size() > db.size();
			return na < nb;
		}
		return na < nb;
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

TEST(lagi_image_video, scan_sequence_mixed_digit_width) {
	TempDir tmp("img_dw");
	tmp.touch("frame01.png");
	tmp.touch("frame02.png");
	tmp.touch("frame001.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "frame01.png");
	ASSERT_EQ(3u, result.size());
	// 按数值自然排序：数值相同则位数更长优先
	EXPECT_EQ("frame001.png", result[0].filename().string());
	EXPECT_EQ("frame01.png", result[1].filename().string());
	EXPECT_EQ("frame02.png", result[2].filename().string());
}

TEST(lagi_image_video, scan_sequence_equal_value_prefers_longer_digits) {
	TempDir tmp("img_eq");
	tmp.touch("f001.png");
	tmp.touch("f01.png");
	tmp.touch("f1.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "f1.png");
	ASSERT_EQ(3u, result.size());
	EXPECT_EQ("f001.png", result[0].filename().string());
	EXPECT_EQ("f01.png", result[1].filename().string());
	EXPECT_EQ("f1.png", result[2].filename().string());
}

TEST(lagi_image_video, scan_sequence_unpadded_numbers) {
	TempDir tmp("img_unpad");
	tmp.touch("frame1.png");
	tmp.touch("frame2.png");
	tmp.touch("frame9.png");
	tmp.touch("frame10.png");
	tmp.touch("frame99.png");
	tmp.touch("frame100.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "frame9.png");
	ASSERT_EQ(6u, result.size());
	EXPECT_EQ("frame1.png", result[0].filename().string());
	EXPECT_EQ("frame2.png", result[1].filename().string());
	EXPECT_EQ("frame9.png", result[2].filename().string());
	EXPECT_EQ("frame10.png", result[3].filename().string());
	EXPECT_EQ("frame99.png", result[4].filename().string());
	EXPECT_EQ("frame100.png", result[5].filename().string());
}

TEST(lagi_image_video, scan_sequence_large_numbers) {
	TempDir tmp("img_large");
	tmp.touch("img_999.png");
	tmp.touch("img_1000.png");
	tmp.touch("img_9999.png");
	tmp.touch("img_10000.png");
	tmp.touch("img_99999.png");

	auto result = ScanImageSequenceLogic(tmp.path() / "img_1000.png");
	ASSERT_EQ(5u, result.size());
	EXPECT_EQ("img_999.png", result[0].filename().string());
	EXPECT_EQ("img_1000.png", result[1].filename().string());
	EXPECT_EQ("img_9999.png", result[2].filename().string());
	EXPECT_EQ("img_10000.png", result[3].filename().string());
	EXPECT_EQ("img_99999.png", result[4].filename().string());
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

// ============================================================================
// 导入图片序列 —— 图片行 ASS 标签格式测试
// ============================================================================

TEST(lagi_image_video, import_tag_format_basic) {
	std::string path = "D:/images/frame001.png";
	int w = 1920, h = 1080;
	auto text = agi::format("{\\an7\\pos(0,0)\\bord0\\shad0\\fscx100\\fscy100\\1img(%s)\\p1}m 0 0 l %d 0 l %d %d l 0 %d",
		path, w, w, h, h);
	EXPECT_EQ("{\\an7\\pos(0,0)\\bord0\\shad0\\fscx100\\fscy100\\1img(D:/images/frame001.png)\\p1}m 0 0 l 1920 0 l 1920 1080 l 0 1080", text);
}

TEST(lagi_image_video, import_tag_format_small_image) {
	std::string path = "img.png";
	int w = 100, h = 50;
	auto text = agi::format("{\\an7\\pos(0,0)\\bord0\\shad0\\fscx100\\fscy100\\1img(%s)\\p1}m 0 0 l %d 0 l %d %d l 0 %d",
		path, w, w, h, h);
	EXPECT_EQ("{\\an7\\pos(0,0)\\bord0\\shad0\\fscx100\\fscy100\\1img(img.png)\\p1}m 0 0 l 100 0 l 100 50 l 0 50", text);
}

TEST(lagi_image_video, import_tag_path_with_backslash) {
	std::string path = "D:\\images\\frame001.png";
	int w = 640, h = 480;
	auto text = agi::format("{\\an7\\pos(0,0)\\bord0\\shad0\\fscx100\\fscy100\\1img(%s)\\p1}m 0 0 l %d 0 l %d %d l 0 %d",
		path, w, w, h, h);
	EXPECT_EQ("{\\an7\\pos(0,0)\\bord0\\shad0\\fscx100\\fscy100\\1img(D:\\images\\frame001.png)\\p1}m 0 0 l 640 0 l 640 480 l 0 480", text);
}

// ============================================================================
// 导入图片序列 —— 时间计算测试
// ============================================================================

TEST(lagi_image_video, import_time_cfr_30fps) {
	agi::vfr::Framerate fr(30.0);
	// 5 帧图片序列从第 0 帧开始
	int start_frame = 0;
	int count = 5;

	// 每帧应分配到正确的时间
	for (int i = 0; i < count; ++i) {
		int start_ms = fr.TimeAtFrame(start_frame + i, agi::vfr::START);
		int end_ms = fr.TimeAtFrame(start_frame + i + 1, agi::vfr::START);
		EXPECT_LT(start_ms, end_ms) << "frame " << i << " start must be before end";
	}

	// 首帧 START 时间在 0 附近（受 START 模式计算影响可能不精确为 0）
	EXPECT_LE(std::abs(fr.TimeAtFrame(0, agi::vfr::START)), 20);
}

TEST(lagi_image_video, import_time_cfr_24fps) {
	agi::vfr::Framerate fr(24.0);
	int start_frame = 100;
	int count = 3;

	int first_start = fr.TimeAtFrame(start_frame, agi::vfr::START);
	int last_end = fr.TimeAtFrame(start_frame + count, agi::vfr::START);

	// 序列总时长应约等于 3/24 秒 = 125ms
	int duration = last_end - first_start;
	EXPECT_GT(duration, 100);
	EXPECT_LT(duration, 150);
}

TEST(lagi_image_video, import_time_cfr_nonzero_start) {
	agi::vfr::Framerate fr(30.0);
	int start_frame = 500;
	int count = 10;

	// 每帧时间递增
	int prev_ms = fr.TimeAtFrame(start_frame, agi::vfr::START);
	for (int i = 1; i <= count; ++i) {
		int cur_ms = fr.TimeAtFrame(start_frame + i, agi::vfr::START);
		EXPECT_GT(cur_ms, prev_ms) << "frame " << i;
		prev_ms = cur_ms;
	}
}

TEST(lagi_image_video, import_time_vfr) {
	// VFR 时间码：帧 0=0ms, 帧 1=50ms, 帧 2=80ms, 帧 3=130ms, 帧 4=160ms
	agi::vfr::Framerate fr({0, 50, 80, 130, 160});
	int start_frame = 1;
	int count = 3;

	// 帧 1 起始时间
	int t1 = fr.TimeAtFrame(1, agi::vfr::START);
	int t2 = fr.TimeAtFrame(2, agi::vfr::START);
	int t3 = fr.TimeAtFrame(3, agi::vfr::START);
	int t4 = fr.TimeAtFrame(4, agi::vfr::START);

	// 时间递增
	EXPECT_LT(t1, t2);
	EXPECT_LT(t2, t3);
	EXPECT_LT(t3, t4);
}

TEST(lagi_image_video, import_comment_end_time) {
	agi::vfr::Framerate fr(30.0);
	int start_frame = 100;
	int count = 5;

	// 注释行结束时间 = 序列最后一帧之后
	int comment_end = fr.TimeAtFrame(start_frame + count, agi::vfr::START);
	// 最后一帧图片行结束时间也应等于此
	int last_img_end = fr.TimeAtFrame(start_frame + count, agi::vfr::START);

	EXPECT_EQ(comment_end, last_img_end);
	// 注释行应包含整个序列
	int comment_start = fr.TimeAtFrame(start_frame, agi::vfr::START);
	EXPECT_LT(comment_start, comment_end);
}

TEST(lagi_image_video, import_duration_display) {
	agi::vfr::Framerate fr(24.0);
	int count = 24;

	// 24 帧 @24fps ≈ 1 秒
	int duration_ms = fr.TimeAtFrame(count) - fr.TimeAtFrame(0);
	EXPECT_GT(duration_ms, 900);
	EXPECT_LT(duration_ms, 1100);

	// agi::Time 格式化
	auto dur = agi::Time(duration_ms);
	auto formatted = dur.GetAssFormatted(true);
	EXPECT_FALSE(formatted.empty());
}

// ============================================================================
// 导出图片序列 —— 文件名生成格式测试
// ============================================================================

/// @brief 模拟导出图片序列的文件名生成逻辑
static std::string MakeExportFilename(const std::string& img_path, long start_frame, long end_frame, int current_frame, bool subtitle_only) {
	const char *ext = subtitle_only ? ".png" : ".jpg";
	return agi::format("%s_[%ld-%ld]_%05d%s", img_path, start_frame, end_frame, current_frame, ext);
}

TEST(lagi_image_video, export_filename_jpeg_mode) {
	auto name = MakeExportFilename("video_clip", 100, 200, 1, false);
	EXPECT_EQ("video_clip_[100-200]_00001.jpg", name);
}

TEST(lagi_image_video, export_filename_png_subtitle_mode) {
	auto name = MakeExportFilename("video_clip", 100, 200, 1, true);
	EXPECT_EQ("video_clip_[100-200]_00001.png", name);
}

TEST(lagi_image_video, export_filename_frame_counter) {
	auto name1 = MakeExportFilename("clip", 0, 999, 1, false);
	EXPECT_EQ("clip_[0-999]_00001.jpg", name1);

	auto name2 = MakeExportFilename("clip", 0, 999, 500, true);
	EXPECT_EQ("clip_[0-999]_00500.png", name2);

	auto name3 = MakeExportFilename("clip", 0, 999, 99999, false);
	EXPECT_EQ("clip_[0-999]_99999.jpg", name3);
}

TEST(lagi_image_video, export_filename_large_frame_range) {
	auto name = MakeExportFilename("test", 10000, 20000, 42, true);
	EXPECT_EQ("test_[10000-20000]_00042.png", name);
}

TEST(lagi_image_video, export_duration_frame_count) {
	// 导出帧范围的帧数计算：end - start + 1
	long start = 100, end_frame = 200;
	int duration = end_frame - start + 1;
	EXPECT_EQ(101, duration);

	start = 0; end_frame = 0;
	duration = end_frame - start + 1;
	EXPECT_EQ(1, duration);
}
