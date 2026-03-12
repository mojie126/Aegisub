/// @file audit_fixes.cpp
/// @brief 审计修复的单元测试：ExtractImgPaths、L2缓存容量、EndOfStreamDist、Extradata过期计数器、combine_karaoke
///
/// @warning 本文件中的辅助函数是对实际实现逻辑的**镜像副本**。
///          修改实际代码后，必须同步更新此处对应的副本函数。
///          各副本函数上方标注了精确的源文件路径和函数名，
///          请在修改实际实现后搜索 "MIRROR-OF:" 标记来定位需同步的副本。

#include <main.h>

#include <algorithm>
#include <string>
#include <vector>

// ============================================================
// #4: ExtractImgPaths 反斜杠+数字前缀验证
// 对照: src/async_video_provider.cpp :: ExtractImgPaths()
// ============================================================

/// @brief 从原始ASS事件文本中提取\\Nimg(path)标签的路径（仅解析逻辑，不依赖AssFile）
/// MIRROR-OF: src/async_video_provider.cpp :: ExtractImgPaths()
/// @param text 单行ASS事件文本
/// @return 匹配到的图片路径列表
static std::vector<std::string> ParseImgPaths(const std::string& text) {
	std::vector<std::string> paths;
	size_t pos = 0;
	while ((pos = text.find("img(", pos)) != std::string::npos) {
		// 必须满足: pos >= 2, 且 text[pos-2] == '\\', text[pos-1] 为 '1'..'7'
		if (pos >= 2 && text[pos - 2] == '\\' && text[pos - 1] >= '1' && text[pos - 1] <= '7') {
			size_t start = pos + 4;
			size_t end = text.find(')', start);
			if (end != std::string::npos) {
				std::string param = text.substr(start, end - start);
				size_t comma = param.find(',');
				std::string path = (comma != std::string::npos) ? param.substr(0, comma) : param;
				auto lt = path.find_first_not_of(" \t");
				if (lt != std::string::npos) {
					auto rt = path.find_last_not_of(" \t");
					path = path.substr(lt, rt - lt + 1);
				}
				if (!path.empty())
					paths.push_back(std::move(path));
			}
		}
		pos += 4;
	}
	return paths;
}

/// 正常\\1img标签应正确提取路径
TEST(AuditFixesTest, ImgPaths_ValidTag) {
	auto paths = ParseImgPaths("some text\\1img(test.png)end");
	ASSERT_EQ(1u, paths.size());
	EXPECT_EQ("test.png", paths[0]);
}

/// \\7img标签（最大数字）应正确提取
TEST(AuditFixesTest, ImgPaths_MaxDigit) {
	auto paths = ParseImgPaths("\\7img(pic.bmp)");
	ASSERT_EQ(1u, paths.size());
	EXPECT_EQ("pic.bmp", paths[0]);
}

/// 缺少反斜杠前缀时不应匹配（#4修复的核心case）
TEST(AuditFixesTest, ImgPaths_NoBackslash) {
	auto paths = ParseImgPaths("x1img(fake.png)");
	EXPECT_TRUE(paths.empty()) << "Without backslash prefix, should not match";
}

/// 数字超出范围（0, 8, 9）时不应匹配
TEST(AuditFixesTest, ImgPaths_InvalidDigit) {
	EXPECT_TRUE(ParseImgPaths("\\0img(a.png)").empty());
	EXPECT_TRUE(ParseImgPaths("\\8img(a.png)").empty());
	EXPECT_TRUE(ParseImgPaths("\\9img(a.png)").empty());
}

/// 多个标签应全部提取
TEST(AuditFixesTest, ImgPaths_Multiple) {
	auto paths = ParseImgPaths("\\1img(a.png) text \\2img(b.jpg)");
	ASSERT_EQ(2u, paths.size());
	EXPECT_EQ("a.png", paths[0]);
	EXPECT_EQ("b.jpg", paths[1]);
}

/// 带参数（逗号分隔）应仅取第一个参数作为路径
TEST(AuditFixesTest, ImgPaths_WithParams) {
	auto paths = ParseImgPaths("\\1img(pic.png, 100, 200)");
	ASSERT_EQ(1u, paths.size());
	EXPECT_EQ("pic.png", paths[0]);
}

/// 路径中前后空白应被裁剪
TEST(AuditFixesTest, ImgPaths_TrimWhitespace) {
	auto paths = ParseImgPaths("\\1img(  pic.png  )");
	ASSERT_EQ(1u, paths.size());
	EXPECT_EQ("pic.png", paths[0]);
}

