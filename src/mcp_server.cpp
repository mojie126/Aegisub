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

/// @file src/mcp_server.cpp
/// @brief 把 Aegisub 的能力注册进 libaegisub 的 MCP server
/// @ingroup mcp
///
/// register_tool 注册的 handler 在 GUI 主线程上被调用，可以安全访问 agi::Context，
/// 工具覆盖：工程信息，字幕读写，样式操作，时间轴调整，帧/时间转换，关键帧，
/// 视频/音频的打开与关闭，视频帧截图（内存 base64 或保存文件），GIF 导出，
/// 音频波形与频谱数据读取等

#include "mcp_server.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "async_video_provider.h"
#include "audio_controller.h"
#include "command/command.h"
#include "compat.h"
#include "frame_main.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "time_range.h"
#include "video_controller.h"
#include "video_export_utils.h"
#include "video_frame.h"
#include "video_out_gl.h"
#include "charset_detect.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/audio/analysis.h>
#include <libaegisub/audio/provider.h>
#include <libaegisub/cajun/elements.h>
#include <libaegisub/character_count.h>
#include <libaegisub/charset.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/keyframe.h>
#include <libaegisub/mcp/server.h>
#include <libaegisub/path.h>
#include <libaegisub/util.h>
#include <libaegisub/vfr.h>

#include <wx/clipbrd.h>
#include <wx/image.h>
#include <wx/imagjpeg.h>
#include <wx/imagpng.h>
#include <wx/mstream.h>

#include "gifski.h"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using json::Object;
using json::Array;
using json::UnknownElement;
using json::Null;

/// 取一个 JSON 对象字段，缺省返回空字符串
std::string GetString(Object const& obj, std::string_view key) {
	auto it = obj.find(key);
	if (it == obj.end()) return {};
	try {
		return static_cast<std::string const&>(it->second);
	} catch (...) {
		return {};
	}
}

/// 取一个 JSON 对象字段，缺省返回默认值
int64_t GetInt(Object const& obj, std::string_view key, int64_t def = 0) {
	auto it = obj.find(key);
	if (it == obj.end()) return def;
	try {
		return static_cast<int64_t const&>(it->second);
	} catch (...) {
		return def;
	}
}

/// 取一个 JSON 对象布尔字段，缺省返回默认值
bool GetBool(Object const& obj, std::string_view key, bool def = false) {
	auto it = obj.find(key);
	if (it == obj.end()) return def;
	try {
		return static_cast<bool const&>(it->second);
	} catch (...) {
		return def;
	}
}

/// 取一个 JSON 对象浮点字段，缺省返回默认值
double GetDouble(Object const& obj, std::string_view key, double def = 0.0) {
	auto it = obj.find(key);
	if (it == obj.end()) return def;
	try {
		return static_cast<double const&>(it->second);
	} catch (...) {
		try {
			return static_cast<double>(static_cast<int64_t const&>(it->second));
		} catch (...) {
			return def;
		}
	}
}

/// 把一条 AssStyle 序列化为 JSON 对象（含完整样式字段）
UnknownElement AssStyleToJson(AssStyle const& s) {
	Object obj;
	obj["name"] = s.name;
	obj["font"] = s.font;
	obj["fontsize"] = s.fontsize;
	obj["primary"] = s.primary.GetAssStyleFormatted();
	obj["secondary"] = s.secondary.GetAssStyleFormatted();
	obj["outline"] = s.outline.GetAssStyleFormatted();
	obj["shadow"] = s.shadow.GetAssStyleFormatted();
	obj["bold"] = s.bold;
	obj["italic"] = s.italic;
	obj["underline"] = s.underline;
	obj["strikeout"] = s.strikeout;
	obj["scalex"] = s.scalex;
	obj["scaley"] = s.scaley;
	obj["spacing"] = s.spacing;
	obj["angle"] = s.angle;
	obj["borderstyle"] = static_cast<int64_t>(s.borderstyle);
	obj["outline_w"] = s.outline_w;
	obj["shadow_w"] = s.shadow_w;
	obj["alignment"] = static_cast<int64_t>(s.alignment);
	obj["margin_l"] = static_cast<int64_t>(s.Margin[0]);
	obj["margin_r"] = static_cast<int64_t>(s.Margin[1]);
	obj["margin_v"] = static_cast<int64_t>(s.Margin[2]);
	obj["encoding"] = static_cast<int64_t>(s.encoding);
	return obj;
}

/// 把 fields 对象应用到 AssStyle 上，返回实际被设置字段数；
/// 颜色等解析失败时抛出 std::runtime_error
int ApplyStyleFields(AssStyle& s, Object const& fields) {
	int applied = 0;

	auto set_str = [&](std::string_view key, std::string AssStyle::*field) {
		auto it = fields.find(key);
		if (it == fields.end()) return;
		try { s.*field = static_cast<std::string const&>(it->second); ++applied; } catch (...) {}
	};
	auto set_double = [&](std::string_view key, double AssStyle::*field) {
		auto it = fields.find(key);
		if (it == fields.end()) return;
		try { s.*field = static_cast<double const&>(it->second); ++applied; } catch (...) {
			try { s.*field = static_cast<double>(static_cast<int64_t const&>(it->second)); ++applied; } catch (...) {}
		}
	};
	auto set_int = [&](std::string_view key, int AssStyle::*field) {
		auto it = fields.find(key);
		if (it == fields.end()) return;
		try { s.*field = static_cast<int>(static_cast<int64_t const&>(it->second)); ++applied; } catch (...) {}
	};
	auto set_bool = [&](std::string_view key, bool AssStyle::*field) {
		auto it = fields.find(key);
		if (it == fields.end()) return;
		try { s.*field = static_cast<bool const&>(it->second); ++applied; } catch (...) {}
	};
	auto set_color = [&](std::string_view key, agi::Color AssStyle::*field) {
		auto it = fields.find(key);
		if (it == fields.end()) return;
		std::string str;
		try { str = static_cast<std::string const&>(it->second); } catch (...) { return; }
		try {
			s.*field = agi::Color(str);
			++applied;
		} catch (...) {
			throw std::runtime_error("invalid color value for '" + std::string(key) + "': " + str);
		}
	};

	set_str("name", &AssStyle::name);
	set_str("font", &AssStyle::font);
	set_double("fontsize", &AssStyle::fontsize);
	set_color("primary", &AssStyle::primary);
	set_color("secondary", &AssStyle::secondary);
	set_color("outline", &AssStyle::outline);
	set_color("shadow", &AssStyle::shadow);
	set_bool("bold", &AssStyle::bold);
	set_bool("italic", &AssStyle::italic);
	set_bool("underline", &AssStyle::underline);
	set_bool("strikeout", &AssStyle::strikeout);
	set_double("scalex", &AssStyle::scalex);
	set_double("scaley", &AssStyle::scaley);
	set_double("spacing", &AssStyle::spacing);
	set_double("angle", &AssStyle::angle);
	set_int("borderstyle", &AssStyle::borderstyle);
	set_double("outline_w", &AssStyle::outline_w);
	set_double("shadow_w", &AssStyle::shadow_w);
	set_int("alignment", &AssStyle::alignment);
	for (int i = 0; i < 3; ++i) {
		std::string key = i == 0 ? "margin_l" : (i == 1 ? "margin_r" : "margin_v");
		auto it = fields.find(key);
		if (it == fields.end()) continue;
		try { s.Margin[i] = static_cast<int>(static_cast<int64_t const&>(it->second)); ++applied; } catch (...) {}
	}
	set_int("encoding", &AssStyle::encoding);

	return applied;
}

/// 按名称查找样式（大小写不敏感），找不到抛出异常
AssStyle& RequireStyle(agi::Context* ctx, std::string const& name) {
	auto* s = ctx->ass->GetStyle(name);
	if (!s)
		throw std::runtime_error("style not found: " + name);
	return *s;
}

/// 检查样式名唯一性（大小写不敏感），已存在则抛出异常
void RequireStyleNameAvailable(agi::Context* ctx, std::string const& name) {
	if (ctx->ass->GetStyle(name))
		throw std::runtime_error("style already exists: " + name);
}

/// 预验证样式字段中会抛异常的部分（颜色解析），
/// 避免 ApplyStyleFields 应用到活对象时中途失败导致部分修改且无撤销记录
void ValidateStyleFields(Object const& fields) {
	auto check_color = [&](std::string_view key) {
		auto it = fields.find(key);
		if (it == fields.end()) return;
		std::string str;
		try { str = static_cast<std::string const&>(it->second); } catch (...) { return; }
		try {
			agi::Color c(str);
		} catch (...) {
			throw std::runtime_error("invalid color value for '" + std::string(key) + "': " + str);
		}
	};
	check_color("primary");
	check_color("secondary");
	check_color("outline");
	check_color("shadow");
}

/// 对内存数据做 base64 编码
std::string Base64Encode(void const* data, size_t len) {
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	auto const* p = static_cast<unsigned char const*>(data);
	for (size_t i = 0; i < len; i += 3) {
		uint32_t v = static_cast<uint32_t>(p[i]) << 16;
		if (i + 1 < len) v |= static_cast<uint32_t>(p[i + 1]) << 8;
		if (i + 2 < len) v |= static_cast<uint32_t>(p[i + 2]);
		out += tbl[(v >> 18) & 0x3F];
		out += tbl[(v >> 12) & 0x3F];
		out += (i + 1 < len) ? tbl[(v >> 6) & 0x3F] : '=';
		out += (i + 2 < len) ? tbl[v & 0x3F] : '=';
	}
	return out;
}

/// 解析目标帧号：优先用 frame 参数，其次 ms 参数，缺省用当前帧
int64_t ResolveTargetFrame(agi::Context* ctx, Object const& args) {
	int64_t frame = GetInt(args, "frame", -1);
	int64_t ms = GetInt(args, "ms", -1);
	if (frame < 0 && ms >= 0)
		frame = ctx->videoController->FrameAtTime(static_cast<int>(std::min<int64_t>(ms, 2147483647)), agi::vfr::START);
	if (frame < 0)
		frame = ctx->videoController->GetFrameN();
	int total = ctx->project->VideoProvider()->GetFrameCount();
	return std::clamp<int64_t>(frame, 0, total > 0 ? total - 1 : 0);
}

/// 解码指定帧并做 HDR 色调映射与黑边填充，返回可保存/编码的 wxImage
wxImage GetVideoFrameImage(agi::Context* ctx, int64_t frame, bool raw) {
	auto* provider = ctx->project->VideoProvider();
	auto vf = provider->GetFrame(static_cast<int>(frame), ctx->project->Timecodes().TimeAtFrame(static_cast<int>(frame)), raw);
	if (!vf)
		throw std::runtime_error("failed to decode frame " + std::to_string(frame));
	wxImage img = GetImage(*vf);
	// HDR 色调映射与 GUI 截图/导出路径保持一致（字幕合成帧才做映射）
	if (OPT_GET("Video/HDR/Tone Mapping")->GetBool() && !raw) {
		VideoOutGL::ApplyHDRLutToImage(img, provider->GetHDRType(), provider->GetDVProfile());
	}
	if (vf->padding_top > 0 || vf->padding_bottom > 0)
		img = AddPaddingToImage(img, vf->padding_top, vf->padding_bottom);
	return img;
}

/// 构造一个最简单的 JSON Schema
UnknownElement MakeObjectSchema(Object properties, Array required = {}) {
	Object schema;
	schema["type"] = std::string("object");
	schema["properties"] = std::move(properties);
	if (!required.empty())
		schema["required"] = std::move(required);
	return schema;
}

/// 构造一个 string 类型属性
UnknownElement MakeStringProp(std::string const& desc, std::string const& def = {}) {
	Object p;
	p["type"] = std::string("string");
	p["description"] = desc;
	if (!def.empty()) p["default"] = def;
	return p;
}

/// 构造一个 integer 类型属性
UnknownElement MakeIntProp(std::string const& desc, int64_t def = 0) {
	Object p;
	p["type"] = std::string("integer");
	p["description"] = desc;
	if (def) p["default"] = static_cast<int64_t>(def);
	return p;
}

/// 构造一个 bool 类型属性
UnknownElement MakeBoolProp(std::string const& desc, bool def = false) {
	Object p;
	p["type"] = std::string("boolean");
	p["description"] = desc;
	p["default"] = def;
	return p;
}

