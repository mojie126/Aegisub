// Copyright (c) 2007, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#ifdef WITH_UPDATE_CHECKER

#include "compat.h"
#include "format.h"
#include "options.h"
#include "string_codec.h"
#include "version.h"

#include <libaegisub/cajun/reader.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/line_iterator.h>
#include <libaegisub/scoped_ptr.h>
#include <libaegisub/split.h>

#include <ctime>
#include <curl/curl.h>
#include <functional>
#include <mutex>
#include <sstream>
#include <vector>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/hyperlink.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/platinfo.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/html/htmlwin.h>

#include <regex>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {
std::mutex VersionCheckLock;

/// @brief 将文本中的 HTML 特殊字符转义
/// @param text 原始文本
/// @return 转义后的文本
std::string HtmlEscape(const std::string& text) {
	std::string result;
	result.reserve(text.size());
	for (char c : text) {
		switch (c) {
			case '&': result += "&amp;"; break;
			case '<': result += "&lt;"; break;
			case '>': result += "&gt;"; break;
			case '"': result += "&quot;"; break;
			default: result += c; break;
		}
	}
	return result;
}

/// @brief 处理 Markdown 行内格式（加粗、斜体、行内代码、图片、链接）
/// @param line 一行文本（已 HTML 转义）
/// @return 替换行内格式标记后的 HTML 文本
std::string ProcessInlineMarkdown(const std::string& line) {
	std::string result = line;

	// 行内代码: `code`
	result = std::regex_replace(result, std::regex("`([^`]+)`"), "<code>$1</code>");

	// 加粗: **text**
	result = std::regex_replace(result, std::regex("\\*\\*([^*]+)\\*\\*"), "<b>$1</b>");

	// 斜体: *text*
	result = std::regex_replace(result, std::regex("\\*([^*]+)\\*"), "<i>$1</i>");

	// 图片: ![alt](url) — wxHtmlWindow 不支持加载外部图片，直接移除
	result = std::regex_replace(result, std::regex("!\\[([^\\]]*)\\]\\(([^)]+)\\)"), "");

	// 链接: [text](url)
	result = std::regex_replace(result, std::regex("\\[([^\\]]+)\\]\\(([^)]+)\\)"), "<a href=\"$2\">$1</a>");

	return result;
}

/// @brief 将 GitHub Release 的 Markdown 正文转换为 HTML
/// @details 支持标题、加粗、斜体、行内代码、链接、无序列表、代码块
/// @param markdown Markdown 格式文本
/// @return HTML 格式文本
std::string MarkdownToHtml(const std::string& markdown) {
	// 预处理: 移除原始 HTML <img> 标签（wxHtmlWindow 不支持加载外部 HTTPS 图片）
	static const std::regex img_tag_re(R"re(<img\b[^>]*/?>)re", std::regex::icase);
	std::string preprocessed = std::regex_replace(markdown, img_tag_re, "");

	std::string html = "<html><body>";

	std::istringstream stream(preprocessed);
	std::string line;
	bool in_code_block = false;
	bool in_list = false;

	while (std::getline(stream, line)) {
		// 移除行尾 \r
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		// 代码块: ```
		if (line.substr(0, 3) == "```") {
			if (in_code_block) {
				html += "</pre>";
				in_code_block = false;
			} else {
				if (in_list) { html += "</ul>"; in_list = false; }
				html += "<pre>";
				in_code_block = true;
			}
			continue;
		}

		if (in_code_block) {
			html += HtmlEscape(line) + "\n";
			continue;
		}

		// 空行
		if (line.empty()) {
			if (in_list) { html += "</ul>"; in_list = false; }
			html += "<br>";
			continue;
		}

		std::string escaped = HtmlEscape(line);

		// 标题: # ## ### ####
		if (line[0] == '#') {
			if (in_list) { html += "</ul>"; in_list = false; }
			int level = 0;
			while (level < (int)line.size() && line[level] == '#') level++;
			if (level >= 1 && level <= 6 && level < (int)line.size() && line[level] == ' ') {
				std::string content = HtmlEscape(line.substr(level + 1));
				content = ProcessInlineMarkdown(content);
				html += "<h" + std::to_string(level) + ">" + content + "</h" + std::to_string(level) + ">";
				continue;
			}
		}

		// 无序列表: - item 或 * item
		if ((line[0] == '-' || line[0] == '*') && line.size() > 1 && line[1] == ' ') {
			if (!in_list) { html += "<ul>"; in_list = true; }
			std::string content = HtmlEscape(line.substr(2));
			content = ProcessInlineMarkdown(content);
			html += "<li>" + content + "</li>";
			continue;
		}

		// 普通段落
		escaped = ProcessInlineMarkdown(escaped);
		html += escaped + "<br>";
	}

	if (in_list) html += "</ul>";
	if (in_code_block) html += "</pre>";

	html += "</body></html>";
	return html;
}

