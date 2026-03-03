// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "async_video_provider.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "export_fixstyle.h"
#include "include/aegisub/subtitles_provider.h"
#include "video_frame.h"
#include "video_provider_manager.h"
#include "options.h"

#include <algorithm>
#include <fstream>
#include <libaegisub/dispatch.h>
#include <libaegisub/fs.h>

#if BOOST_VERSION >= 106900
#include <boost/gil.hpp>
#else
#include <boost/gil/gil_all.hpp>
#endif

enum {
	NEW_SUBS_FILE = -1,
	SUBS_FILE_ALREADY_LOADED = -2
};

/// @brief 从ASS事件文本中提取\\img标签引用的图片文件路径
/// @param subs ASS字幕文件
/// @return 去重后的图片路径列表
static std::vector<std::string> ExtractImgPaths(const AssFile& subs) {
	std::vector<std::string> paths;
	for (const auto& line : subs.Events) {
		if (line.Comment) continue;
		auto& text = line.Text.get();
		size_t pos = 0;
		while ((pos = text.find("img(", pos)) != std::string::npos) {
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
	}
	std::sort(paths.begin(), paths.end());
	paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
	return paths;
}

std::shared_ptr<VideoFrame> AsyncVideoProvider::ProcFrame(int frame_number, double time, bool raw) {
	// L1缓存查找：合成帧 (frame_number + subs_version + raw) 完全命中则直接返回
	for (auto& entry : frame_cache) {
		if (entry.composited && entry.frame_number == frame_number
			&& entry.raw == raw && entry.subs_ver == subs_version) {
			return entry.composited;
		}
	}

	// L2缓存查找
	auto l2_it = raw_video_cache.find(frame_number);
	bool l2_hit = (l2_it != raw_video_cache.end());

	// L2命中时更新LRU访问顺序（移至链表尾部表示最近使用）
	if (l2_hit)
		l2_lru_order.splice(l2_lru_order.end(), l2_lru_order, l2_it->second.lru_it);

	// 原始帧/无字幕路径 + L2命中：零拷贝直接返回缓存引用
	if (l2_hit && (raw || !subs_provider || !subs)) {
		auto& cached = l2_it->second.frame;
		FrameCacheEntry entry;
		entry.frame_number = frame_number;
		entry.subs_ver = subs_version;
		entry.raw = raw;
		entry.composited = cached;
		if (frame_cache.size() < L1_CACHE_CAPACITY)
			frame_cache.push_back(std::move(entry));
		else {
			frame_cache[cache_next] = std::move(entry);
			cache_next = (cache_next + 1) % L1_CACHE_CAPACITY;
		}
		return cached;
	}

	// 分配工作缓冲区（需字幕合成或L2未命中时使用）
	std::shared_ptr<VideoFrame> frame;
	for (auto& buffer : buffers) {
		if (buffer.use_count() == 1) { frame = buffer; break; }
	}
	if (!frame) {
		frame = std::make_shared<VideoFrame>();
		buffers.push_back(frame);
	}

	if (l2_hit) {
		// L2命中但需字幕合成：拷贝原始帧到工作缓冲区
		*frame = *(l2_it->second.frame);
	} else {
		try {
			source_provider->GetFrame(frame_number, *frame);
		}
		catch (VideoProviderError const& err) { throw VideoProviderErrorEvent(err); }

		// 存入L2缓存（LRU淘汰策略）
		auto cached_frame = std::make_shared<VideoFrame>(*frame);
		if (raw_video_cache.size() >= l2_cache_capacity) {
			// 淘汰最久未使用的条目
			int evict_key = l2_lru_order.front();
			raw_video_cache.erase(evict_key);
			l2_lru_order.pop_front();
		}
		l2_lru_order.push_back(frame_number);
		raw_video_cache[frame_number] = { std::move(cached_frame), std::prev(l2_lru_order.end()) };
	}

	if (raw || !subs_provider || !subs) {
		// 存入L1缓存
		FrameCacheEntry entry;
		entry.frame_number = frame_number;
		entry.subs_ver = subs_version;
		entry.raw = raw;
		entry.composited = frame;
		if (frame_cache.size() < L1_CACHE_CAPACITY)
			frame_cache.push_back(std::move(entry));
		else {
			frame_cache[cache_next] = std::move(entry);
			cache_next = (cache_next + 1) % L1_CACHE_CAPACITY;
		}
		return frame;
	}

	try {
		if (single_frame != frame_number && single_frame != SUBS_FILE_ALREADY_LOADED) {
			// Generally edits and seeks come in groups; if the last thing done
			// was seek it is more likely that the user will seek again and
			// vice versa. As such, if this is the first frame requested after
			// an edit, only export the currently visible lines (because the
			// other lines will probably not be viewed before the file changes
			// again), and if it's a different frame, export the entire file.
			if (single_frame != NEW_SUBS_FILE) {
				subs_provider->LoadSubtitles(subs.get());
				single_frame = SUBS_FILE_ALREADY_LOADED;
			}
			else {
				AssFixStylesFilter::ProcessSubs(subs.get());
				single_frame = frame_number;
				subs_provider->LoadSubtitles(subs.get(), time);
			}
		}
	}
	catch (agi::Exception const& err) { throw SubtitlesProviderErrorEvent(err.GetMessage()); }

	try {
		subs_provider->DrawSubtitles(*frame, time / 1000.);
	}
	catch (agi::UserCancelException const&) { }

	// 存入L1缓存
	FrameCacheEntry entry;
	entry.frame_number = frame_number;
	entry.subs_ver = subs_version;
	entry.raw = false;
	entry.composited = frame;
	if (frame_cache.size() < L1_CACHE_CAPACITY)
		frame_cache.push_back(std::move(entry));
	else {
		frame_cache[cache_next] = std::move(entry);
		cache_next = (cache_next + 1) % L1_CACHE_CAPACITY;
	}

	return frame;
}

VideoFrame AsyncVideoProvider::GetBlankFrame(bool white) {
	VideoFrame result;
	result.width = GetWidth();
	result.height = GetHeight();
	result.pitch = result.width * 4;
	result.flipped = false;
	result.data.resize(result.pitch * result.height, white ? 255 : 0);
	return result;
}

VideoFrame AsyncVideoProvider::GetSubtitles(double time) {
	VideoFrame result;
	worker->Sync([&]{
		// 与 worker 线程同步，避免并发访问 subs/subs_provider 导致数据竞争
		// We want to combine all transparent subtitle layers onto one layer.
		// Instead of alpha blending them all together, which can be messy and cause
		// rounding errors, we draw them once on a black frame and once on a white frame,
		// and solve for the color and alpha. This has the benefit of being independent
		// of the subtitle provider, as long as the provider works by alpha blending.
		VideoFrame frame_black = GetBlankFrame(false);
		if (!subs) { result = frame_black; return; }
		VideoFrame frame_white = GetBlankFrame(true);

		subs_provider->LoadSubtitles(subs.get());
		subs_provider->DrawSubtitles(frame_black, time / 1000.);
		subs_provider->DrawSubtitles(frame_white, time / 1000.);

		using namespace boost::gil;
		auto blackview = interleaved_view(frame_black.width, frame_black.height, (bgra8_pixel_t*) frame_black.data.data(), frame_black.width * 4);
		auto whiteview = interleaved_view(frame_white.width, frame_white.height, (bgra8_pixel_t*) frame_white.data.data(), frame_white.width * 4);

		transform_pixels(blackview, whiteview, blackview, [](const bgra8_pixel_t black, const bgra8_pixel_t white) -> bgra8_pixel_t {
			int a = 255 - (white[0] - black[0]);

			bgra8_pixel_t ret;
			if (a == 0) {
				ret[0] = 0;
				ret[1] = 0;
				ret[2] = 0;
				ret[3] = 0;
			} else {
				ret[0] = black[0] / (a / 255.);
				ret[1] = black[1] / (a / 255.);
				ret[2] = black[2] / (a / 255.);
				ret[3] = a;
			}
			return ret;
		});

		result = frame_black;
	}); // worker->Sync
	return result;
}

static std::unique_ptr<SubtitlesProvider> get_subs_provider(wxEvtHandler *evt_handler, agi::BackgroundRunner *br) {
	try {
		return SubtitlesProviderFactory::GetProvider(br);
	}
	catch (agi::Exception const& err) {
		evt_handler->AddPendingEvent(SubtitlesProviderErrorEvent(err.GetMessage()));
		return nullptr;
	}
}

AsyncVideoProvider::AsyncVideoProvider(agi::fs::path const& video_filename, std::string_view colormatrix, wxEvtHandler *parent, agi::BackgroundRunner *br)
: worker(agi::dispatch::Create())
, subs_provider(get_subs_provider(parent, br))
, source_provider(VideoProviderFactory::GetProvider(video_filename, colormatrix, br))
, parent(parent)
{
	// 根据视频分辨率动态计算L2缓存容量，限制总缓存内存约256MB
	size_t frame_bytes = static_cast<size_t>(source_provider->GetWidth()) * source_provider->GetHeight() * 4;
	l2_cache_capacity = frame_bytes > 0
		? std::clamp<size_t>(256ULL * 1024 * 1024 / frame_bytes, 2, 16)
		: 16;
}

AsyncVideoProvider::~AsyncVideoProvider() {
	// 通知预取任务尽快退出（req_version < version 条件成立）
	++version;
	// Block until all currently queued jobs are complete
	worker->Sync([]{});
}

void AsyncVideoProvider::LoadSubtitles(const AssFile *new_subs) throw() {
	uint_fast32_t req_version = ++version;

	auto copy = new AssFile(*new_subs);
	worker->Async([=, this]{
		subs.reset(copy);
		single_frame = NEW_SUBS_FILE;
		prefetch_full_subs = true;
		++subs_version;
		// 清除L1合成帧缓存，版本递增后所有条目均已过期
		frame_cache.clear();
		cache_next = 0;

		// 提前启动OS文件缓存预热（不依赖CSRI渲染器，最早时机）
		auto warm_paths = ExtractImgPaths(*subs);
		for (auto& p : warm_paths) {
			agi::dispatch::Background().Async([p = agi::fs::path(p)] {
				try {
					std::ifstream f(p, std::ios::binary);
					if (!f) return;
					char buf[65536];
					while (f.read(buf, sizeof(buf))) {}
				} catch (...) {}
			});
		}

		ProcAsync(req_version, false);
	});
}

void AsyncVideoProvider::UpdateSubtitles(const AssDialogue *changed) throw() {
	uint_fast32_t req_version = ++version;

	// Copy just the line which were changed, then replace the line at the
	// same index in the worker's copy of the file with the new entry
	auto copy = new AssDialogue(*changed);
	worker->Async([=, this]{
		int i = 0;
		auto it = subs->Events.begin();
		std::advance(it, copy->Row - i);
		i = copy->Row;
		subs->Events.insert(it, *copy);
		delete &*it--;

		single_frame = NEW_SUBS_FILE;
		++subs_version;
		// 清除L1合成帧缓存，版本递增后所有条目均已过期
		frame_cache.clear();
		cache_next = 0;
		ProcAsync(req_version, true);
	});
}

void AsyncVideoProvider::RequestFrame(int new_frame, double new_time) throw() {
	uint_fast32_t req_version = ++version;

	worker->Async([=, this]{
		time = new_time;
		frame_number = new_frame;
		ProcAsync(req_version, false);
	});
}

bool AsyncVideoProvider::NeedUpdate(std::vector<AssDialogueBase const*> const& visible_lines) {
	// Always need to render after a seek
	if (single_frame != NEW_SUBS_FILE || frame_number != last_rendered)
		return true;

	// Obviously need to render if the number of visible lines has changed
	if (visible_lines.size() != last_lines.size())
		return true;

	for (size_t i = 0; i < last_lines.size(); ++i) {
		auto const& last = last_lines[i];
		auto const& cur = *visible_lines[i];
		if (last.Layer  != cur.Layer)  return true;
		if (last.Margin != cur.Margin) return true;
		if (last.Style  != cur.Style)  return true;
		if (last.Effect != cur.Effect) return true;
		if (last.Text   != cur.Text)   return true;

		// Changing the start/end time effects the appearance only if the
		// line is animated. This is obviously not a very accurate check for
		// animated lines, but false positives aren't the end of the world
		if ((last.Start != cur.Start || last.End != cur.End) &&
			(!cur.Effect.get().empty() || cur.Text.get().find('\\') != std::string::npos))
			return true;
	}

	return false;
}

void AsyncVideoProvider::ProcAsync(uint_fast32_t req_version, bool check_updated) {
	// Only actually produce the frame if there's no queued changes waiting
	if (req_version < version || frame_number < 0) return;

	std::vector<AssDialogueBase const*> visible_lines;
	for (auto const& line : subs->Events) {
		if (!line.Comment && !(line.Start > time || line.End <= time))
			visible_lines.push_back(&line);
	}

	if (check_updated && !NeedUpdate(visible_lines)) return;

	last_lines.clear();
	last_lines.reserve(visible_lines.size());
	for (auto line : visible_lines)
		last_lines.push_back(*line);
	last_rendered = frame_number;

	try {
		auto evt = new FrameReadyEvent(ProcFrame(frame_number, time), time);
		evt->SetEventType(EVT_FRAME_READY);
		parent->QueueEvent(evt);
	}
	catch (wxEvent const& err) {
		// Pass error back to parent thread
		parent->QueueEvent(err.Clone());
	}

	// 首帧渲染后预导出完整字幕文件，使后续seek时无需重新序列化
	// 拆分为独立任务：析构器 ++version 后，此任务开头即可检测并提前退出
	// 注：DrawSubtitles/GetFrame 预取循环已移除——这些不可中断的库调用
	// 会导致析构器 worker->Sync 长时间阻塞（切换硬件加速时卡顿）
	// OS 文件缓存预热已在 LoadSubtitles 中通过 Background 线程池独立完成
	if (prefetch_full_subs && single_frame >= 0 && single_frame != SUBS_FILE_ALREADY_LOADED
		&& OPT_GET("Video/Prefetch")->GetBool()) {
		prefetch_full_subs = false;
		worker->Async([this, pv = req_version] {
			if (pv < version) return;
			if (subs && subs_provider) {
				try {
					subs_provider->LoadSubtitles(subs.get());
					single_frame = SUBS_FILE_ALREADY_LOADED;
				}
				catch (...) { }
			}
		});
	}
}

std::shared_ptr<VideoFrame> AsyncVideoProvider::GetFrame(int frame, double time, bool raw) {
	std::shared_ptr<VideoFrame> ret;
	worker->Sync([&]{ ret = ProcFrame(frame, time, raw); });
	return ret;
}

void AsyncVideoProvider::SetColorSpace(std::string_view matrix) {
	worker->Async([this, matrix = std::string(matrix)]() {
		source_provider->SetColorSpace(matrix);
		// 色彩空间变更后原始帧需重新解码，清除L2缓存
		raw_video_cache.clear();
		l2_lru_order.clear();
	});
}

wxDEFINE_EVENT(EVT_FRAME_READY, FrameReadyEvent);
wxDEFINE_EVENT(EVT_VIDEO_ERROR, VideoProviderErrorEvent);
wxDEFINE_EVENT(EVT_SUBTITLES_ERROR, SubtitlesProviderErrorEvent);

VideoProviderErrorEvent::VideoProviderErrorEvent(VideoProviderError const& err)
: agi::Exception(std::string(err.GetMessage()))
{
	SetEventType(EVT_VIDEO_ERROR);
}
SubtitlesProviderErrorEvent::SubtitlesProviderErrorEvent(std::string const& err)
: agi::Exception(std::string(err))
{
	SetEventType(EVT_SUBTITLES_ERROR);
}