/// 构造一个 array of integer 类型属性
UnknownElement MakeIntArrayProp(std::string const& desc) {
	Object item;
	item["type"] = std::string("integer");
	Object p;
	p["type"] = std::string("array");
	p["description"] = desc;
	p["items"] = std::move(item);
	return p;
}

/// 把一条 AssDialogue 序列化为 JSON 对象
UnknownElement DialogueToJson(AssDialogue const& diag, int idx) {
	Object obj;
	obj["index"] = static_cast<int64_t>(idx);
	obj["class"] = std::string(diag.Comment ? "comment" : "dialogue");
	obj["layer"] = static_cast<int64_t>(diag.Layer);
	obj["start"] = static_cast<int64_t>(diag.Start);
	obj["end"] = static_cast<int64_t>(diag.End);
	obj["style"] = diag.Style.get();
	obj["actor"] = diag.Actor.get();
	obj["effect"] = diag.Effect.get();
	obj["text"] = diag.Text.get();
	obj["margin_l"] = static_cast<int64_t>(diag.Margin[0]);
	obj["margin_r"] = static_cast<int64_t>(diag.Margin[1]);
	obj["margin_v"] = static_cast<int64_t>(diag.Margin[2]);
	return obj;
}

/// 获取当前活动上下文，如果为空则抛出异常
agi::Context* RequireContext() {
	auto* ctx = agi::mcp::ActiveContext();
	if (!ctx) throw std::runtime_error("no active Aegisub context");
	return ctx;
}

// ---------------------------------------------------------------------------
// 工具 handler 实现
// ---------------------------------------------------------------------------

/// 获取工程信息
UnknownElement HandleGetProjectInfo(Object const& /*args*/) {
	auto* ctx = RequireContext();
	Object info;
	info["filename"] = ctx->subsController->Filename().string();
	info["is_modified"] = ctx->subsController->IsModified();
	info["has_video"] = !!ctx->project->VideoProvider();
	info["has_audio"] = !!ctx->project->AudioProvider();
	info["total_lines"] = static_cast<int64_t>(ctx->ass->Events.size());
	info["style_count"] = static_cast<int64_t>(ctx->ass->Styles.size());
	return info;
}

/// 从已注册命令中找出与给定名称最相近的若干命令名，用于错误提示
std::vector<std::string> FindSimilarCommands(std::string_view name, size_t max_count = 3) {
	std::vector<std::pair<size_t, std::string>> scored;
	// 编辑距离阈值：名称越长容错越多，但不超过名称长度的三分之一
	size_t threshold = std::max<size_t>(3, name.size() / 3);
	for (auto const& cmd_name : cmd::get_registered_commands()) {
		size_t dist = agi::util::edit_distance(name, cmd_name);
		if (dist <= threshold)
			scored.emplace_back(dist, std::string(cmd_name));
	}
	std::sort(scored.begin(), scored.end());
	std::vector<std::string> ret;
	for (auto& [dist, cmd_name] : scored) {
		ret.emplace_back(std::move(cmd_name));
		if (ret.size() >= max_count) break;
	}
	return ret;
}

/// 执行已注册命令
UnknownElement HandleRunCommand(Object const& args) {
	auto command = GetString(args, "command");
	if (command.empty())
		throw std::runtime_error("missing 'command'");
	bool executed = false;
	try {
		executed = cmd::call(command, RequireContext());
	} catch (cmd::CommandNotFound const&) {
		// 命令名错误时给出相近命令建议，便于 AI 客户端自行纠正
		std::string msg = "'" + command + "' is not a valid command name";
		auto suggestions = FindSimilarCommands(command);
		if (!suggestions.empty())
			msg += ", did you mean: " + boost::join(suggestions, ", ") + "?";
		msg += ", use list_commands to query all valid command names";
		throw std::runtime_error(msg);
	}
	Object result;
	result["ok"] = true;
	result["command"] = command;
	// Validate 未通过（如缺少视频/音频/选中行）时命令不会执行，返回 false 便于调用方感知
	result["executed"] = executed;
	return result;
}

/// 已知会弹出模态对话框、文件选择器、确认框或输入框的命令，
/// 执行后需要用户交互，AI 调用会阻塞直到用户关闭对话框，
/// 名单对照 docs/hotkey_commands.md 全量命令逐一核对（源码中调用
/// ShowXxxDialog / OpenFileSelector / SaveFileSelector / wxMessageBox /
/// wxGetTextFromUser / GetColorFromUser / GetFontFromUser / PickLanguage /
/// TryToClose 等阻塞 API 的命令），
/// 以 '/' 结尾的条目按前缀匹配（如 recent/subtitle/ 匹配 recent/subtitle/0）
static const std::set<std::string_view, std::less<>> blocking_commands = {
	// 自动化
	"am/manager",
	"am/meta",
	// 应用程序
	"app/about",
	"app/clear_all",
	"app/clear_autosave",
	"app/clear_cache",
	"app/clear_log",
	"app/clear_recent",
	"app/exit",
	"app/language",
	"app/options",
	// 音频
	"audio/open",
	"audio/save/clip",
	// 编辑
	"edit/color/outline",
	"edit/color/primary",
	"edit/color/secondary",
	"edit/color/shadow",
	"edit/find_replace",
	"edit/font",
	"edit/line/paste/over",
	// 关键帧
	"keyframe/open",
	"keyframe/save",
	// 最近文件（带序号动态注册，如 recent/subtitle/0）
	"recent/subtitle/",
	// 字幕文件
	"subtitle/apply/mocha",
	"subtitle/apply/perspective",
	"subtitle/attachment",
	"subtitle/close",
	"subtitle/find",
	"subtitle/find/next",
	"subtitle/new",
	"subtitle/open",
	"subtitle/open/autosave",
	"subtitle/open/charset",
	"subtitle/open/video",
	"subtitle/properties",
	"subtitle/save",
	"subtitle/save/as",
	"subtitle/spellcheck",
	// 时间调整
	"time/align",
	"time/shift",
	// 时间码
	"timecode/open",
	"timecode/save",
	// 工具
	"tool/export",
	"tool/font_collector",
	"tool/line/select",
	"tool/resampleres",
	"tool/style/manager",
	"tool/time/kanji",
	"tool/time/postprocess",
	"tool/translation_assistant",
	// 视频
	"video/aspect/custom",
	"video/details",
	"video/frame/save/export",
	"video/import/image_sequence",
	"video/jump",
	"video/open",
	"video/open/dummy",
	"video/open/image",
	"video/save/clip",
	"video/save/gif",
};

/// 已提供专用 MCP 工具的命令 -> 工具名，AI 应优先使用专用工具而非 run_command
static const std::map<std::string_view, std::string_view, std::less<>> command_to_tool = {
	{"audio/close", "close_audio"},
	{"audio/open", "open_audio"},
	{"audio/play/line", "play_audio"},
	{"audio/play/selection", "play_audio"},
	{"edit/find_replace", "search_dialogue"},
	{"edit/redo", "redo"},
	{"edit/undo", "undo"},
	{"subtitle/find", "search_dialogue"},
	{"subtitle/new", "new_file"},
	{"subtitle/open", "open_file"},
	{"subtitle/save", "save_file"},
	{"subtitle/save/as", "save_file"},
	{"time/shift", "shift_times"},
	{"video/close", "close_video"},
	{"video/frame/save", "save_video_frame"},
	{"video/open", "open_video"},
	{"video/save/gif", "export_gif"},
};

/// 判断命令是否在阻塞名单中，
/// 名单项以 '/' 结尾时按前缀匹配（如 recent/subtitle/ 匹配 recent/subtitle/0）
bool IsBlockingCommand(std::string_view name) {
	if (::blocking_commands.count(name) > 0)
		return true;
	for (auto const& prefix : ::blocking_commands) {
		if (prefix.size() > 1 && prefix.back() == '/' &&
			name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
			return true;
	}
	return false;
}

/// 列出所有已注册命令名，附阻塞与专用工具信息
UnknownElement HandleListCommands(Object const& /*args*/) {
	Array commands;
	for (auto const& name : cmd::get_registered_commands()) {
		Object item;
		item["name"] = std::string(name);
		item["blocking"] = ::IsBlockingCommand(name);
		auto it = ::command_to_tool.find(name);
		if (it != ::command_to_tool.end())
			item["tool"] = std::string(it->second);
		commands.emplace_back(std::move(item));
	}
	Object result;
	result["commands"] = std::move(commands);
	return result;
}

/// 读取当前字幕文件中的 dialogue 行
UnknownElement HandleGetDialogueLines(Object const& args) {
	auto* ctx = RequireContext();
	int64_t limit = GetInt(args, "limit", 0);
	int64_t offset = GetInt(args, "offset", 0);
	bool include_comment = false;
	auto it = args.find("include_comment");
	if (it != args.end()) {
		try { include_comment = static_cast<bool const&>(it->second); } catch (...) {}
	}

	Array lines;
	int64_t idx = 0;
	for (auto& diag : ctx->ass->Events) {
		++idx;
		if (offset > 0 && idx <= offset) continue;
		if (diag.Comment && !include_comment) continue;
		lines.emplace_back(DialogueToJson(diag, static_cast<int>(idx)));
		if (limit > 0 && lines.size() >= static_cast<size_t>(limit)) break;
	}
	Object result;
	result["lines"] = std::move(lines);
	result["total"] = static_cast<int64_t>(ctx->ass->Events.size());
	return result;
}

/// 获取所有样式
UnknownElement HandleGetStyles(Object const& /*args*/) {
	auto* ctx = RequireContext();
	Array styles;
	for (auto& s : ctx->ass->Styles)
		styles.emplace_back(AssStyleToJson(s));
	Object result;
	result["styles"] = std::move(styles);
	return result;
}

/// 获取单个样式完整定义
UnknownElement HandleGetStyle(Object const& args) {
	auto* ctx = RequireContext();
	std::string name = GetString(args, "name");
	if (name.empty())
		throw std::runtime_error("missing 'name'");
	Object result;
	result["style"] = AssStyleToJson(::RequireStyle(ctx, name));
	return result;
}

/// 新建样式
UnknownElement HandleAddStyle(Object const& args) {
	auto* ctx = RequireContext();
	auto fit = args.find("fields");
	if (fit == args.end())
		throw std::runtime_error("missing 'fields'");
	Object const* fields = nullptr;
	try {
		fields = &static_cast<Object const&>(fit->second);
	} catch (...) {
		throw std::runtime_error("'fields' must be an object");
	}

	std::string name = GetString(*fields, "name");
	if (name.empty())
		throw std::runtime_error("'fields.name' is required");
	::RequireStyleNameAvailable(ctx, name);

	AssStyle style;
	// 字段校验失败时不允许部分创建：先应用到临时样式上
	::ApplyStyleFields(style, *fields);
	style.name = name;
	style.UpdateData();
	// intrusive list 需要堆分配对象，局部对象会在离开作用域时被 auto_unlink 摘除
	ctx->ass->Styles.push_back(*new AssStyle(style));

	auto undo = GetString(args, "undo_point");
	ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("style add"), AssFile::COMMIT_STYLES);

	Object result;
	result["ok"] = true;
	result["style"] = AssStyleToJson(*ctx->ass->GetStyle(name));
	return result;
}

/// 修改已有样式
UnknownElement HandleUpdateStyle(Object const& args) {
	auto* ctx = RequireContext();
	std::string name = GetString(args, "name");
	if (name.empty())
		throw std::runtime_error("missing 'name'");

	auto fit = args.find("fields");
	if (fit == args.end())
		throw std::runtime_error("missing 'fields'");
	Object const* fields = nullptr;
	try {
		fields = &static_cast<Object const&>(fit->second);
	} catch (...) {
		throw std::runtime_error("'fields' must be an object");
	}

	// 先验证会抛异常的字段（颜色），再应用；
	// 不能拷贝 AssStyle（intrusive hook 浅拷贝会破坏 Styles 链表），
	// 也不能在应用到活对象中途失败（会导致部分修改且无撤销记录）
	::ValidateStyleFields(*fields);
	AssStyle& style = ::RequireStyle(ctx, name);
	std::string new_name = GetString(*fields, "name");
	if (!new_name.empty() && new_name != name) {
		::RequireStyleNameAvailable(ctx, new_name);
		style.name = new_name;
	}
	::ApplyStyleFields(style, *fields);
	style.UpdateData();

	auto undo = GetString(args, "undo_point");
	ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("style edit"), AssFile::COMMIT_STYLES);

	Object result;
	result["ok"] = true;
	result["style"] = AssStyleToJson(style);
	return result;
}