/// 空路径不应被提取
TEST(AuditFixesTest, ImgPaths_EmptyPath) {
	auto paths = ParseImgPaths("\\1img()");
	EXPECT_TRUE(paths.empty());
}

/// 文本开头的img(不可能有\\N前缀（pos < 2）
TEST(AuditFixesTest, ImgPaths_AtStart) {
	EXPECT_TRUE(ParseImgPaths("1img(a.png)").empty());
	EXPECT_TRUE(ParseImgPaths("img(a.png)").empty());
}

// ============================================================
// #1: L2缓存动态容量计算
// 对照: src/async_video_provider.cpp :: 构造函数
// ============================================================

/// @brief 计算L2缓存容量（对照实际实现逻辑）
/// MIRROR-OF: src/async_video_provider.cpp :: AsyncVideoProvider 构造函数中 l2_cache_capacity 计算
/// @param width 视频宽度
/// @param height 视频高度
/// @return 缓存容量
static size_t ComputeL2Capacity(int width, int height) {
	size_t frame_bytes = static_cast<size_t>(width) * height * 4;
	return frame_bytes > 0
		? std::clamp<size_t>(256ULL * 1024 * 1024 / frame_bytes, 2, 16)
		: 16;
}

/// 1080p分辨率下容量应为16（上限）
TEST(AuditFixesTest, L2Capacity_1080p) {
	size_t cap = ComputeL2Capacity(1920, 1080);
	EXPECT_EQ(16u, cap) << "1080p frame ~8MB, 256MB/8MB=32, clamped to 16";
}

/// 4K分辨率下容量应显著降低
TEST(AuditFixesTest, L2Capacity_4K) {
	size_t cap = ComputeL2Capacity(3840, 2160);
	// 3840*2160*4 = 33,177,600 -> 256MB/33MB ≈ 8
	EXPECT_GE(cap, 2u);
	EXPECT_LE(cap, 8u);
	EXPECT_EQ(8u, cap) << "4K frame ~31.6MB, 256MB/31.6MB=8";
}

/// 8K分辨率下容量应为最小值2
TEST(AuditFixesTest, L2Capacity_8K) {
	size_t cap = ComputeL2Capacity(7680, 4320);
	// 7680*4320*4 = 132,710,400 -> 256MB/132MB ≈ 2
	EXPECT_EQ(2u, cap) << "8K frame ~126MB, 256MB/126MB=2";
}

/// 480p分辨率下容量应为16（上限）
TEST(AuditFixesTest, L2Capacity_480p) {
	size_t cap = ComputeL2Capacity(854, 480);
	EXPECT_EQ(16u, cap);
}

/// 零尺寸时应回退到默认值16
TEST(AuditFixesTest, L2Capacity_Zero) {
	EXPECT_EQ(16u, ComputeL2Capacity(0, 0));
	EXPECT_EQ(16u, ComputeL2Capacity(0, 1080));
	EXPECT_EQ(16u, ComputeL2Capacity(1920, 0));
}

/// 总缓存内存不超过约256MB
TEST(AuditFixesTest, L2Capacity_MemoryBound) {
	// 对各分辨率验证: 容量 * 帧大小 <= 256MB (允许一帧余量)
	auto check = [](int w, int h) {
		size_t frame_bytes = static_cast<size_t>(w) * h * 4;
		size_t cap = ComputeL2Capacity(w, h);
		size_t total = cap * frame_bytes;
		EXPECT_LE(total, 256ULL * 1024 * 1024)
			<< "Resolution " << w << "x" << h
			<< " with capacity " << cap
			<< " uses " << (total / (1024 * 1024)) << "MB";
	};
	check(1920, 1080);
	check(2560, 1440);
	check(3840, 2160);
	check(7680, 4320);
}

// ============================================================
// #F5: EndOfStreamDist H264安全裕量
// 对照: subprojects/ffms2/src/core/videosource.cpp :: SeekTo()
// ============================================================

/// @brief 计算EndOfStreamDist（对照实际实现逻辑）
/// MIRROR-OF: subprojects/ffms2/src/core/videosource.cpp :: FFMS_VideoSource::SeekTo()
/// @param reorder_delay 重排序延迟
/// @param thread_delay 线程延迟
/// @param codec_id 编解码器ID（0 = 非H264, 1 = H264）
/// @param has_b_frames 编解码器B帧数
/// @return EndOfStreamDist值
static int ComputeEndOfStreamDist(int reorder_delay, int thread_delay, bool is_h264, int has_b_frames) {
	int dist = reorder_delay + thread_delay + 1;
	if (is_h264)
		dist = std::max(dist, (has_b_frames + 1) * 2);
	return dist;
}

