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
/// 工具覆盖：工程信息，字幕读写，样式操作，时间轴调整，帧/时间转换，关键帧等

#include "mcp_server.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "async_video_provider.h"
#include "audio_controller.h"
#include "command/command.h"
#include "frame_main.h"
#include "include/aegisub/context.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "time_range.h"
#include "video_controller.h"
#include "charset_detect.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/mcp/server.h>
#include <libaegisub/path.h>

#include <algorithm>
#include <regex>
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

/// 执行已注册命令
UnknownElement HandleRunCommand(Object const& args) {
	auto command = GetString(args, "command");
	if (command.empty())
		throw std::runtime_error("missing 'command'");
	cmd::call(command, RequireContext());
	Object result;
	result["ok"] = true;
	result["command"] = command;
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
	for (auto& s : ctx->ass->Styles) {
		Object obj;
		obj["name"] = s.name;
		obj["font"] = s.font;
		obj["fontsize"] = s.fontsize;
		obj["bold"] = s.bold;
		obj["italic"] = s.italic;
		obj["underline"] = s.underline;
		obj["strikeout"] = s.strikeout;
		obj["alignment"] = static_cast<int64_t>(s.alignment);
		styles.emplace_back(std::move(obj));
	}
	Object result;
	result["styles"] = std::move(styles);
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

		// 整数字段
		if (auto f = fields->find("layer"); f != fields->end()) {
			try { diag.Layer = static_cast<int>(static_cast<int64_t const&>(f->second)); } catch (...) {}
		}
		if (auto f = fields->find("start"); f != fields->end()) {
			try { diag.Start = static_cast<int>(static_cast<int64_t const&>(f->second)); } catch (...) {}
		}
		if (auto f = fields->find("end"); f != fields->end()) {
			try { diag.End = static_cast<int>(static_cast<int64_t const&>(f->second)); } catch (...) {}
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

	ctx->ass->Events.push_back(diag);

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
	std::vector<AssDialogue*> to_delete;
	int line_num = 0;
	for (auto& diag : ctx->ass->Events) {
		++line_num;
		for (auto const& idx_val : *indices) {
			try {
				if (static_cast<int64_t const&>(idx_val) == line_num) {
					to_delete.push_back(&diag);
					break;
				}
			} catch (...) {}
		}
	}

	// 从大到小排序
	std::sort(to_delete.begin(), to_delete.end(), [](AssDialogue* a, AssDialogue* b) {
		return a->Row > b->Row;
	});

	for (auto* d : to_delete) {
		ctx->ass->Events.erase(ctx->ass->iterator_to(*d));
	}

	auto undo = GetString(args, "undo_point");
	ctx->ass->Commit(!undo.empty() ? wxString::FromUTF8(undo) : _("delete lines"), AssFile::COMMIT_DIAG_ADDREM);

	Object result;
	result["ok"] = true;
	result["deleted"] = static_cast<int64_t>(to_delete.size());
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
		diag.Start = std::max(0, diag.Start + static_cast<int>(start_offset));
		diag.End = std::max(0, diag.End + static_cast<int>(end_offset));
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
	std::string encoding = CharSetDetect::GetEncoding(path);
	ctx->subsController->Load(agi::fs::path(path), encoding.c_str());
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
	ctx->subsController->Save(agi::fs::path(std::filesystem::path(wxString::FromUTF8(path).ToStdWstring())));
	Object result;
	result["ok"] = true;
	result["filename"] = path;
	return result;
}

/// 新建字幕文件
UnknownElement HandleNewFile(Object const& /*args*/) {
	auto* ctx = RequireContext();
	ctx->project->CloseSubtitles();
	Object result;
	result["ok"] = true;
	result["filename"] = ctx->subsController->Filename().string();
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
		Object props;
		props["command"] = ::MakeStringProp("Aegisub command name, e.g. 'time/shift'");
		Array required;
		required.emplace_back(std::string("command"));
		::agi::mcp::RegisterTool(
			"run_command",
			"Execute a registered Aegisub command by name (equivalent to clicking the menu item)",
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
			"List all styles defined in the current subtitle file",
			::MakeObjectSchema(Object{}),
			::HandleGetStyles);
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
}

} // namespace agi::mcp