/// 删除样式
UnknownElement HandleDeleteStyle(Object const& args) {
	auto* ctx = RequireContext();
	std::string name = GetString(args, "name");
	if (name.empty())
		throw std::runtime_error("missing 'name'");
	AssStyle& style = ::RequireStyle(ctx, name);
	// erase 只摘除节点不释放对象，用 unique_ptr 接管所有权
	std::unique_ptr<AssStyle> owner(&style);
	ctx->ass->Styles.erase(ctx->ass->Styles.iterator_to(style));

	// 将引用被删样式的行回退为 Default，避免渲染时找不到样式
	if (ctx->ass->GetStyle("Default")) {
		int updated = 0;
		for (auto& diag : ctx->ass->Events) {
			if (boost::iequals(diag.Style.get(), name)) {
				diag.Style = "Default";
				++updated;
			}
		}
		if (updated > 0) {
			Object result;
			result["ok"] = true;
			result["reassigned"] = static_cast<int64_t>(updated);
			auto undo = GetString(args, "undo_point");
			ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("style delete"), AssFile::COMMIT_DIAG_META | AssFile::COMMIT_STYLES);
			return result;
		}
	}

	auto undo = GetString(args, "undo_point");
	ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("style delete"), AssFile::COMMIT_STYLES);

	Object result;
	result["ok"] = true;
	result["reassigned"] = 0;
	return result;
}

/// 在字幕中搜索文本
UnknownElement HandleSearchDialogue(Object const& args) {
	auto* ctx = RequireContext();
	auto pattern = GetString(args, "pattern");
	if (pattern.empty())
		throw std::runtime_error("missing 'pattern'");
	bool regex = false;
	auto it = args.find("regex");
	if (it != args.end()) {
		try { regex = static_cast<bool const&>(it->second); } catch (...) {}
	}

	Array matches;
	int64_t idx = 0;
	for (auto& diag : ctx->ass->Events) {
		++idx;
		auto text = diag.Text.get();
		bool hit;
		if (regex) {
			try {
				hit = std::regex_search(text, std::regex(pattern));
			} catch (...) {
				throw std::runtime_error("invalid regex pattern");
			}
		} else {
			hit = text.find(pattern) != std::string::npos;
		}
		if (hit) {
			auto entry = DialogueToJson(diag, static_cast<int>(idx));
			matches.emplace_back(std::move(entry));
		}
	}
	Object result;
	result["matches"] = std::move(matches);
	result["total"] = static_cast<int64_t>(ctx->ass->Events.size());
	return result;
}

/// 更新字幕行字段
UnknownElement HandleUpdateSubtitleFields(Object const& args) {
	auto* ctx = RequireContext();

	auto it = args.find("line_index");
	if (it == args.end()) throw std::runtime_error("missing 'line_index'");
	Array const* indices = nullptr;
	try {
		indices = &static_cast<Array const&>(it->second);
	} catch (...) {
		throw std::runtime_error("'line_index' must be an array");
	}

	Object const* fields = nullptr;
	auto fit = args.find("fields");
	if (fit == args.end()) throw std::runtime_error("missing 'fields'");
	try {
		fields = &static_cast<Object const&>(fit->second);
	} catch (...) {
		throw std::runtime_error("'fields' must be an object");
	}

	int updated = 0;
	int line_num = 0;
	for (auto& diag : ctx->ass->Events) {
		++line_num;
		bool matched = false;
		for (auto const& idx_val : *indices) {
			try {
				if (static_cast<int64_t const&>(idx_val) == line_num) {
					matched = true;
					break;
				}
			} catch (...) {}
		}
		if (!matched) continue;

		auto set_str = [&](std::string_view key, boost::flyweight<std::string> AssDialogue::*field) {
			auto f = fields->find(std::string(key));
			if (f != fields->end()) {
				try { diag.*field = static_cast<std::string const&>(f->second); } catch (...) {}
			}
		};
		auto set_str_plain = [&](std::string_view key, std::string AssDialogue::*field) {
			auto f = fields->find(std::string(key));
			if (f != fields->end()) {
				try { diag.*field = static_cast<std::string const&>(f->second); } catch (...) {}
			}
		};

		set_str("text", &AssDialogue::Text);
		set_str("style", &AssDialogue::Style);
		set_str("actor", &AssDialogue::Actor);
		set_str("effect", &AssDialogue::Effect);

		// 整数字段（时间戳与图层做范围校验，避免异常值损坏数据）
		if (auto f = fields->find("layer"); f != fields->end()) {
			try {
				int64_t v = static_cast<int64_t const&>(f->second);
				if (v < -10000 || v > 10000) throw std::runtime_error("'layer' out of range (-10000..10000)");
				diag.Layer = static_cast<int>(v);
			} catch (std::runtime_error const&) { throw; } catch (...) {}
		}
		if (auto f = fields->find("start"); f != fields->end()) {
			try {
				int64_t v = static_cast<int64_t const&>(f->second);
				if (v < 0 || v > 2147483647) throw std::runtime_error("'start' out of range (0..2147483647 ms)");
				diag.Start = static_cast<int>(v);
			} catch (std::runtime_error const&) { throw; } catch (...) {}
		}
		if (auto f = fields->find("end"); f != fields->end()) {
			try {
				int64_t v = static_cast<int64_t const&>(f->second);
				if (v < 0 || v > 2147483647) throw std::runtime_error("'end' out of range (0..2147483647 ms)");
				diag.End = static_cast<int>(v);
			} catch (std::runtime_error const&) { throw; } catch (...) {}
		}

		// 布尔字段
		if (auto f = fields->find("comment"); f != fields->end()) {
			try { diag.Comment = static_cast<bool const&>(f->second); } catch (...) {}
		}

		// 边距
		auto mf = fields->find("margin_l");
		if (mf != fields->end()) {
			try { diag.Margin[0] = static_cast<int>(static_cast<int64_t const&>(mf->second)); } catch (...) {}
		}
		mf = fields->find("margin_r");
		if (mf != fields->end()) {
			try { diag.Margin[1] = static_cast<int>(static_cast<int64_t const&>(mf->second)); } catch (...) {}
		}
		mf = fields->find("margin_v");
		if (mf != fields->end()) {
			try { diag.Margin[2] = static_cast<int>(static_cast<int64_t const&>(mf->second)); } catch (...) {}
		}

		++updated;
	}

	if (updated > 0) {
		auto undo = GetString(args, "undo_point");
		ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("MCP edit"), AssFile::COMMIT_DIAG_TEXT);
	}

	Object result;
	result["ok"] = true;
	result["updated"] = static_cast<int64_t>(updated);
	return result;
}

/// 插入新字幕行
UnknownElement HandleInsertSubtitleLine(Object const& args) {
	auto* ctx = RequireContext();

	AssDialogue diag;

	// 从 fields 参数加载字段
	auto fit = args.find("fields");
	if (fit != args.end()) {
		try {
			auto& fields = static_cast<Object const&>(fit->second);
			if (auto it = fields.find("text"); it != fields.end())
				diag.Text = static_cast<std::string const&>(it->second);
			if (auto it = fields.find("style"); it != fields.end())
				diag.Style = static_cast<std::string const&>(it->second);
			if (auto it = fields.find("actor"); it != fields.end())
				diag.Actor = static_cast<std::string const&>(it->second);
			if (auto it = fields.find("effect"); it != fields.end())
				diag.Effect = static_cast<std::string const&>(it->second);
			if (auto it = fields.find("layer"); it != fields.end())
				diag.Layer = static_cast<int>(static_cast<int64_t const&>(it->second));
			if (auto it = fields.find("start"); it != fields.end())
				diag.Start = static_cast<int>(static_cast<int64_t const&>(it->second));
			if (auto it = fields.find("end"); it != fields.end())
				diag.End = static_cast<int>(static_cast<int64_t const&>(it->second));
			if (auto it = fields.find("comment"); it != fields.end())
				diag.Comment = static_cast<bool const&>(it->second);
		} catch (...) {}
	}

	// intrusive list 需要堆分配对象，局部对象会在离开作用域时被 auto_unlink 摘除
	ctx->ass->Events.push_back(*new AssDialogue(diag));

	auto undo = GetString(args, "undo_point");
	ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("insert line"), AssFile::COMMIT_DIAG_ADDREM);

	auto& inserted = ctx->ass->Events.back();
	ctx->selectionController->SetSelectionAndActive({&inserted}, &inserted);

	Object result;
	result["ok"] = true;
	result["line_index"] = static_cast<int64_t>(ctx->ass->Events.size());
	return result;
}

/// 删除字幕行
UnknownElement HandleDeleteSubtitleLine(Object const& args) {
	auto* ctx = RequireContext();

	auto it = args.find("line_index");
	if (it == args.end()) throw std::runtime_error("missing 'line_index'");
	Array const* indices = nullptr;
	try {
		indices = &static_cast<Array const&>(it->second);
	} catch (...) {
		throw std::runtime_error("'line_index' must be an array");
	}
	// 收集要删除的行(从大到小排序避免索引偏移)
	std::vector<std::unique_ptr<AssDialogue>> to_delete;
	int line_num = 0;
	for (auto& diag : ctx->ass->Events) {
		++line_num;
		for (auto const& idx_val : *indices) {
			try {
				if (static_cast<int64_t const&>(idx_val) == line_num) {
					to_delete.emplace_back(&diag);
					break;
				}
			} catch (...) {}
		}
	}

	// 从大到小排序（按内部指针指向的 Row 排序，需先取出裸指针）
	std::vector<AssDialogue*> ptrs;
	ptrs.reserve(to_delete.size());
	for (auto& p : to_delete) ptrs.push_back(p.get());
	std::sort(ptrs.begin(), ptrs.end(), [](AssDialogue* a, AssDialogue* b) {
		return a->Row > b->Row;
	});

	for (auto* d : ptrs) {
		// erase 只摘除节点不释放对象，unique_ptr 在函数结束时统一 delete
		ctx->ass->Events.erase(ctx->ass->iterator_to(*d));
	}

	auto undo = GetString(args, "undo_point");
	ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("delete lines"), AssFile::COMMIT_DIAG_ADDREM);

	// 重建 selection：过滤掉已删除的行；若为空则选第一行；
	// 否则 SelectionController 会持有指向已释放对象的悬空指针（与 Close 悬空问题同类）
	if (!ctx->ass->Events.empty()) {
		Selection new_sel;
		AssDialogue* active = ctx->selectionController->GetActiveLine();
		for (auto& diag : ctx->ass->Events) {
			if (ctx->selectionController->GetSelectedSet().count(&diag))
				new_sel.insert(&diag);
		}
		if (new_sel.empty())
			new_sel.insert(&*ctx->ass->Events.begin());
		if (!new_sel.count(active))
			active = *new_sel.begin();
		ctx->selectionController->SetSelectionAndActive(std::move(new_sel), active);
	}

	Object result;
	result["ok"] = true;
	result["deleted"] = static_cast<int64_t>(ptrs.size());
	return result;
}