/// @brief 在 wxHtmlWindow 中点击链接时打开外部浏览器
class HtmlWindowWithLinks : public wxHtmlWindow {
public:
	using wxHtmlWindow::wxHtmlWindow;
	void OnLinkClicked(const wxHtmlLinkInfo& link) override {
		wxLaunchDefaultBrowser(link.GetHref());
	}
};

struct AegisubUpdateDescription {
	std::string url;
	std::string friendly_name;
	std::string description;
};

class VersionCheckerResultDialog final : public wxDialog {
	void OnClose(wxCloseEvent &evt);

	wxCheckBox *automatic_check_checkbox;

public:
	/// @brief 构造更新检查结果对话框
	/// @param has_update 是否有可用更新
	/// @param current_ver 当前版本号
	/// @param update 最新Release信息（可为空）
	VersionCheckerResultDialog(bool has_update, wxString const& current_ver,
	                           const AegisubUpdateDescription &update);

	bool ShouldPreventAppExit() const override { return false; }
};

VersionCheckerResultDialog::VersionCheckerResultDialog(bool has_update, wxString const& current_ver,
                                                       const AegisubUpdateDescription &update)
: wxDialog(nullptr, -1, _("Version Checker"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	const int controls_width = FromDIP(400);

	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

	// 版本状态区域
	wxSizer *status_sizer = new wxBoxSizer(wxVERTICAL);

	if (has_update) {
		auto *status_text = new wxStaticText(this, -1, _("An update to Aegisub is available!"));
		wxFont status_font = status_text->GetFont();
		status_font.SetPointSize(status_font.GetPointSize() + 2);
		status_font.SetWeight(wxFONTWEIGHT_BOLD);
		status_text->SetFont(status_font);
		status_text->SetForegroundColour(wxColour(0, 128, 0));
		status_sizer->Add(status_text, 0, wxBOTTOM, FromDIP(4));
	} else {
		auto *status_text = new wxStaticText(this, -1, _("Aegisub is up to date."));
		wxFont status_font = status_text->GetFont();
		status_font.SetPointSize(status_font.GetPointSize() + 2);
		status_font.SetWeight(wxFONTWEIGHT_BOLD);
		status_text->SetFont(status_font);
		status_sizer->Add(status_text, 0, wxBOTTOM, FromDIP(4));
	}

	// 当前版本 / 最新版本对比
	auto *ver_current = new wxStaticText(this, -1,
		wxString::Format(_("Current version: %s"), current_ver));
	status_sizer->Add(ver_current, 0, wxBOTTOM, FromDIP(2));

	if (!update.friendly_name.empty()) {
		auto *ver_latest = new wxStaticText(this, -1,
			wxString::Format(_("Latest version: %s"), to_wx(update.friendly_name)));
		if (has_update)
			ver_latest->SetForegroundColour(wxColour(0, 128, 0));
		status_sizer->Add(ver_latest, 0, wxBOTTOM, FromDIP(2));
	}

	main_sizer->Add(status_sizer, 0, wxEXPAND|wxBOTTOM, FromDIP(8));
	main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND|wxBOTTOM, FromDIP(8));

	// Release Notes 区域
	if (!update.description.empty()) {
		auto *notes_label = new wxStaticText(this, -1, _("Release Notes:"));
		wxFont notes_font = notes_label->GetFont();
		notes_font.SetWeight(wxFONTWEIGHT_BOLD);
		notes_label->SetFont(notes_font);
		main_sizer->Add(notes_label, 0, wxBOTTOM, FromDIP(4));

		auto *descbox = new HtmlWindowWithLinks(this, -1, wxDefaultPosition,
			FromDIP(wxSize(controls_width, 240)), wxHW_SCROLLBAR_AUTO);
		descbox->SetPage(to_wx(MarkdownToHtml(update.description)));
		main_sizer->Add(descbox, 1, wxEXPAND|wxBOTTOM, FromDIP(8));
	}

	// 发行页面链接
	if (!update.url.empty()) {
		wxString link_label = has_update ? _("Download from GitHub") : _("View on GitHub");
		main_sizer->Add(new wxHyperlinkCtrl(this, -1,
			link_label, to_wx(update.url)),
			0, wxALIGN_LEFT|wxBOTTOM, FromDIP(8));
	}

	main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND|wxBOTTOM, FromDIP(8));

	// 底部: 自动检查 + 按钮
	automatic_check_checkbox = new wxCheckBox(this, -1, _("&Auto Check for Updates"));
	automatic_check_checkbox->SetValue(OPT_GET("App/Auto/Check For Updates")->GetBool());
	main_sizer->Add(automatic_check_checkbox, 0, wxEXPAND|wxBOTTOM, FromDIP(8));

	auto *button_sizer = new wxStdDialogButtonSizer();
	wxButton *close_button = new wxButton(this, wxID_OK, _("&Close"));
	button_sizer->AddButton(close_button);
	if (has_update) {
		auto *remind_btn = new wxButton(this, wxID_NO, _("Remind me again in a &week"));
		button_sizer->AddButton(remind_btn);
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			time_t new_next_check_time = time(nullptr) + 7*24*60*60;
			OPT_SET("Version/Next Check")->SetInt(new_next_check_time);
			Close();
		}, wxID_NO);
	}
	button_sizer->Realize();
	main_sizer->Add(button_sizer, 0, wxEXPAND, 0);

	wxSizer *outer_sizer = new wxBoxSizer(wxVERTICAL);
	outer_sizer->Add(main_sizer, 1, wxALL|wxEXPAND, FromDIP(12));

	SetSizerAndFit(outer_sizer);
	SetMinSize(FromDIP(wxSize(360, 280)));
	Centre();
	Show();

	SetAffirmativeId(wxID_OK);
	SetEscapeId(wxID_OK);
	Bind(wxEVT_BUTTON, std::bind(&VersionCheckerResultDialog::Close, this, false), wxID_OK);
	Bind(wxEVT_CLOSE_WINDOW, &VersionCheckerResultDialog::OnClose, this);
}