/// 非H264编解码器使用基本公式
TEST(AuditFixesTest, EndOfStreamDist_NonH264) {
	EXPECT_EQ(5, ComputeEndOfStreamDist(3, 1, false, 2));
	EXPECT_EQ(4, ComputeEndOfStreamDist(2, 1, false, 4));
}

/// H264应使用两种公式的较大值
TEST(AuditFixesTest, EndOfStreamDist_H264_SafetyMargin) {
	// has_b_frames=2, reorder=3, thread=0 -> base=4, h264=(2+1)*2=6 -> max=6
	EXPECT_EQ(6, ComputeEndOfStreamDist(3, 0, true, 2));
}

/// H264高B帧数时安全裕量应更大
TEST(AuditFixesTest, EndOfStreamDist_H264_HighBFrames) {
	// has_b_frames=15 (H264 max), reorder=17, thread=0 -> base=18, h264=(15+1)*2=32 -> max=32
	EXPECT_EQ(32, ComputeEndOfStreamDist(17, 0, true, 15));
}

/// H264多线程基础公式已足够大时不额外增加
TEST(AuditFixesTest, EndOfStreamDist_H264_ThreadDominant) {
	// has_b_frames=2, reorder=4, thread=30 -> base=35, h264=(2+1)*2=6 -> max=35
	EXPECT_EQ(35, ComputeEndOfStreamDist(4, 30, true, 2));
}

/// H264 HW解码(+2)场景: reorder=has_b_frames+2, thread=0
TEST(AuditFixesTest, EndOfStreamDist_H264_HWDecode) {
	// has_b_frames=4, reorder=6(4+2), thread=0 -> base=7, h264=(4+1)*2=10 -> max=10
	EXPECT_EQ(10, ComputeEndOfStreamDist(6, 0, true, 4));
}

// ============================================================
// #7: Extradata 过期计数器
// 对照: src/ass_file.h :: EXTRADATA_EXPIRATION_LIMIT
//       src/ass_parser.cpp :: ParseExtradataLine()
//       src/ass_file.cpp :: CleanExtradata()
// ============================================================

#include "ass_file.h"
#include <unordered_set>

/// @brief 模拟 CleanExtradata 的过期清除逻辑
/// MIRROR-OF: src/ass_file.cpp :: CleanExtradata()
/// @param entries extradata 条目（id, counter）
/// @param ids_used 被引用的 id 集合
/// @param limit 过期阈值
static void SimulateCleanExtradata(
	std::vector<std::pair<uint32_t, int>>& entries,
	const std::unordered_set<uint32_t>& ids_used,
	int limit)
{
	for (auto& e : entries) {
		if (ids_used.count(e.first))
			e.second = 0;
		else
			e.second++;
	}
	entries.erase(std::remove_if(entries.begin(), entries.end(),
		[&](const std::pair<uint32_t, int>& e) {
			return e.second >= limit;
		}), entries.end());
}

/// 加载时计数器为 LIMIT+1，未引用条目首次清理即被删除
TEST(AuditFixesTest, Extradata_LoadedUnusedDeletedOnFirstClean) {
	const int LIMIT = EXTRADATA_EXPIRATION_LIMIT;
	// 模拟加载：counter = LIMIT + 1
	std::vector<std::pair<uint32_t, int>> entries = {
		{1, LIMIT + 1}, {2, LIMIT + 1}, {3, LIMIT + 1}
	};
	// id=1 被引用，id=2,3 未被引用
	std::unordered_set<uint32_t> used = {1};
	SimulateCleanExtradata(entries, used, LIMIT);
	// id=1 保留(counter=0)，id=2,3 的 counter 从11变12 >= 10，被清除
	ASSERT_EQ(1u, entries.size());
	EXPECT_EQ(1u, entries[0].first);
	EXPECT_EQ(0, entries[0].second);
}

/// 被引用的条目计数器始终为0，永不过期
TEST(AuditFixesTest, Extradata_ReferencedNeverExpires) {
	const int LIMIT = EXTRADATA_EXPIRATION_LIMIT;
	std::vector<std::pair<uint32_t, int>> entries = {{1, LIMIT + 1}};
	std::unordered_set<uint32_t> used = {1};
	for (int i = 0; i < 20; i++)
		SimulateCleanExtradata(entries, used, LIMIT);
	ASSERT_EQ(1u, entries.size());
	EXPECT_EQ(0, entries[0].second);
}