/// 偏移时间轴
UnknownElement HandleShiftTimes(Object const& args) {
	auto* ctx = RequireContext();

	int64_t start_offset = GetInt(args, "start_offset", 0);
	int64_t end_offset = GetInt(args, "end_offset", 0);
	int64_t offset = GetInt(args, "offset", 0);
	if (offset != 0) {
		start_offset = offset;
		end_offset = offset;
	}
	// 偏移量限制在合理范围，避免 int 溢出
	constexpr int64_t MAX_OFFSET = 24 * 60 * 60 * 1000; // 24 小时
	start_offset = std::clamp<int64_t>(start_offset, -MAX_OFFSET, MAX_OFFSET);
	end_offset = std::clamp<int64_t>(end_offset, -MAX_OFFSET, MAX_OFFSET);

	// 处理选定行范围: 若提供 line_indices 则只改这些行
	Array const* indices = nullptr;
	auto iit = args.find("line_indices");
	if (iit != args.end()) {
		try {
			indices = &static_cast<Array const&>(iit->second);
		} catch (...) {}
	}

	int shifted = 0;
	int line_num = 0;
	for (auto& diag : ctx->ass->Events) {
		++line_num;
		if (indices) {
			bool matched = false;
			for (auto const& idx_val : *indices) {
				try {
					if (static_cast<int64_t const&>(idx_val) == line_num) {
						matched = true;
						break;
					}
				} catch (...) {}
			}
			if (!matched) continue;
		}
		// 用 int64 计算再 clamp，避免 int 溢出
		diag.Start = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(diag.Start) + start_offset, 0, 2147483647));
		diag.End = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(diag.End) + end_offset, 0, 2147483647));
		++shifted;
	}

	if (shifted > 0) {
		auto undo = GetString(args, "undo_point");
		ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("shift times"), AssFile::COMMIT_DIAG_TEXT);
	}

	Object result;
	result["ok"] = true;
	result["shifted"] = static_cast<int64_t>(shifted);
	return result;
}

/// 获取当前音频选区
UnknownElement HandleGetAudioSelection(Object const& /*args*/) {
	auto* ctx = RequireContext();
	if (!ctx->audioController) {
		Object result;
		result["has_audio"] = false;
		return result;
	}
	auto range = ctx->audioController->GetPrimaryPlaybackRange();
	Object result;
	result["has_audio"] = !!ctx->project->AudioProvider();
	result["start"] = static_cast<int64_t>(range.begin());
	result["end"] = static_cast<int64_t>(range.end());
	result["duration"] = static_cast<int64_t>(range.length());
	return result;
}

/// 撤销
UnknownElement HandleUndo(Object const& /*args*/) {
	auto* ctx = RequireContext();
	ctx->subsController->Undo();
	Object result;
	result["ok"] = true;
	return result;
}

/// 重做
UnknownElement HandleRedo(Object const& /*args*/) {
	auto* ctx = RequireContext();
	ctx->subsController->Redo();
	Object result;
	result["ok"] = true;
	return result;
}

/// 获取当前选中的行
UnknownElement HandleGetSelectedLines(Object const& /*args*/) {
	auto* ctx = RequireContext();
	auto const& sel = ctx->selectionController->GetSelectedSet();
	Array indices;
	Array lines;
	int64_t idx = 0;
	for (auto& diag : ctx->ass->Events) {
		++idx;
		if (sel.count(&diag)) {
			indices.emplace_back(idx);
			lines.emplace_back(DialogueToJson(diag, static_cast<int>(idx)));
		}
	}
	Object result;
	result["indices"] = std::move(indices);
	result["lines"] = std::move(lines);
	result["count"] = static_cast<int64_t>(sel.size());
	return result;
}

/// 毫秒转帧号
UnknownElement HandleFrameFromMs(Object const& args) {
	auto* ctx = RequireContext();
	int64_t ms = GetInt(args, "ms", 0);
	if (!ctx->project->Timecodes().IsLoaded())
		throw std::runtime_error("no timecodes loaded");
	if (!ctx->videoController)
		throw std::runtime_error("no video controller available");
	if (ms < 0 || ms > 2147483647)
		throw std::runtime_error("ms value out of range");
	Object result;
	result["frame"] = static_cast<int64_t>(ctx->videoController->FrameAtTime(static_cast<int>(ms), agi::vfr::START));
	result["ms"] = ms;
	return result;
}

/// 帧号转毫秒
UnknownElement HandleMsFromFrame(Object const& args) {
	auto* ctx = RequireContext();
	int64_t frame = GetInt(args, "frame", 0);
	if (!ctx->project->Timecodes().IsLoaded())
		throw std::runtime_error("no timecodes loaded");
	if (!ctx->videoController)
		throw std::runtime_error("no video controller available");
	if (frame < 0 || frame > 2147483647)
		throw std::runtime_error("frame value out of range");
	Object result;
	result["ms"] = static_cast<int64_t>(ctx->videoController->TimeAtFrame(static_cast<int>(frame), agi::vfr::START));
	result["frame"] = frame;
	return result;
}

/// 获取视频尺寸
UnknownElement HandleVideoSize(Object const& /*args*/) {
	auto* ctx = RequireContext();
	if (!ctx->project->VideoProvider() || !ctx->videoController) {
		Object result;
		result["has_video"] = false;
		return result;
	}
	auto provider = ctx->project->VideoProvider();
	Object result;
	result["has_video"] = true;
	result["width"] = static_cast<int64_t>(provider->GetWidth());
	result["height"] = static_cast<int64_t>(provider->GetHeight());
	result["aspect_ratio"] = ctx->videoController->GetAspectRatioValue();
	result["aspect_ratio_type"] = static_cast<int64_t>(ctx->videoController->GetAspectRatioType());
	return result;
}

/// 获取关键帧列表
UnknownElement HandleKeyframes(Object const& /*args*/) {
	auto* ctx = RequireContext();
	auto const& kfs = ctx->project->Keyframes();
	Array frames;
	frames.reserve(kfs.size());
	for (auto f : kfs)
		frames.emplace_back(static_cast<int64_t>(f));
	Object result;
	result["keyframes"] = std::move(frames);
	result["count"] = static_cast<int64_t>(kfs.size());
	return result;
}

/// 获取工程属性（Script Info 键值对）
UnknownElement HandleProjectProperties(Object const& /*args*/) {
	auto* ctx = RequireContext();
	Object props;
	for (auto const& info : ctx->ass->Info) {
		props[std::string(info.Key())] = std::string(info.Value());
	}
	Object result;
	result["properties"] = std::move(props);
	return result;
}

/// 解码 Aegisub 路径令牌
UnknownElement HandleDecodePath(Object const& args) {
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	auto* ctx = agi::mcp::ActiveContext();
	std::string decoded;
	if (ctx)
		decoded = ctx->path->Decode(path).string();
	else
		decoded = path;
	Object result;
	result["decoded"] = decoded;
	return result;
}

/// 把绝对路径编码为 Aegisub 路径令牌形式
UnknownElement HandleEncodePath(Object const& args) {
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	auto* ctx = agi::mcp::ActiveContext();
	Object result;
	if (ctx)
		result["encoded"] = ctx->path->Encode(agi::fs::path(wxString::FromUTF8(path).ToStdWstring()));
	else
		result["encoded"] = path;
	return result;
}

/// ASS 时间字符串转毫秒
UnknownElement HandleTimeToMs(Object const& args) {
	std::string time_str = GetString(args, "time");
	if (time_str.empty())
		throw std::runtime_error("missing 'time'");
	// agi::Time 对非法字符静默跳过、不抛异常，这里先校验输入格式
	// 允许：纯毫秒数字、"1:23.45"、"0:01:23.45"（数字、冒号、点、可选负号）
	bool valid = !time_str.empty();
	int digits = 0;
	for (char ch : time_str) {
		if (ch >= '0' && ch <= '9') { ++digits; continue; }
		if (ch == ':' || ch == '.' || ch == '-') continue;
		valid = false;
		break;
	}
	if (!valid || digits == 0)
		throw std::runtime_error("invalid time format: " + time_str);
	Object result;
	result["ms"] = static_cast<int64_t>(agi::Time(time_str));
	return result;
}

/// 毫秒转 ASS 时间字符串
UnknownElement HandleMsToTime(Object const& args) {
	int64_t ms = GetInt(args, "ms", -1);
	if (ms < 0)
		throw std::runtime_error("missing or invalid 'ms'");
	if (ms > 2147483647)
		throw std::runtime_error("ms value out of range");
	Object result;
	result["time"] = agi::Time(static_cast<int>(ms)).GetAssFormatted();
	result["ms"] = ms;
	return result;
}

/// 读取系统剪贴板文本
UnknownElement HandleClipboardGet(Object const& /*args*/) {
	if (!wxTheClipboard->Open())
		throw std::runtime_error("failed to open clipboard");
	std::string text;
	if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT) || wxTheClipboard->IsSupported(wxDF_TEXT)) {
		wxTextDataObject data;
		if (wxTheClipboard->GetData(data))
			text = data.GetText().ToUTF8().data();
	}
	wxTheClipboard->Close();
	Object result;
	result["text"] = text;
	return result;
}

/// 写入系统剪贴板文本
UnknownElement HandleClipboardSet(Object const& args) {
	std::string text = GetString(args, "text");
	if (!wxTheClipboard->Open())
		throw std::runtime_error("failed to open clipboard");
	bool ok = wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text)));
	wxTheClipboard->Close();
	if (!ok)
		throw std::runtime_error("failed to write clipboard");
	Object result;
	result["ok"] = true;
	return result;
}

/// 加载 timecodes 文件
UnknownElement HandleLoadTimecodes(Object const& args) {
	auto* ctx = RequireContext();
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	agi::fs::path fs_path(wxString::FromUTF8(path).ToStdWstring());
	// Project::LoadTimecodes 内部吞掉解析异常，先自行验证以便向调用方反馈错误
	try {
		agi::vfr::Framerate tc(fs_path);
	} catch (agi::Exception const& e) {
		throw std::runtime_error("failed to parse timecodes file: " + e.GetMessage());
	}
	ctx->project->LoadTimecodes(fs_path);
	Object result;
	result["ok"] = true;
	result["is_loaded"] = ctx->project->Timecodes().IsLoaded();
	return result;
}

/// 加载关键帧文件
UnknownElement HandleLoadKeyframes(Object const& args) {
	auto* ctx = RequireContext();
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	agi::fs::path fs_path(wxString::FromUTF8(path).ToStdWstring());
	try {
		agi::keyframe::Load(fs_path);
	} catch (agi::Exception const& e) {
		throw std::runtime_error("failed to parse keyframes file: " + e.GetMessage());
	}
	ctx->project->LoadKeyframes(fs_path);
	Object result;
	result["ok"] = true;
	result["count"] = static_cast<int64_t>(ctx->project->Keyframes().size());
	return result;
}

/// 定位 VSFilterMod 标签文档，优先安装布局 ?data/data/，兼容 ?data/
agi::fs::path FindVsmodDocPath() {
	for (auto const& prefix : {"?data/data/", "?data/"}) {
		auto p = config::path->Decode(std::string(prefix) + "AssRocket-VSFilterMod-使用文档.md");
		if (agi::fs::FileExists(p))
			return p;
	}
	return {};
}

/// 从一行 `#### \tag...` 条目标题提取标签名（到 '('、'<'、'&'、空格或 '-' 为止，含反斜杠）
std::string ParseVsmodTagName(std::string const& heading) {
	auto pos = heading.find('\\');
	if (pos == std::string::npos) return {};
	auto end = heading.find_first_of("(<& -", pos);
	if (end == std::string::npos) end = heading.size();
	return heading.substr(pos, end - pos);
}

/// curl 写回调：把响应体追加到 std::string
size_t VsmodCurlWriteCb(char *contents, size_t size, size_t nmemb, void *userdata) {
	static_cast<std::string*>(userdata)->append(contents, size * nmemb);
	return size * nmemb;
}

/// 从 GitHub wiki 在线拉取 VSFilterMod 标签文档
std::string FetchVsmodDocOnline() {
	const char *url = "https://raw.githubusercontent.com/wiki/mojie126/Aegisub/AssRocket-VSFilterMod-%E4%BD%BF%E7%94%A8%E6%96%87%E6%A1%A3.md";

	CURL *curl = curl_easy_init();
	if (!curl)
		throw std::runtime_error("curl init failed");

	std::string result;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Aegisub-MCP");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, VsmodCurlWriteCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK)
		throw std::runtime_error("failed to fetch VSFilterMod doc from GitHub wiki: " + std::string(curl_easy_strerror(res)));
	return result;
}

/// 把文档文本拆成行
std::vector<std::string> VsmodDocToLines(std::string const& content) {
	std::vector<std::string> lines;
	std::string line;
	std::istringstream stream(content);
	while (std::getline(stream, line))
		lines.emplace_back(std::move(line));
	return lines;
}