void VersionCheckerResultDialog::OnClose(wxCloseEvent &) {
	OPT_SET("App/Auto/Check For Updates")->SetBool(automatic_check_checkbox->GetValue());
	Destroy();
}

DEFINE_EXCEPTION(VersionCheckError, agi::Exception);

void PostErrorEvent(bool interactive, wxString const& error_text) {
	if (interactive) {
		agi::dispatch::Main().Async([error_text]{
			wxMessageBox(error_text, _("Version Checker"), wxOK | wxICON_ERROR);
		});
	}
}

static const char * GetOSShortName() {
	int osver_maj, osver_min;
	wxOperatingSystemId osid = wxGetOsVersion(&osver_maj, &osver_min);

	if (osid & wxOS_WINDOWS_NT) {
		if (osver_maj == 5 && osver_min == 0)
			return "win2k";
		else if (osver_maj == 5 && osver_min == 1)
			return "winxp";
		else if (osver_maj == 5 && osver_min == 2)
			return "win2k3"; // this is also xp64
		else if (osver_maj == 6 && osver_min == 0)
			return "win60"; // vista and server 2008
		else if (osver_maj == 6 && osver_min == 1)
			return "win61"; // 7 and server 2008r2
		else if (osver_maj == 6 && osver_min == 2)
			return "win62"; // 8 and server 2012
		else if (osver_maj == 6 && osver_min == 3)
			return "win63"; // 8.1 and server 2012r2
		else if (osver_maj == 10 && osver_min == 0)
			return "win10"; // 10 or 11 and server 2016/2019
		else
			return "windows"; // future proofing? I doubt we run on nt4
	}
	// CF returns 0x10 for some reason, which wx has recently started
	// turning into 10
	else if (osid & wxOS_MAC_OSX_DARWIN && (osver_maj == 0x10 || osver_maj == 10)) {
		// ugliest hack in the world? nah.
		static char osxstring[] = "osx00";
		char minor = osver_min >> 4;
		char patch = osver_min & 0x0F;
		osxstring[3] = minor + ((minor<=9) ? '0' : ('a'-1));
		osxstring[4] = patch + ((patch<=9) ? '0' : ('a'-1));
		return osxstring;
	}
	else if (osid & wxOS_UNIX_LINUX)
		return "linux";
	else if (osid & wxOS_UNIX_FREEBSD)
		return "freebsd";
	else if (osid & wxOS_UNIX_OPENBSD)
		return "openbsd";
	else if (osid & wxOS_UNIX_NETBSD)
		return "netbsd";
	else if (osid & wxOS_UNIX_SOLARIS)
		return "solaris";
	else if (osid & wxOS_UNIX_AIX)
		return "aix";
	else if (osid & wxOS_UNIX_HPUX)
		return "hpux";
	else if (osid & wxOS_UNIX)
		return "unix";
	else if (osid & wxOS_OS2)
		return "os2";
	else if (osid & wxOS_DOS)
		return "dos";
	else
		return "unknown";
}

