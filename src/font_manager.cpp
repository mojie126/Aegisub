// Copyright (c) 2026, Aegisub Project
// All rights reserved.

/// @file font_manager.cpp
/// @brief 用户字体管理实现：加载用户指定路径的字体并注册到进程字体表
/// @ingroup secondary_ui

#include "font_manager.h"

#include "dialog_font_chooser.h"
#include "libaegisub/path.h"
#include "options.h"

#include <algorithm>
#include <memory>
#include <fstream>
#include <mutex>
#include <set>
#include <vector>

#include <wx/arrstr.h>
#include <wx/fontenum.h>

#include <libaegisub/fs.h>

namespace font_manager {

namespace {
/// 验证探针日志开关
bool g_probe_enabled = false;

/// 已加载字体文件记录（用于反注册与探针）
struct LoadedFont {
	/// AddFontResourceEx 返回值（字体数量，仅作成功标记，非句柄）
	int count = 0;
	agi::fs::path file;
};
std::vector<LoadedFont> g_loaded_fonts;
std::mutex g_loaded_mutex;

/// 字体扩展名白名单（小写，含前导点）
const std::set<std::string> &FontExtWhitelist() {
	static const std::set<std::string> exts = {
		".ttf", ".otf", ".ttc", ".otc", ".woff", ".woff2",
		".fon", ".pfb", ".pfm"
	};
	return exts;
}

bool IsFontFile(const agi::fs::path &p) {
	std::string ext = p.extension().string();
	std::ranges::transform(ext, ext.begin(), ::tolower);
	return FontExtWhitelist().contains(ext);
}
void ProbeLog(const std::string &stage, const std::string &msg) {
	if (!g_probe_enabled)
		return;
	try {
		const agi::fs::path probe_path = config::path->Decode("?user/font_manager_probe.log");
		std::ofstream out(probe_path.string());
		if (out) {
			out << "[" << wxDateTime::Now().FormatISOTime() << "] "
				<< "[" << stage << "] " << msg << "\n";
		}
	}
	catch (...) {
		// 探针失败不应影响主流程
	}
}

/// 递归收集一个目录下的字体文件
void CollectFontFilesRecursive(const agi::fs::path &dir, std::vector<agi::fs::path> &out) {
	for (agi::fs::DirectoryIterator it(dir, ""); it != agi::fs::DirectoryIterator(); ++it) {
		agi::fs::path child = dir / *it;
		if (agi::fs::DirectoryExists(child))
			CollectFontFilesRecursive(child, out);
		else if (IsFontFile(child))
			out.push_back(child);
	}
}

/// 收集一个路径条目下所有字体文件（目录递归，文件直接加入）
void CollectFontFiles(const agi::fs::path &entry, std::vector<agi::fs::path> &out) {
	if (agi::fs::DirectoryExists(entry))
		CollectFontFilesRecursive(entry, out);
	else if (agi::fs::FileExists(entry)) {
		if (IsFontFile(entry))
			out.push_back(entry);
	}
}

bool RegisterFontFile(const agi::fs::path &file, int &out_count) {
	out_count = AddFontResourceEx(file.wstring().c_str(), FR_PRIVATE, nullptr);
	const DWORD last_err = out_count ? 0 : GetLastError();
	ProbeLog("Register",
		wxString::Format("path=%s count=%d success=%d errno=%u",
			file.string(), out_count, out_count != 0 ? 1 : 0, last_err).ToStdString());
	return out_count != 0;
}

void UnregisterFontFile(const agi::fs::path &file) {
	RemoveFontResourceExW(file.wstring().c_str(), FR_PRIVATE, nullptr);
}
} // namespace

void SetProbeEnabled(const bool enable) {
	g_probe_enabled = enable;
}

std::vector<agi::fs::path> GetLoadedFontFiles() {
	std::lock_guard lock(g_loaded_mutex);
	std::vector<agi::fs::path> files;
	files.reserve(g_loaded_fonts.size());
	for (auto const &f : g_loaded_fonts)
		files.push_back(f.file);
	return files;
}

void UnloadUserFonts() {
	std::lock_guard lock(g_loaded_mutex);
	for (auto &f : g_loaded_fonts)
		UnregisterFontFile(f.file);
	ProbeLog("Unload", wxString::Format("unloaded %u font(s)",
		static_cast<unsigned>(g_loaded_fonts.size())).ToStdString());
	g_loaded_fonts.clear();
	InvalidateSystemFontCache();
}

void LoadUserFonts() {
	// 订阅选项变更，运行时增删路径即时生效（仅订阅一次，连接需长期持有）
	static std::unique_ptr<agi::signal::Connection> user_font_paths_conn;
	if (!user_font_paths_conn) {
		user_font_paths_conn = std::make_unique<agi::signal::Connection>(
			OPT_SUB("App/User Font Paths", [] {
				LoadUserFonts();
			}));
	}

	if (!config::opt) {
		ProbeLog("Config", "config::opt not ready, skip");
		return;
	}

	// 先反注册旧字体，避免重复注册
	UnloadUserFonts();

	const std::vector<std::string> paths = OPT_GET("App/User Font Paths")->GetListString();
	ProbeLog("Config", wxString::Format("App/User Font Paths count = %u",
		static_cast<unsigned>(paths.size())).ToStdString());

	std::vector<agi::fs::path> font_files;
	for (auto const &p : paths) {
		if (p.empty())
			continue;
		agi::fs::path entry(p);
		const bool is_dir = agi::fs::DirectoryExists(entry);
		const bool is_file = agi::fs::FileExists(entry);
		ProbeLog("Discover", wxString::Format("entry=%s dir=%d file=%d",
			p, is_dir ? 1 : 0, is_file ? 1 : 0).ToStdString());
		CollectFontFiles(entry, font_files);
	}

	for (size_t i = 0; i < font_files.size(); ++i) {
		ProbeLog("Collect", wxString::Format("file[%u]=%s",
			static_cast<unsigned>(i), font_files[i].string()).ToStdString());
	}
	ProbeLog("Discover", wxString::Format("total font files found = %u",
		static_cast<unsigned>(font_files.size())).ToStdString());

	// 探针日志：注册前获取系统字体列表，用于对比 FR_PRIVATE 字体是否被 GetFacenames 枚举
	const wxArrayString before = g_probe_enabled ? wxFontEnumerator::GetFacenames() : wxArrayString();
	const auto before_count = before.GetCount();

	{
		std::lock_guard<std::mutex> lock(g_loaded_mutex);
		for (auto const &file : font_files) {
			LoadedFont lf;
			if (RegisterFontFile(file, lf.count)) {
				lf.file = file;
				g_loaded_fonts.push_back(lf);
			}
		}
	}

	// 探针日志：验证假设 A（FR_PRIVATE 字体是否被 GetFacenames 枚举）
	if (g_probe_enabled) {
		const wxArrayString after = wxFontEnumerator::GetFacenames();
		const auto after_count = after.GetCount();
		ProbeLog("AssumeA", wxString::Format("before_count=%u after_count=%u",
			static_cast<unsigned>(before_count), static_cast<unsigned>(after_count)).ToStdString());

		const size_t added = after_count > before_count ? after_count - before_count : 0;
		ProbeLog("AssumeA", wxString::Format("newly enumerated font faces = %u",
			static_cast<unsigned>(added)).ToStdString());

		const wxArrayString preferred = GetPreferredFontFaceList();
		ProbeLog("Preferred", wxString::Format("total preferred font count = %u",
			static_cast<unsigned>(preferred.GetCount())).ToStdString());
	}

	// 使系统字体静态缓存失效，强制后续取列表重新枚举（含新注册字体）
	InvalidateSystemFontCache();
	BumpFontListGeneration();
}

} // namespace font_manager