/// 解析文档：有 tag 返回条目详情，无 tag 返回标签清单
Object ParseVsmodDoc(std::vector<std::string> const& lines, std::string const& tag) {
	if (!tag.empty()) {
		// 规范化输入：去掉反斜杠与参数部分（如 "\1vc(...)" 或 "1vc" 均匹配 \1vc）
		std::string want = tag;
		if (!want.empty() && want[0] == '\\') want.erase(0, 1);
		auto paren = want.find('(');
		if (paren != std::string::npos) want.erase(paren);
		if (want.empty())
			throw std::runtime_error("invalid tag: " + tag);

		// 逐条扫描 #### 条目，匹配标签名
		for (size_t i = 0; i < lines.size(); ++i) {
			if (lines[i].compare(0, 6, "#### `") != 0) continue;
			auto name = ParseVsmodTagName(lines[i]);
			if (name.empty()) continue;
			// 匹配标题首个标签名，或标题中出现的别名（如 "\K 或 \kf" 合并条目）
			bool matched = false;
			for (auto pos = lines[i].find('\\'); pos != std::string::npos; pos = lines[i].find('\\', pos + 1)) {
				auto end = lines[i].find_first_of("(<& -", pos);
				if (end == std::string::npos) end = lines[i].size();
				auto cand = lines[i].substr(pos + 1, end - pos - 1);
				if (cand == want) {
					matched = true;
					break;
				}
			}
			if (!matched) continue;

			// 收集条目内容直到下一个 #### / ### / ---
			std::string entry = lines[i].substr(6) + "\n";
			for (size_t j = i + 1; j < lines.size(); ++j) {
				auto const& l = lines[j];
				if (l.compare(0, 6, "#### `") == 0 || l.compare(0, 4, "### ") == 0 || l.compare(0, 3, "---") == 0)
					break;
				entry += l + "\n";
			}
			Object result;
			result["tag"] = name;
			result["syntax"] = entry;
			return result;
		}
		throw std::runtime_error("tag not found in VSFilterMod syntax doc: " + tag);
	}

	// 无 tag：返回全部标签条目清单（标签名 + 条目标题说明）
	Array tags;
	for (auto const& l : lines) {
		if (l.compare(0, 6, "#### `") != 0) continue;
		auto name = ParseVsmodTagName(l);
		if (name.empty()) continue;
		Object item;
		item["tag"] = name;
		item["title"] = l.substr(6);
		tags.emplace_back(std::move(item));
	}
	Object result;
	int64_t count = static_cast<int64_t>(tags.size());
	result["tags"] = std::move(tags);
	result["count"] = count;
	return result;
}

/// 查询 VSFilterMod 标签语法，source 支持 local/online/auto（本地无则在线）
UnknownElement HandleVsmodSyntax(Object const& args) {
	std::string source = GetString(args, "source");
	if (source.empty()) source = "auto";

	std::vector<std::string> lines;
	std::string used_source;

	if (source == "local" || source == "auto") {
		auto doc_path = FindVsmodDocPath();
		if (!doc_path.empty()) {
			std::string content;
			auto in = agi::io::Open(doc_path);
			content.assign(std::istreambuf_iterator<char>(*in), std::istreambuf_iterator<char>());
			lines = VsmodDocToLines(content);
			used_source = "local";
		}
		else if (source == "local") {
			throw std::runtime_error("VSFilterMod syntax doc not found, please install Aegisub data files or use source=online");
		}
	}

	if (lines.empty() && (source == "online" || source == "auto")) {
		lines = VsmodDocToLines(FetchVsmodDocOnline());
		used_source = "online";
	}

	if (lines.empty())
		throw std::runtime_error("no VSFilterMod syntax doc available");

	Object result = ParseVsmodDoc(lines, GetString(args, "tag"));
	result["source"] = used_source;
	return result;
}

/// 剥离 ASS override 标签，返回纯文本
UnknownElement HandleStripTags(Object const& args) {
	std::string text = GetString(args, "text");
	agi::util::tagless_find_helper helper;
	Object result;
	result["text"] = helper.strip_tags(text, 0);
	return result;
}

/// 统计字符数（可忽略空白/标点/块）
UnknownElement HandleCharacterCount(Object const& args) {
	std::string text = GetString(args, "text");
	int ignore = 0;
	if (GetBool(args, "ignore_whitespace", false)) ignore |= agi::IGNORE_WHITESPACE;
	if (GetBool(args, "ignore_punctuation", false)) ignore |= agi::IGNORE_PUNCTUATION;
	if (GetBool(args, "ignore_blocks", false)) ignore |= agi::IGNORE_BLOCKS;
	Object result;
	result["count"] = static_cast<int64_t>(agi::CharacterCount(text, ignore));
	result["max_line_length"] = static_cast<int64_t>(agi::MaxLineLength(text, ignore));
	return result;
}

/// 获取脚本分辨率
UnknownElement HandleGetScriptResolution(Object const& /*args*/) {
	auto* ctx = RequireContext();
	int w = 0, h = 0;
	ctx->ass->GetResolution(w, h);
	Object result;
	result["width"] = static_cast<int64_t>(w);
	result["height"] = static_cast<int64_t>(h);
	return result;
}

/// 设置脚本分辨率
UnknownElement HandleSetScriptResolution(Object const& args) {
	auto* ctx = RequireContext();
	int64_t w = GetInt(args, "width", -1);
	int64_t h = GetInt(args, "height", -1);
	if (w <= 0 || w > 32767 || h <= 0 || h > 32767)
		throw std::runtime_error("missing or invalid 'width'/'height' (1-32767)");
	ctx->ass->SetScriptInfo("PlayResX", std::to_string(w));
	ctx->ass->SetScriptInfo("PlayResY", std::to_string(h));
	auto undo = GetString(args, "undo_point");
	ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("set script resolution"), AssFile::COMMIT_SCRIPTINFO);
	Object result;
	result["ok"] = true;
	result["width"] = w;
	result["height"] = h;
	return result;
}

/// 设置字幕网格选中行（可指定活动行）
UnknownElement HandleSetSelection(Object const& args) {
	auto* ctx = RequireContext();
	auto it = args.find("line_indices");
	if (it == args.end())
		throw std::runtime_error("missing 'line_indices'");
	Array const* indices = nullptr;
	try {
		indices = &static_cast<Array const&>(it->second);
	} catch (...) {
		throw std::runtime_error("'line_indices' must be an array");
	}

	Selection selection;
	AssDialogue* active = nullptr;
	int line_num = 0;
	for (auto& diag : ctx->ass->Events) {
		++line_num;
		for (auto const& idx_val : *indices) {
			try {
				if (static_cast<int64_t const&>(idx_val) == line_num) {
					selection.insert(&diag);
					break;
				}
			} catch (...) {}
		}
	}
	if (selection.empty())
		throw std::runtime_error("no matching lines for the given indices");

	// 活动行：优先取参数，缺省取选中的第一行
	int64_t active_idx = GetInt(args, "active_line", -1);
	if (active_idx > 0) {
		int n = 0;
		for (auto& diag : ctx->ass->Events) {
			++n;
			if (n == active_idx) {
				active = &diag;
				break;
			}
		}
		if (!active) throw std::runtime_error("active_line index out of range");
	} else {
		active = *selection.begin();
	}

	size_t count = selection.size();
	ctx->selectionController->SetSelectionAndActive(std::move(selection), active);
	Object result;
	result["ok"] = true;
	result["count"] = static_cast<int64_t>(count);
	return result;
}

/// 播放音频区间
UnknownElement HandlePlayAudio(Object const& args) {
	auto* ctx = RequireContext();
	if (!ctx->project->AudioProvider() || !ctx->audioController)
		throw std::runtime_error("no audio loaded");
	int64_t start_ms = GetInt(args, "start_ms", -1);
	int64_t end_ms = GetInt(args, "end_ms", -1);
	if (start_ms < 0)
		throw std::runtime_error("missing 'start_ms'");
	int64_t total_ms = ctx->project->AudioProvider()->GetNumSamples() * 1000 / std::max(1, ctx->project->AudioProvider()->GetSampleRate());
	if (start_ms > total_ms)
		throw std::runtime_error("'start_ms' beyond audio duration (" + std::to_string(total_ms) + " ms)");
	if (end_ms < 0 || end_ms > total_ms) end_ms = total_ms;
	if (end_ms <= start_ms)
		throw std::runtime_error("'end_ms' must be greater than 'start_ms'");
	ctx->audioController->PlayRange(TimeRange(static_cast<int>(start_ms), static_cast<int>(end_ms)));
	Object result;
	result["ok"] = true;
	result["start_ms"] = start_ms;
	result["end_ms"] = end_ms;
	return result;
}

/// 获取当前帧号
UnknownElement HandleGetFrame(Object const& /*args*/) {
	auto* ctx = RequireContext();
	if (!ctx->project->Timecodes().IsLoaded())
		throw std::runtime_error("no timecodes loaded");
	if (!ctx->videoController)
		throw std::runtime_error("no video controller available");
	Object result;
	result["frame"] = static_cast<int64_t>(ctx->videoController->GetFrameN());
	return result;
}

/// 打开字幕文件
UnknownElement HandleOpenFile(Object const& args) {
	auto* ctx = RequireContext();
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	agi::fs::path fs_path(wxString::FromUTF8(path).ToStdWstring());

	// 以下格式在读取时会弹出模态对话框（FPS 输入/文本导入选项），MCP 无法交互，直接拒绝
	auto lower = boost::to_lower_copy(fs_path.string());
	if (boost::iends_with(lower, ".txt"))
		throw std::runtime_error("plain text (.txt) import requires an interactive dialog; convert the file to .ass first");
	if (boost::iends_with(lower, ".sub"))
		throw std::runtime_error("MicroDVD (.sub) import requires an interactive FPS prompt; convert the file to .ass first");

	// 编码检测：无法确定时回退 UTF-8，不弹选择框
	std::string encoding = agi::charset::Detect(fs_path);
	if (encoding.empty())
		encoding = "UTF-8";
	ctx->subsController->Load(fs_path, encoding.c_str());

	// 与 GUI 的 DoLoadSubtitles 一致：加载后更新选中/活动行，
	// 否则 SubsEditBox 等组件的 line 缓存仍指向被 Load 内部 swap+析构释放的旧行
	// （AssFile 析构 clear_and_dispose 会 delete 行对象），后续 Commit 信号链读取悬空指针崩溃
	Selection sel;
	AssDialogue* active_line = nullptr;
	if (!ctx->ass->Events.empty()) {
		active_line = &*ctx->ass->Events.begin();
		sel.insert(active_line);
	}
	ctx->selectionController->SetSelectionAndActive(std::move(sel), active_line);

	Object result;
	result["ok"] = true;
	result["filename"] = ctx->subsController->Filename().string();
	return result;
}

/// 保存字幕文件
UnknownElement HandleSaveFile(Object const& args) {
	auto* ctx = RequireContext();
	std::string path = GetString(args, "path");
	if (path.empty())
		path = ctx->subsController->Filename().string();
	if (path.empty())
		throw std::runtime_error("no filename to save to; specify 'path'");
	agi::fs::path fs_path(wxString::FromUTF8(path).ToStdWstring());

	// 以下格式写入时会弹出模态 FPS 输入框，MCP 无法交互，直接拒绝
	auto lower = boost::to_lower_copy(fs_path.string());
	if (boost::iends_with(lower, ".sub") || boost::iends_with(lower, ".encore.txt") || boost::iends_with(lower, ".transtation.txt"))
		throw std::runtime_error("this format requires an interactive FPS prompt on save; save as .ass or .srt instead");

	ctx->subsController->Save(fs_path);
	Object result;
	result["ok"] = true;
	result["filename"] = path;
	return result;
}

/// 新建字幕文件
UnknownElement HandleNewFile(Object const& /*args*/) {
	auto* ctx = RequireContext();
	// 先卸载关联文件，避免 CloseSubtitles 内部按 "Load Linked Files" 选项弹确认框
	ctx->project->CloseVideo();
	ctx->project->CloseAudio();
	ctx->project->CloseTimecodes();
	ctx->project->CloseKeyframes();
	ctx->project->CloseSubtitles();
	Object result;
	result["ok"] = true;
	result["filename"] = ctx->subsController->Filename().string();
	return result;
}