#ifdef WIN32
typedef BOOL (WINAPI * PGetUserPreferredUILanguages)(DWORD dwFlags, PULONG pulNumLanguages, wchar_t *pwszLanguagesBuffer, PULONG pcchLanguagesBuffer);

// Try using Win 6+ functions if available
static wxString GetUILanguage() {
	agi::scoped_holder<HMODULE, BOOL (__stdcall *)(HMODULE)> kernel32(LoadLibraryW(L"kernel32.dll"), FreeLibrary);
	if (!kernel32) return "";

	PGetUserPreferredUILanguages gupuil = (PGetUserPreferredUILanguages)GetProcAddress(kernel32, "GetUserPreferredUILanguages");
	if (!gupuil) return "";

	ULONG numlang = 0, output_len = 0;
	if (gupuil(MUI_LANGUAGE_NAME, &numlang, 0, &output_len) != TRUE || !output_len)
		return "";

	std::vector<wchar_t> output(output_len);
	if (!gupuil(MUI_LANGUAGE_NAME, &numlang, &output[0], &output_len) || numlang < 1)
		return "";

	// We got at least one language, just treat it as the only, and a null-terminated string
	return &output[0];
}

static wxString GetSystemLanguage() {
	wxString res = GetUILanguage();
	if (!res)
		// On an old version of Windows, let's just return the LANGID as a string
		res = fmt_wx("x-win%04x", GetUserDefaultUILanguage());

	return res;
}
#elif __APPLE__
static wxString GetSystemLanguage() {
	CFLocaleRef locale = CFLocaleCopyCurrent();
	CFStringRef localeName = (CFStringRef)CFLocaleGetValue(locale, kCFLocaleIdentifier);

	char buf[128] = { 0 };
	CFStringGetCString(localeName, buf, sizeof buf, kCFStringEncodingUTF8);
	CFRelease(locale);

	return wxString::FromUTF8(buf);

}
#else
static wxString GetSystemLanguage() {
	return wxLocale::GetLanguageInfo(wxLocale::GetSystemLanguage())->CanonicalName;
}
#endif

static wxString GetAegisubLanguage() {
	return to_wx(OPT_GET("App/Language")->GetString());
}

/// @brief 比较两个语义化版本号，判断远程版本是否更新
/// @param remote 远程版本号字符串 (如 "v3.4.3" 或 "3.4.3-RC1")
/// @param local 本地版本号字符串 (如 "3.4.2-RC2")
/// @return 远程版本大于本地版本时返回 true
bool IsNewerVersion(const std::string& remote, const std::string& local) {
	auto strip_v = [](const std::string& s) -> std::string {
		if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
			return s.substr(1);
		return s;
	};

	auto split_pre = [](const std::string& s) -> std::pair<std::string, std::string> {
		auto pos = s.find('-');
		if (pos != std::string::npos)
			return {s.substr(0, pos), s.substr(pos + 1)};
		return {s, ""};
	};

	auto parse_ver = [](const std::string& v) -> std::vector<int> {
		std::vector<int> parts;
		std::istringstream iss(v);
		std::string part;
		while (std::getline(iss, part, '.')) {
			try { parts.push_back(std::stoi(part)); }
			catch (...) { parts.push_back(0); }
		}
		while (parts.size() < 3) parts.push_back(0);
		return parts;
	};

	std::string r = strip_v(remote);
	std::string l = strip_v(local);

	auto [r_ver, r_pre] = split_pre(r);
	auto [l_ver, l_pre] = split_pre(l);

	auto rv = parse_ver(r_ver);
	auto lv = parse_ver(l_ver);

	for (size_t i = 0; i < 3; ++i) {
		if (rv[i] > lv[i]) return true;
		if (rv[i] < lv[i]) return false;
	}

	// 版本号相同时，正式版优先于预发布版 (无后缀 > 有后缀)
	if (r_pre.empty() && !l_pre.empty()) return true;
	if (!r_pre.empty() && l_pre.empty()) return false;

	// 两者都有预发布后缀时，按字典序比较
	return r_pre > l_pre;
}

