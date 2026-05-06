// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪配置持久化实现
// 复用 aegisub-motion.json 的 "persp" section

#include "perspective_config.h"
#include "../mocha_motion/motion_config.h"
#include "../options.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/writer.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

namespace mocha {
	bool PerspectiveConfig::Load(PerspectiveOptions &opts) {
		auto path = config::path->Decode("?user") / MotionConfig::CONFIG_FILENAME;
		if (!agi::fs::FileExists(path)) {
			Save(opts);
			return false;
		}

		try {
			auto root = agi::json_util::parse(*agi::io::Open(path));
			json::Object const &root_obj = root;
			auto persp_it = root_obj.find("persp");
			if (persp_it == root_obj.end())
				return false;

			// UnknownElement 不可拷贝，需通过引用访问
			json::Object const *persp_obj = nullptr;
			try {
				persp_obj = &static_cast<json::Object const &>(persp_it->second);
			} catch (...) {
				return false;
			}

			auto read_bool = [&](const char *key, bool &target) {
				auto it = persp_obj->find(key);
				if (it != persp_obj->end()) {
					try { target = static_cast<json::Boolean const &>(it->second); } catch (...) {}
				}
			};
			auto read_int = [&](const char *key, int &target) {
				auto it = persp_obj->find(key);
				if (it != persp_obj->end()) {
					try { target = static_cast<int>(static_cast<json::Integer const &>(it->second)); } catch (...) {}
				}
			};

			read_int("refFrame", opts.relframe);
			read_bool("trackPos", opts.track_pos);
			read_bool("trackClip", opts.track_clip);
			read_bool("trackBordShad", opts.track_bord_shad);
			read_bool("applyPersp", opts.apply_perspective);
			read_int("orgMode", opts.org_mode);
			read_bool("relative", opts.relative);
			read_int("startFrame", opts.start_frame);
			read_bool("preview", opts.preview);
			read_bool("reverseTracking", opts.reverse_tracking);
			read_bool("writeConf", opts.write_conf);

			return true;
		} catch (json::Exception const &e) {
			LOG_E("persp/config") << "JSON parse error: " << e.what();
			return false;
		}
	}

	bool PerspectiveConfig::Save(const PerspectiveOptions &opts) {
		auto path = config::path->Decode("?user") / MotionConfig::CONFIG_FILENAME;

		try {
			json::Object root_obj;
			if (agi::fs::FileExists(path)) {
				try {
					auto existing = agi::json_util::parse(*agi::io::Open(path));
					root_obj = std::move(static_cast<json::Object &>(existing));
				} catch (...) {}
			}

			json::Object persp_obj;
			persp_obj["refFrame"] = json::UnknownElement(static_cast<int64_t>(opts.relframe));
			persp_obj["trackPos"] = json::UnknownElement(opts.track_pos);
			persp_obj["trackClip"] = json::UnknownElement(opts.track_clip);
			persp_obj["trackBordShad"] = json::UnknownElement(opts.track_bord_shad);
			persp_obj["applyPersp"] = json::UnknownElement(opts.apply_perspective);
			persp_obj["orgMode"] = json::UnknownElement(static_cast<int64_t>(opts.org_mode));
			persp_obj["relative"] = json::UnknownElement(opts.relative);
			persp_obj["startFrame"] = json::UnknownElement(static_cast<int64_t>(opts.start_frame));
			persp_obj["preview"] = json::UnknownElement(opts.preview);
			persp_obj["reverseTracking"] = json::UnknownElement(opts.reverse_tracking);
			persp_obj["writeConf"] = json::UnknownElement(opts.write_conf);

			root_obj["persp"] = json::UnknownElement(std::move(persp_obj));

			agi::io::Save file(path);
			agi::JsonWriter::Write(root_obj, file.Get());
			return true;
		} catch (agi::Exception const &e) {
			LOG_E("persp/config") << "Failed to write config: " << e.GetMessage();
			return false;
		}
	}
} // namespace mocha