/// 打开视频文件
UnknownElement HandleOpenVideo(Object const& args) {
	auto* ctx = RequireContext();
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	// 非交互模式：加载失败不弹 provider 选择对话框，异常直接抛出；
	// 分辨率不匹配时不弹对话框、不自动修改字幕尺寸
	ctx->project->LoadVideo(agi::fs::path(wxString::FromUTF8(path).ToStdWstring()), false);
	if (!ctx->project->VideoProvider())
		throw std::runtime_error("failed to load video: no video provider created");
	auto* provider = ctx->project->VideoProvider();
	Object result;
	result["ok"] = true;
	result["filename"] = ctx->project->VideoName().string();
	// 返回视频尺寸与脚本分辨率，便于调用方判断是否尺寸不匹配
	result["video_width"] = static_cast<int64_t>(provider->GetWidth());
	result["video_height"] = static_cast<int64_t>(provider->GetHeight());
	int sx = 0, sy = 0;
	ctx->ass->GetResolution(sx, sy);
	result["script_width"] = static_cast<int64_t>(sx);
	result["script_height"] = static_cast<int64_t>(sy);
	result["resolution_mismatch"] = (sx > 0 && sy > 0) && (sx != provider->GetWidth() || sy != provider->GetHeight());
	return result;
}

/// 打开音频文件
UnknownElement HandleOpenAudio(Object const& args) {
	auto* ctx = RequireContext();
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	ctx->project->LoadAudio(agi::fs::path(wxString::FromUTF8(path).ToStdWstring()), false);
	if (!ctx->project->AudioProvider())
		throw std::runtime_error("failed to load audio: no audio provider created");
	Object result;
	result["ok"] = true;
	result["filename"] = ctx->project->AudioName().string();
	return result;
}

/// 关闭视频
UnknownElement HandleCloseVideo(Object const& /*args*/) {
	auto* ctx = RequireContext();
	ctx->project->CloseVideo();
	Object result;
	result["ok"] = true;
	return result;
}

/// 关闭音频
UnknownElement HandleCloseAudio(Object const& /*args*/) {
	auto* ctx = RequireContext();
	ctx->project->CloseAudio();
	Object result;
	result["ok"] = true;
	return result;
}

/// 获取指定帧的截图，以 PNG base64 图片返回（同时附带帧信息文本）
UnknownElement HandleGetVideoFrame(Object const& args) {
	auto* ctx = RequireContext();
	if (!ctx->project->VideoProvider() || !ctx->videoController)
		throw std::runtime_error("no video loaded");
	bool raw = GetBool(args, "raw", false);
	int64_t frame = ::ResolveTargetFrame(ctx, args);
	wxImage img = ::GetVideoFrameImage(ctx, frame, raw);
	if (!img.IsOk())
		throw std::runtime_error("failed to decode video frame");

	// 编码为 PNG 并 base64
	wxMemoryOutputStream os;
	if (!img.SaveFile(os, wxBITMAP_TYPE_PNG))
		throw std::runtime_error("failed to encode frame as PNG");
	auto* stream_buf = os.GetOutputStreamBuffer();
	std::string b64 = ::Base64Encode(stream_buf->GetBufferStart(), os.GetSize());

	int64_t ms = ctx->videoController->TimeAtFrame(static_cast<int>(frame), agi::vfr::START);
	Object frame_info;
	frame_info["type"] = std::string("text");
	frame_info["text"] = "frame=" + std::to_string(frame) + " ms=" + std::to_string(ms)
		+ " " + std::to_string(img.GetWidth()) + "x" + std::to_string(img.GetHeight())
		+ " raw=" + (raw ? "true" : "false");
	Object image_item;
	image_item["type"] = std::string("image");
	image_item["data"] = b64;
	image_item["mimeType"] = std::string("image/png");
	Array content;
	content.emplace_back(std::move(frame_info));
	content.emplace_back(std::move(image_item));

	Object result;
	result["frame"] = frame;
	result["ms"] = ms;
	result["width"] = static_cast<int64_t>(img.GetWidth());
	result["height"] = static_cast<int64_t>(img.GetHeight());
	result["total_frames"] = static_cast<int64_t>(ctx->project->VideoProvider()->GetFrameCount());
	result["content"] = std::move(content);
	return result;
}

/// 保存指定帧的截图到文件
UnknownElement HandleSaveVideoFrame(Object const& args) {
	auto* ctx = RequireContext();
	if (!ctx->project->VideoProvider() || !ctx->videoController)
		throw std::runtime_error("no video loaded");
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");
	bool raw = GetBool(args, "raw", false);
	int64_t frame = ::ResolveTargetFrame(ctx, args);
	wxImage img = ::GetVideoFrameImage(ctx, frame, raw);
	if (!img.IsOk())
		throw std::runtime_error("failed to decode video frame");

	std::string format = GetString(args, "format");
	if (format.empty()) format = "png";
	wxBitmapType type;
	if (format == "jpeg" || format == "jpg") {
		if (!wxImage::FindHandler(wxBITMAP_TYPE_JPEG))
			wxImage::AddHandler(new wxJPEGHandler);
		type = wxBITMAP_TYPE_JPEG;
	} else {
		if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG))
			wxImage::AddHandler(new wxPNGHandler);
		type = wxBITMAP_TYPE_PNG;
	}
	agi::fs::path out_path(wxString::FromUTF8(path).ToStdWstring());
	if (!img.SaveFile(wxString(out_path.wstring()), type))
		throw std::runtime_error("failed to save frame image to file");

	int64_t ms = ctx->videoController->TimeAtFrame(static_cast<int>(frame), agi::vfr::START);
	Object result;
	result["ok"] = true;
	result["path"] = path;
	result["frame"] = frame;
	result["ms"] = ms;
	result["width"] = static_cast<int64_t>(img.GetWidth());
	result["height"] = static_cast<int64_t>(img.GetHeight());
	return result;
}

/// 导出视频区间为 GIF（无对话框版本，直接写入指定文件）
UnknownElement HandleExportGif(Object const& args) {
	auto* ctx = RequireContext();
	if (!ctx->project->VideoProvider() || !ctx->videoController)
		throw std::runtime_error("no video loaded");
	std::string path = GetString(args, "path");
	if (path.empty())
		throw std::runtime_error("missing 'path'");

	int64_t start_frame = GetInt(args, "start_frame", -1);
	int64_t end_frame = GetInt(args, "end_frame", -1);
	if (start_frame < 0 || end_frame < 0)
		throw std::runtime_error("missing 'start_frame'/'end_frame'");
	int64_t quality = GetInt(args, "quality", 100);
	int64_t scale_factor = GetInt(args, "scale_factor", 1);

	auto* provider = ctx->project->VideoProvider();
	int total = provider->GetFrameCount();
	start_frame = std::clamp<int64_t>(start_frame, 0, total - 1);
	end_frame = std::clamp<int64_t>(end_frame, 0, total - 1);
	if (end_frame <= start_frame)
		throw std::runtime_error("'end_frame' must be greater than 'start_frame'");
	const int start = static_cast<int>(start_frame);
	const int end = static_cast<int>(end_frame);

	const auto frame_pts = agi::BuildGifFramePresentationTimestamps(ctx->project->Timecodes(), start, end);
	const int total_frame = end - start + 1;
	if (frame_pts.size() != static_cast<size_t>(total_frame))
		throw std::runtime_error("failed to build GIF frame timestamps");

	// 首帧解码以确定输出尺寸（与 GUI 导出路径一致）
	auto first_vf = provider->GetFrame(start, ctx->project->Timecodes().TimeAtFrame(start), false);
	wxImage first_img = GetImage(*first_vf);
	if (!first_img.IsOk() || !first_img.GetData())
		throw std::runtime_error("failed to decode first frame for GIF export");
	const bool gif_hdr_enabled = OPT_GET("Video/HDR/Tone Mapping")->GetBool();
	if (gif_hdr_enabled)
		VideoOutGL::ApplyHDRLutToImage(first_img, provider->GetHDRType(), provider->GetDVProfile());
	const int gif_padding_top = first_vf->padding_top;
	const int gif_padding_bottom = first_vf->padding_bottom;
	if (gif_padding_top > 0 || gif_padding_bottom > 0)
		first_img = AddPaddingToImage(first_img, gif_padding_top, gif_padding_bottom);

	const int source_width = first_img.GetWidth();
	const int source_height = first_img.GetHeight();

	// 初始化 gifski（输出尺寸按 scale_factor 等比缩小）
	GifskiSettings settings;
	settings.quality = static_cast<uint8_t>(std::clamp<int64_t>(quality, 1, 100));
	settings.width = std::max(1, (scale_factor > 1) ? (source_width / static_cast<int>(scale_factor)) : source_width);
	settings.height = std::max(1, (scale_factor > 1) ? (source_height / static_cast<int>(scale_factor)) : source_height);
	settings.fast = false;
	settings.repeat = 0;
	gifski* g = gifski_new(&settings);
	if (!g)
		throw std::runtime_error("failed to initialize gifski");
	GifskiError error = {};

	// RAII 清理：任何异常路径（含解码抛异常）都释放 gifski 与输出文件
	struct GifskiGuard {
		gifski* g = nullptr;
		FILE* f = nullptr;
		~GifskiGuard() {
			if (g) gifski_finish(g);
			if (f) fclose(f);
		}
	} guard;
	guard.g = g;

	// 确保父目录存在并打开输出文件
	{
		agi::fs::path out_path(wxString::FromUTF8(path).ToStdWstring());
		agi::fs::path parent_dir = out_path.parent_path();
		if (!parent_dir.empty()) {
			std::error_code ec;
			std::filesystem::create_directories(parent_dir, ec);
			if (ec)
				throw std::runtime_error("failed to create output directory: " + ec.message());
		}
	}
	FILE* output_file = _wfopen(wxString::FromUTF8(path).wc_str(), L"wb");
	if (!output_file)
		throw std::runtime_error("failed to open output file for writing");
	guard.f = output_file;

	auto gifski_write_cb = [](size_t buf_len, const uint8_t* buf, void* user_data) -> int {
		FILE* f = static_cast<FILE*>(user_data);
		if (buf_len == 0) {
			fflush(f);
			return GIFSKI_OK;
		}
		return fwrite(buf, 1, buf_len, f) == buf_len ? GIFSKI_OK : 1;
	};
	error = gifski_set_write_callback(g, gifski_write_cb, output_file);
	if (error != GIFSKI_OK)
		throw std::runtime_error("failed to set gifski write callback");

	// 逐帧喂给 gifski
	uint32_t current_frame = 0;
	for (int i = start; i <= end; ++i) {
		const wxImage* img = nullptr;
		wxImage decoded_img;
		if (i == start) {
			img = &first_img;
		} else {
			decoded_img = GetImage(*provider->GetFrame(i, ctx->project->Timecodes().TimeAtFrame(i), false));
			if (gif_hdr_enabled)
				VideoOutGL::ApplyHDRLutToImage(decoded_img, provider->GetHDRType(), provider->GetDVProfile());
			if (gif_padding_top > 0 || gif_padding_bottom > 0)
				decoded_img = AddPaddingToImage(decoded_img, gif_padding_top, gif_padding_bottom);
			img = &decoded_img;
		}
		if (!img->IsOk() || !img->GetData() || img->GetWidth() != source_width || img->GetHeight() != source_height)
			throw std::runtime_error("failed to decode frame " + std::to_string(i) + " for GIF export");

		// BGRA 像素（无裁剪，全帧导出）
		const size_t pixel_count = static_cast<size_t>(source_width) * static_cast<size_t>(source_height);
		std::vector<uint8_t> pixels(pixel_count * 4);
		const unsigned char* imgData = img->GetData();
		for (size_t p = 0; p < pixel_count; ++p) {
			pixels[p * 4 + 0] = imgData[p * 3 + 0];
			pixels[p * 4 + 1] = imgData[p * 3 + 1];
			pixels[p * 4 + 2] = imgData[p * 3 + 2];
			pixels[p * 4 + 3] = 255;
		}
		error = gifski_add_frame_rgba(g, current_frame, source_width, source_height, pixels.data(), frame_pts[current_frame]);
		if (error != GIFSKI_OK)
			throw std::runtime_error("failed to add frame to GIF");
		++current_frame;
	}

	error = gifski_finish(g);
	guard.g = nullptr; // finish 已释放句柄，防止 RAII 重复释放
	fclose(output_file);
	guard.f = nullptr;
	if (error != GIFSKI_OK)
		throw std::runtime_error("failed to finish GIF export");

	Object result;
	result["ok"] = true;
	result["path"] = path;
	result["start_frame"] = start_frame;
	result["end_frame"] = end_frame;
	result["frames"] = static_cast<int64_t>(total_frame);
	result["width"] = static_cast<int64_t>(source_width);
	result["height"] = static_cast<int64_t>(source_height);
	return result;
}