size_t writeToStringCb(char *contents, size_t size, size_t nmemb, std::string *s) {
	s->append(contents, size * nmemb);
	return size * nmemb;
}

void DoCheck(bool interactive) {
	CURL *curl;
	CURLcode res_code;

	curl = curl_easy_init();
	if (!curl)
		throw VersionCheckError(from_wx(_("Curl could not be initialized.")));

	// 构建 GitHub Releases API 请求 URL
	std::string api_url = std::string(UPDATE_CHECKER_SERVER) + UPDATE_CHECKER_BASE_URL;
	curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, agi::format("Aegisub %s", GetAegisubLongVersionString()).c_str());

	// 设置 GitHub API 请求头
	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	std::string result;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStringCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

	res_code = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res_code != CURLE_OK) {
		std::string err_msg = agi::format(_("Checking for updates failed: %s."), curl_easy_strerror(res_code));
		throw VersionCheckError(err_msg);
	}

	// 解析 GitHub Releases API 的 JSON 响应
	std::istringstream json_stream(result);
	json::UnknownElement root;
	try {
		json::Reader::Read(root, json_stream);
	}
	catch (json::Exception const&) {
		throw VersionCheckError("Failed to parse update response JSON.");
	}

	json::Object const& obj = root;

	auto get_string = [&obj](const char* key) -> std::string {
		auto it = obj.find(key);
		if (it != obj.end()) {
			try { return static_cast<json::String const&>(it->second); }
			catch (...) {}
		}
		return {};
	};

	std::string tag_name = get_string("tag_name");
	std::string release_name = get_string("name");
	std::string body = get_string("body");
	std::string html_url = get_string("html_url");

	AegisubUpdateDescription update_info{
		html_url,
		release_name.empty() ? tag_name : release_name,
		body
	};

	bool has_update = !tag_name.empty() && IsNewerVersion(tag_name, GetVersionNumber());

	if (has_update || interactive) {
		wxString current_ver = to_wx(GetVersionNumber());
		agi::dispatch::Main().Async([has_update, current_ver, update_info]{
			new VersionCheckerResultDialog(has_update, current_ver, update_info);
		});
	}
}
}

void PerformVersionCheck(bool interactive) {
	agi::dispatch::Background().Async([interactive]{
		if (!interactive) {
			// Automatic checking enabled?
			if (!OPT_GET("App/Auto/Check For Updates")->GetBool())
				return;

			// Is it actually time for a check?
			time_t next_check = OPT_GET("Version/Next Check")->GetInt();
			if (next_check > time(nullptr))
				return;
		}

		if (!VersionCheckLock.try_lock()) return;

		try {
			DoCheck(interactive);
		}
		catch (const agi::Exception &e) {
			PostErrorEvent(interactive, fmt_tl(
				"There was an error checking for updates to Aegisub:\n%s\n\nIf other applications can access the Internet fine, this is probably a temporary server problem on our end.",
				e.GetMessage()));
		}
		catch (...) {
			PostErrorEvent(interactive, _("An unknown error occurred while checking for updates to Aegisub."));
		}

		VersionCheckLock.unlock();

		agi::dispatch::Main().Async([]{
			time_t new_next_check_time = time(nullptr) + 60*60; // in one hour
			OPT_SET("Version/Next Check")->SetInt(new_next_check_time);
		});
	});
}

#endif
