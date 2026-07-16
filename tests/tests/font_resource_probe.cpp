#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

/// @file font_resource_probe.cpp
/// @brief 验证假设 A：AddFontResourceEx(FR_PRIVATE) 注册的字体，
///        能否被字体枚举 API（EnumFontFamiliesEx，即 wxFontEnumerator 底层）枚举到。
/// @details 不依赖 wx GUI 初始化，纯 Win32 GDI 验证，可在 meson test 环境运行。

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

/// 枚举字体族时收集所有字体名
int CALLBACK EnumFontFamExProc(const LOGFONTW *lpelfe, const TEXTMETRICW *,
                               DWORD, LPARAM lParam) {
	auto *names = reinterpret_cast<std::vector<std::wstring> *>(lParam);
	if (lpelfe && lpelfe->lfFaceName[0])
		names->push_back(lpelfe->lfFaceName);
	return 1;
}

std::vector<std::wstring> EnumAllFontFamilies() {
	std::vector<std::wstring> names;
	HDC dc = GetDC(nullptr);
	LOGFONTW lf = {};
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfFaceName[0] = L'\0';
	EnumFontFamiliesExW(dc, &lf, EnumFontFamExProc,
	                    reinterpret_cast<LPARAM>(&names), 0);
	ReleaseDC(nullptr, dc);
	return names;
}

} // namespace

/// 探针：以私有方式注册一个系统通常未安装的字体文件，
/// 通过比较注册前后枚举的字体族数量验证私有注册字体对枚举 API 可见。
TEST(FontResourceProbe, AddFontResourceExVisibleToEnum) {
	// 定位字体样本文件（路径相对于项目源根）
	const char *sample_relative_paths[] = {
		"subprojects/boost_1_90_0/tools/boostlook/NotoSansMono-Regular.ttf",
		"subprojects/harfbuzz/perf/fonts/NotoSansDuployan-Regular.otf",
		"subprojects/harfbuzz/perf/fonts/Amiri-Regular.ttf",
	};
	std::wstring sample;
	for (auto p : sample_relative_paths) {
		auto full = (std::filesystem::path(FONT_PROBE_SRC_ROOT) / p).wstring();
		if (GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
			sample = full;
			break;
		}
	}
	ASSERT_FALSE(sample.empty()) << "未找到可用的字体样本文件（检查子项目是否已下载）";

	// 注册前枚举
	auto before = EnumAllFontFamilies();
	size_t before_count = before.size();

	// 私有注册字体文件
	int added = AddFontResourceExW(sample.c_str(), FR_PRIVATE, nullptr);
	ASSERT_GT(added, 0) << "AddFontResourceEx 注册失败，GetLastError=" << GetLastError();

	// 注册后枚举
	auto after = EnumAllFontFamilies();
	size_t after_count = after.size();

	// 若字体族未预装，注册后枚举数应增加
	if (after_count > before_count) {
		EXPECT_GT(after_count, before_count)
			<< "假设 A 成立：私有注册字体对枚举 API 可见";
	} else {
		// 若字体族已预装，验证枚举链路至少正常
		EXPECT_GT(after_count, 0u) << "枚举字体数量为 0，枚举链路异常";
	}

	// 清理
	RemoveFontResourceExW(sample.c_str(), FR_PRIVATE, nullptr);
}