/// 读取音频波形聚合数据（峰值/均值）
UnknownElement HandleGetAudioWaveform(Object const& args) {
	auto* ctx = RequireContext();
	auto* provider = ctx->project->AudioProvider();
	if (!provider)
		throw std::runtime_error("no audio loaded");

	int64_t total_samples = provider->GetNumSamples();
	int64_t total_ms = provider->GetSampleRate() > 0 ? total_samples * 1000 / provider->GetSampleRate() : 0;
	int64_t start_ms = std::clamp<int64_t>(GetInt(args, "start_ms", 0), 0, total_ms);
	int64_t end_ms = GetInt(args, "end_ms", -1);
	if (end_ms < 0) end_ms = total_ms;
	end_ms = std::clamp<int64_t>(end_ms, start_ms, total_ms);
	int64_t points = std::clamp<int64_t>(GetInt(args, "points", 256), 1, 4096);

	int64_t start_sample = start_ms * provider->GetSampleRate() / 1000;
	int64_t end_sample = end_ms * provider->GetSampleRate() / 1000;
	auto pts = agi::ComputeWaveform(*provider, start_sample, end_sample, static_cast<size_t>(points));

	Array data;
	data.reserve(pts.size());
	for (auto const& p : pts) {
		Object pt;
		pt["peak_min"] = static_cast<int64_t>(p.peak_min);
		pt["peak_max"] = static_cast<int64_t>(p.peak_max);
		pt["avg_min"] = p.avg_min;
		pt["avg_max"] = p.avg_max;
		data.emplace_back(std::move(pt));
	}

	Object result;
	result["has_audio"] = true;
	result["sample_rate"] = static_cast<int64_t>(provider->GetSampleRate());
	result["start_ms"] = start_ms;
	result["end_ms"] = end_ms;
	result["points"] = static_cast<int64_t>(data.size());
	result["data"] = std::move(data);
	return result;
}

/// 读取指定时刻的音频频谱功率数据（低频在前，范围 0~1）
UnknownElement HandleGetAudioSpectrum(Object const& args) {
	auto* ctx = RequireContext();
	auto* provider = ctx->project->AudioProvider();
	if (!provider)
		throw std::runtime_error("no audio loaded");

	int64_t total_samples = provider->GetNumSamples();
	int64_t total_ms = provider->GetSampleRate() > 0 ? total_samples * 1000 / provider->GetSampleRate() : 0;
	int64_t ms = std::clamp<int64_t>(GetInt(args, "ms", 0), 0, total_ms);
	int64_t fft_size = GetInt(args, "fft_size", 1024);
	fft_size = std::clamp<int64_t>(fft_size, 256, 32768);

	int64_t start_sample = ms * provider->GetSampleRate() / 1000;
	auto powers = agi::ComputeSpectrum(*provider, start_sample, static_cast<size_t>(fft_size));

	Array data;
	data.reserve(powers.size());
	for (float v : powers)
		data.emplace_back(static_cast<double>(v));

	Object result;
	result["has_audio"] = true;
	result["sample_rate"] = static_cast<int64_t>(provider->GetSampleRate());
	result["ms"] = ms;
	result["fft_size"] = fft_size;
	result["data"] = std::move(data);
	return result;
}

/// 设置撤销点（仅占位，commit 时自动入栈）
UnknownElement HandleSetUndoPoint(Object const& args) {
	auto* ctx = RequireContext();
	auto desc = GetString(args, "description");
	if (desc.empty()) desc = "MCP edit";
	// 调用一次空 Commit 来标记 undo 点
	ctx->ass->Commit(wxString::FromUTF8(desc), AssFile::COMMIT_DIAG_TEXT);
	Object result;
	result["ok"] = true;
	return result;
}

} // anonymous namespace