/// 从 counter=0 开始的条目需要恰好 LIMIT 次未引用才被清除
TEST(AuditFixesTest, Extradata_GradualExpiration) {
	const int LIMIT = EXTRADATA_EXPIRATION_LIMIT;
	std::vector<std::pair<uint32_t, int>> entries = {{1, 0}};
	std::unordered_set<uint32_t> empty;
	// 前 LIMIT-1 次不应被清除
	for (int i = 0; i < LIMIT - 1; i++) {
		SimulateCleanExtradata(entries, empty, LIMIT);
		ASSERT_EQ(1u, entries.size()) << "round " << i;
	}
	// 第 LIMIT 次应被清除
	SimulateCleanExtradata(entries, empty, LIMIT);
	EXPECT_TRUE(entries.empty());
}

// ============================================================
// #8: combine_karaoke 卡拉OK合并逻辑
// 对照: src/command/edit.cpp :: combine_karaoke()
// ============================================================

#include <libaegisub/string.h>

/// @brief 模拟 combine_karaoke 的合并逻辑
/// MIRROR-OF: src/command/edit.cpp :: combine_karaoke()
/// @param first_text 首行文本（会被就地修改）
/// @param first_start 首行起始时间（毫秒）
/// @param first_end 首行结束时间（毫秒）
/// @param second_text 次行文本，nullptr 表示首次调用
/// @param second_start 次行起始时间（毫秒）
static void SimulateCombineKaraoke(
	std::string& first_text,
	int first_start, int first_end,
	const char* second_text, int second_start)
{
	if (second_text)
		first_text = agi::Str(first_text, " {\\k", std::to_string(std::max(0, (second_start - first_end) / 10)), "}", second_text);
	else
		first_text = agi::Str("{\\k", std::to_string((first_end - first_start) / 10), "}", first_text);
}

/// 首行包裹：用 duration 作为 k 值
TEST(AuditFixesTest, CombineKaraoke_FirstLineWrapped) {
	std::string text = "Hello";
	SimulateCombineKaraoke(text, 0, 1000, nullptr, 0);
	EXPECT_EQ("{\\k100}Hello", text);
}

/// 合并次行：用两行间的时间间隔作为 k 值
TEST(AuditFixesTest, CombineKaraoke_GapBetweenLines) {
	std::string text = "{\\k100}Hello";
	// first_end=1000, second_start=1500, gap=500ms=50cs
	SimulateCombineKaraoke(text, 0, 1000, "World", 1500);
	EXPECT_EQ("{\\k100}Hello {\\k50}World", text);
}

/// 三行合并完整流程（模拟 combine_lines 调用顺序）
TEST(AuditFixesTest, CombineKaraoke_ThreeLines) {
	// Line 1: 0ms-1000ms "A", Line 2: 1500ms-2500ms "B", Line 3: 3000ms-4000ms "C"
	std::string text = "A";
	int first_end = 1000;

	// 第一步：combiner(first, nullptr)
	SimulateCombineKaraoke(text, 0, first_end, nullptr, 0);
	EXPECT_EQ("{\\k100}A", text);

	// 第二步：combiner(first, sel[1])，之后 first_end 更新为 max(1000,2500)=2500
	SimulateCombineKaraoke(text, 0, first_end, "B", 1500);
	first_end = std::max(first_end, 2500);
	EXPECT_EQ("{\\k100}A {\\k50}B", text);

	// 第三步：combiner(first, sel[2])，first_end 已更新为 2500
	SimulateCombineKaraoke(text, 0, first_end, "C", 3000);
	EXPECT_EQ("{\\k100}A {\\k50}B {\\k50}C", text);
}

/// 相邻行（无间隔）产生 k0
TEST(AuditFixesTest, CombineKaraoke_ZeroGap) {
	std::string text = "{\\k50}X";
	// first_end=500, second_start=500, gap=0
	SimulateCombineKaraoke(text, 0, 500, "Y", 500);
	EXPECT_EQ("{\\k50}X {\\k0}Y", text);
}

/// 重叠行（负间隔）clamp 到 k0
TEST(AuditFixesTest, CombineKaraoke_OverlappingLines) {
	std::string text = "{\\k100}X";
	// first_end=1000, second_start=800, gap=-200ms → clamp 到 0
	SimulateCombineKaraoke(text, 0, 1000, "Y", 800);
	EXPECT_EQ("{\\k100}X {\\k0}Y", text);
}