namespace agi::mcp {

static std::vector<FrameMain*>* g_frames = nullptr;

void SetFrames(std::vector<FrameMain*>* frames) {
	g_frames = frames;
}

agi::Context* ActiveContext() {
	if (!g_frames || g_frames->empty()) return nullptr;
	return g_frames->front()->GetContext();
}

void RegisterMcpTools() {
	{
		::agi::mcp::RegisterTool(
			"get_project_info",
			"Query the current Aegisub project: filename, modified flag, video/audio state, line and style counts",
			::MakeObjectSchema(Object{}),
			::HandleGetProjectInfo);
	}

	{
		::agi::mcp::RegisterTool(
			"list_commands",
			"List all valid Aegisub command names that run_command can execute. "
			"Each entry has: name; blocking (true = opens a modal dialog/file picker and waits for user interaction, "
			"the call will hang until the user closes it); tool (the dedicated MCP tool name that replaces this command, "
			"prefer it over run_command when present). "
			"Call this before run_command when unsure of the exact command name.",
			::MakeObjectSchema(Object{}),
			::HandleListCommands);
	}

	{
		Object props;
		props["command"] = ::MakeStringProp("Aegisub command name, e.g. 'time/shift'. "
			"Use list_commands to query all valid names first.");
		Array required;
		required.emplace_back(std::string("command"));
		::agi::mcp::RegisterTool(
			"run_command",
			"Execute a registered Aegisub command by name (equivalent to clicking the menu item). "
			"Call list_commands first to get the exact command names.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleRunCommand);
	}

	{
		Object props;
		props["limit"] = ::MakeIntProp("Maximum number of lines to return (0 = all)", 0);
		props["offset"] = ::MakeIntProp("Skip this many lines from the start", 0);
		props["include_comment"] = ::MakeBoolProp("Include comment lines in results", false);
		::agi::mcp::RegisterTool(
			"get_dialogue_lines",
			"Read dialogue/comment lines from the current subtitle file. Returns line index, text, style, timing, etc.",
			::MakeObjectSchema(std::move(props)),
			::HandleGetDialogueLines);
	}

	{
		::agi::mcp::RegisterTool(
			"get_styles",
			"List all styles defined in the current subtitle file with complete style fields (colors, outline, shadow, margins, etc.)",
			::MakeObjectSchema(Object{}),
			::HandleGetStyles);
	}

	{
		Object props;
		props["name"] = ::MakeStringProp("Style name to fetch (case-insensitive)");
		Array required;
		required.emplace_back(std::string("name"));
		::agi::mcp::RegisterTool(
			"get_style",
			"Get a single style's complete definition by name.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleGetStyle);
	}

	{
		Object props;
		{
			Object field_prop;
			field_prop["type"] = std::string("object");
			field_prop["description"] = std::string(
				"Style fields. name (required): unique style name; font: font face; fontsize; "
				"primary/secondary/outline/shadow: ASS colors like '&H00FFFFFF'; "
				"bold/italic/underline/strikeout: bool; scalex/scaley/spacing/angle/outline_w/shadow_w: numbers; "
				"borderstyle/alignment/margin_l/margin_r/margin_v/encoding: ints");
			props["fields"] = std::move(field_prop);
		}
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		Array required;
		required.emplace_back(std::string("fields"));
		::agi::mcp::RegisterTool(
			"add_style",
			"Add a new style to the current subtitle file. 'fields.name' is required and must be unique (case-insensitive).",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleAddStyle);
	}

	{
		Object props;
		props["name"] = ::MakeStringProp("Name of the style to update (case-insensitive)");
		{
			Object field_prop;
			field_prop["type"] = std::string("object");
			field_prop["description"] = std::string(
				"Style fields to change. 'name' inside fields renames the style. "
				"Colors accept ASS format like '&H00FFFFFF' or '#RRGGBB'.");
			props["fields"] = std::move(field_prop);
		}
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		Array required;
		required.emplace_back(std::string("name"));
		required.emplace_back(std::string("fields"));
		::agi::mcp::RegisterTool(
			"update_style",
			"Update fields of an existing style: font, fontsize, colors, bold/italic, outline, shadow, alignment, margins, etc.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleUpdateStyle);
	}

	{
		Object props;
		props["name"] = ::MakeStringProp("Name of the style to delete (case-insensitive)");
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		Array required;
		required.emplace_back(std::string("name"));
		::agi::mcp::RegisterTool(
			"delete_style",
			"Delete a style by name. Lines referencing the deleted style are reassigned to 'Default'.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleDeleteStyle);
	}

	{
		Object props;
		props["pattern"] = ::MakeStringProp("Text pattern to search for");
		props["regex"] = ::MakeBoolProp("Use regex matching", false);
		Array required;
		required.emplace_back(std::string("pattern"));
		::agi::mcp::RegisterTool(
			"search_dialogue",
			"Search dialogue lines for text matching a pattern. Returns matching lines with their indices.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleSearchDialogue);
	}

	{
		Object props;
		props["line_index"] = ::MakeIntArrayProp("1-based indices of lines to update");
		{
			Object field_prop;
			field_prop["type"] = std::string("object");
			field_prop["description"] = std::string("Fields to update: text, style, actor, effect, layer, start, end, comment, margin_l/r/v");
			props["fields"] = std::move(field_prop);
		}
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		Array required;
		required.emplace_back(std::string("line_index"));
		required.emplace_back(std::string("fields"));
		::agi::mcp::RegisterTool(
			"update_subtitle_fields",
			"Update fields on specific subtitle lines by index. Supports: text, style, actor, effect, layer, start/end (ms), comment, margins.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleUpdateSubtitleFields);
	}

	{
		Object props;
		{
			Object field_prop;
			field_prop["type"] = std::string("object");
			field_prop["description"] = std::string("Line fields: text, style, actor, effect, layer, start, end, comment");
			props["fields"] = std::move(field_prop);
		}
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		Array required;
		required.emplace_back(std::string("fields"));
		::agi::mcp::RegisterTool(
			"insert_subtitle_line",
			"Insert a new subtitle line at the end of the events list. Returns the new line index.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleInsertSubtitleLine);
	}

	{
		Object props;
		props["line_index"] = ::MakeIntArrayProp("1-based indices of lines to delete");
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		Array required;
		required.emplace_back(std::string("line_index"));
		::agi::mcp::RegisterTool(
			"delete_subtitle_line",
			"Delete subtitle lines by index. Multiple indices can be specified.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleDeleteSubtitleLine);
	}

	{
		Object props;
		props["offset"] = ::MakeIntProp("Offset in ms to apply to both start and end of selected lines (mutually exclusive with start_offset/end_offset)");
		props["start_offset"] = ::MakeIntProp("Offset in ms to apply to start time only");
		props["end_offset"] = ::MakeIntProp("Offset in ms to apply to end time only");
		props["line_indices"] = ::MakeIntArrayProp("Optional: only shift these specific lines (by 1-based index). If omitted, shift all lines.");
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		::agi::mcp::RegisterTool(
			"shift_times",
			"Shift start/end times of subtitle lines by the given offset in milliseconds. "
			"Use for timing alignment: positive offsets delay, negative offsets advance.",
			::MakeObjectSchema(std::move(props)),
			::HandleShiftTimes);
	}

	{
		Object props;
		props["description"] = ::MakeStringProp("Undo point description", "MCP edit");
		::agi::mcp::RegisterTool(
			"set_undo_point",
			"Mark an undo point in the current editing session. Call this after a batch of changes to create a single undo step.",
			::MakeObjectSchema(std::move(props)),
			::HandleSetUndoPoint);
	}

	{
		::agi::mcp::RegisterTool(
			"get_audio_selection",
			"Get the current audio selection range (start/end/duration in ms). Returns has_audio=false if no audio loaded.",
			::MakeObjectSchema(Object{}),
			::HandleGetAudioSelection);
	}

	{
		::agi::mcp::RegisterTool(
			"undo",
			"Undo the last editing operation. Returns ok=true on success.",
			::MakeObjectSchema(Object{}),
			::HandleUndo);
	}

	{
		::agi::mcp::RegisterTool(
			"redo",
			"Redo the last undone editing operation. Returns ok=true on success.",
			::MakeObjectSchema(Object{}),
			::HandleRedo);
	}

	{
		::agi::mcp::RegisterTool(
			"get_selected_lines",
			"Get the currently selected subtitle lines in the grid. Returns indices and full line data.",
			::MakeObjectSchema(Object{}),
			::HandleGetSelectedLines);
	}

	{
		Object props;
		props["ms"] = ::MakeIntProp("Time in milliseconds", 0);
		Array required;
		required.emplace_back(std::string("ms"));
		::agi::mcp::RegisterTool(
			"frame_from_ms",
			"Convert milliseconds to frame number using the loaded video timecodes",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleFrameFromMs);
	}

	{
		Object props;
		props["frame"] = ::MakeIntProp("Frame number", 0);
		Array required;
		required.emplace_back(std::string("frame"));
		::agi::mcp::RegisterTool(
			"ms_from_frame",
			"Convert frame number to milliseconds using the loaded video timecodes",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleMsFromFrame);
	}

	{
		::agi::mcp::RegisterTool(
			"video_size",
			"Get the video dimensions (width, height, aspect ratio). Returns has_video=false if no video loaded.",
			::MakeObjectSchema(Object{}),
			::HandleVideoSize);
	}

	{
		::agi::mcp::RegisterTool(
			"keyframes",
			"Get the list of keyframe frame numbers from the loaded video. Returns empty array if no keyframes available.",
			::MakeObjectSchema(Object{}),
			::HandleKeyframes);
	}

	{
		::agi::mcp::RegisterTool(
			"project_properties",
			"Get the Script Info section of the current subtitle file as key-value pairs",
			::MakeObjectSchema(Object{}),
			::HandleProjectProperties);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Aegisub path token to decode, e.g. '?user' or '?script'");
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"decode_path",
			"Decode an Aegisub path token (e.g. ?user, ?script, ?data) to an absolute filesystem path",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleDecodePath);
	}

	{
		::agi::mcp::RegisterTool(
			"get_frame",
			"Get the current video frame number. Returns error if no video timecodes loaded.",
			::MakeObjectSchema(Object{}),
			::HandleGetFrame);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Absolute path to encode, e.g. '?user/config.json' style token path");
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"encode_path",
			"Encode an absolute filesystem path to an Aegisub path token (?user, ?script, ?video, ...). "
			"Pair of decode_path.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleEncodePath);
	}

	{
		Object props;
		props["time"] = ::MakeStringProp("ASS time string, e.g. '0:01:23.45' or '1:23.45'");
		Array required;
		required.emplace_back(std::string("time"));
		::agi::mcp::RegisterTool(
			"time_to_ms",
			"Convert an ASS/SRT time string to milliseconds.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleTimeToMs);
	}

	{
		Object props;
		props["ms"] = ::MakeIntProp("Time in milliseconds", 0);
		Array required;
		required.emplace_back(std::string("ms"));
		::agi::mcp::RegisterTool(
			"ms_to_time",
			"Convert milliseconds to an ASS time string like '0:01:23.45'.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleMsToTime);
	}

	{
		::agi::mcp::RegisterTool(
			"clipboard_get",
			"Read the current system clipboard text.",
			::MakeObjectSchema(Object{}),
			::HandleClipboardGet);
	}

	{
		Object props;
		props["text"] = ::MakeStringProp("Text to write to the clipboard");
		Array required;
		required.emplace_back(std::string("text"));
		::agi::mcp::RegisterTool(
			"clipboard_set",
			"Write text to the system clipboard.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleClipboardSet);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Absolute path to the timecodes file (v1/v2)");
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"load_timecodes",
			"Load a timecodes file into the current project, overriding the video framerate.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleLoadTimecodes);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Absolute path to the keyframes file (x264 log, aegi, etc.)");
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"load_keyframes",
			"Load a keyframes file into the current project.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleLoadKeyframes);
	}

	{
		Object props;
		props["tag"] = ::MakeStringProp(
			"VSFilterMod tag name to look up, e.g. '\\1vc', '\\blur', '\\z' or '\\1img'. "
			"Omit to list all available tags.");
		props["source"] = ::MakeStringProp(
			"Where to read the syntax reference from: 'local' (bundled doc), "
			"'online' (fetch GitHub wiki), 'auto' (local first, fall back to online). Default: auto",
			"auto");
		::agi::mcp::RegisterTool(
			"vsmod_syntax",
			"Look up VSFilterMod (VSMod) subtitle override tag syntax and effect from the built-in reference "
			"or the online GitHub wiki (https://github.com/mojie126/Aegisub/wiki). "
			"Pass tag to get its usage, parameters, example, effect and conflict notes; "
			"omit tag to list every documented tag. "
			"VSMod tags are extensions beyond standard ASS, e.g. gradient colors \\1vc, PNG images \\1img, "
			"3D transforms \\z/\\ortho, distortion \\distort, blur effects, jitter and Lua scripting. "
			"Returns source ('local' or 'online') indicating where the info came from.",
			::MakeObjectSchema(std::move(props)),
			::HandleVsmodSyntax);
	}

	{
		Object props;
		props["text"] = ::MakeStringProp("ASS dialogue text, possibly containing override tags like {\\an8\\pos(...)}");
		Array required;
		required.emplace_back(std::string("text"));
		::agi::mcp::RegisterTool(
			"strip_tags",
			"Remove ASS override tags from dialogue text and return the plain visible text.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleStripTags);
	}

	{
		Object props;
		props["text"] = ::MakeStringProp("Text to count characters in (override tags included if present)");
		props["ignore_whitespace"] = ::MakeBoolProp("Ignore whitespace", false);
		props["ignore_punctuation"] = ::MakeBoolProp("Ignore punctuation", false);
		props["ignore_blocks"] = ::MakeBoolProp("Ignore non-Latin blocks", false);
		Array required;
		required.emplace_back(std::string("text"));
		::agi::mcp::RegisterTool(
			"character_count",
			"Count characters and the longest line length in a string, with optional ignore flags.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleCharacterCount);
	}

	{
		::agi::mcp::RegisterTool(
			"get_script_resolution",
			"Get the script resolution (PlayResX/PlayResY) for coordinate calculations.",
			::MakeObjectSchema(Object{}),
			::HandleGetScriptResolution);
	}

	{
		Object props;
		props["width"] = ::MakeIntProp("New script width (PlayResX)", 0);
		props["height"] = ::MakeIntProp("New script height (PlayResY)", 0);
		props["undo_point"] = ::MakeStringProp("Undo description (optional)");
		Array required;
		required.emplace_back(std::string("width"));
		required.emplace_back(std::string("height"));
		::agi::mcp::RegisterTool(
			"set_script_resolution",
			"Change the script resolution (PlayResX/PlayResY). "
			"Use when open_video reports resolution_mismatch=true and subtitles should be rescaled to the video size.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleSetScriptResolution);
	}

	{
		Object props;
		props["line_indices"] = ::MakeIntArrayProp("1-based indices of lines to select");
		props["active_line"] = ::MakeIntProp("1-based index of the active line (default: first selected)", -1);
		Array required;
		required.emplace_back(std::string("line_indices"));
		::agi::mcp::RegisterTool(
			"set_selection",
			"Select subtitle lines in the grid. Pair of get_selected_lines.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleSetSelection);
	}

	{
		Object props;
		props["start_ms"] = ::MakeIntProp("Playback start time in ms", 0);
		props["end_ms"] = ::MakeIntProp("Playback end time in ms (default: end of audio)", -1);
		Array required;
		required.emplace_back(std::string("start_ms"));
		::agi::mcp::RegisterTool(
			"play_audio",
			"Play a range of the loaded audio (and video if it is playing). Useful for AI to audition a subtitle line's timing.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandlePlayAudio);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Absolute path to the .ass file to open");
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"open_file",
			"Open a subtitle file by path. Loads the file into the current Aegisub instance.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleOpenFile);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Optional save path. If omitted, saves to the current file path.");
		::agi::mcp::RegisterTool(
			"save_file",
			"Save the current subtitle file. If no path is given, saves to the current file location.",
			::MakeObjectSchema(std::move(props)),
			::HandleSaveFile);
	}

	{
		::agi::mcp::RegisterTool(
			"new_file",
			"Create a new blank subtitle file in the current Aegisub instance.",
			::MakeObjectSchema(Object{}),
			::HandleNewFile);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Absolute path to the video file to open");
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"open_video",
			"Open a video file in the current Aegisub instance. Blocks until the video is loaded.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleOpenVideo);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Absolute path to the audio file to open");
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"open_audio",
			"Open an audio file in the current Aegisub instance. Blocks until the audio is loaded.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleOpenAudio);
	}

	{
		::agi::mcp::RegisterTool(
			"close_video",
			"Close the currently open video file.",
			::MakeObjectSchema(Object{}),
			::HandleCloseVideo);
	}

	{
		::agi::mcp::RegisterTool(
			"close_audio",
			"Close the currently open audio file.",
			::MakeObjectSchema(Object{}),
			::HandleCloseAudio);
	}

	{
		Object props;
		props["frame"] = ::MakeIntProp("Frame number to capture (default: current frame)", -1);
		props["ms"] = ::MakeIntProp("Time in milliseconds to capture (used when frame is not given)", -1);
		props["raw"] = ::MakeBoolProp("Capture raw frame without subtitles", false);
		::agi::mcp::RegisterTool(
			"get_video_frame",
			"Capture a video frame and return it as a PNG image (base64 data URI). "
			"Returns frame info as text plus the image content.",
			::MakeObjectSchema(std::move(props)),
			::HandleGetVideoFrame);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Output file path, e.g. C:/shots/frame.png or .jpg");
		props["frame"] = ::MakeIntProp("Frame number to save (default: current frame)", -1);
		props["ms"] = ::MakeIntProp("Time in milliseconds to save (used when frame is not given)", -1);
		props["format"] = ::MakeStringProp("Output format: 'png' (default) or 'jpeg'", "png");
		props["raw"] = ::MakeBoolProp("Save raw frame without subtitles", false);
		Array required;
		required.emplace_back(std::string("path"));
		::agi::mcp::RegisterTool(
			"save_video_frame",
			"Save a video frame screenshot to a PNG or JPEG file.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleSaveVideoFrame);
	}

	{
		Object props;
		props["path"] = ::MakeStringProp("Output GIF file path");
		props["start_frame"] = ::MakeIntProp("First frame to export", 0);
		props["end_frame"] = ::MakeIntProp("Last frame to export (exclusive of start)", 0);
		props["quality"] = ::MakeIntProp("GIF quality 1-100", 100);
		props["scale_factor"] = ::MakeIntProp("Integer downscale factor for output size (1 = original)", 1);
		Array required;
		required.emplace_back(std::string("path"));
		required.emplace_back(std::string("start_frame"));
		required.emplace_back(std::string("end_frame"));
		::agi::mcp::RegisterTool(
			"export_gif",
			"Export a range of video frames (with rendered subtitles) as a GIF animation file.",
			::MakeObjectSchema(std::move(props), std::move(required)),
			::HandleExportGif);
	}

	{
		Object props;
		props["start_ms"] = ::MakeIntProp("Start time in milliseconds (default 0)", 0);
		props["end_ms"] = ::MakeIntProp("End time in milliseconds (default: end of audio)", -1);
		props["points"] = ::MakeIntProp("Number of waveform points to return (1-4096)", 256);
		::agi::mcp::RegisterTool(
			"get_audio_waveform",
			"Get audio waveform data (per-point peak and average amplitude) for a time range. "
			"Useful for detecting speech/pauses and aligning subtitles.",
			::MakeObjectSchema(std::move(props)),
			::HandleGetAudioWaveform);
	}

	{
		Object props;
		props["ms"] = ::MakeIntProp("Time in milliseconds to analyze (default 0)", 0);
		props["fft_size"] = ::MakeIntProp("FFT size (power of two, 256-32768)", 1024);
		::agi::mcp::RegisterTool(
			"get_audio_spectrum",
			"Get audio spectrum power data (low frequencies first; silence=0, full-scale peak about 1.9-2.9) at a given time. "
			"Useful for speech/music discrimination and voice activity detection.",
			::MakeObjectSchema(std::move(props)),
			::HandleGetAudioSpectrum);
	}
}

} // namespace agi::mcp
