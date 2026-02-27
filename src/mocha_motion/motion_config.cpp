// Copyright (c) 2024-2026, Aegisub contributors
// 运动追踪配置持久化实现
// 对应 MoonScript 版 Aegisub-Motion 的 ConfigHandler 模块
// 使用 cajun JSON 库读写配置，与 MoonScript 版 json.encode/decode 兼容

#include "motion_config.h"

#include "../options.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/writer.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

namespace mocha {
	agi::fs::path MotionConfig::GetConfigPath() {
		return config::path->Decode("?user") / CONFIG_FILENAME;
	}

	bool MotionConfig::Load(MotionOptions &opts) {
		const auto path = GetConfigPath();
		if (!agi::fs::FileExists(path)) {
			LOG_D("mocha/config") << "Configuration file not found, creating defaults: " << path;
			// 对应 MoonScript ConfigHandler.read：配置文件不存在时自动写入默认值
			Save(opts);
			return false;
		}

		try {
			auto root = agi::json_util::parse(*agi::io::Open(path));
			json::Object const &root_obj = root;

			// 查找 main section
			const auto main_it = root_obj.find("main");
			if (main_it == root_obj.end()) {
				LOG_D("mocha/config") << "No 'main' section in configuration file";
				return false;
			}

			json::Object const &main_obj = main_it->second;

			// 读取每个配置项，仅更新已知字段
			// 对应 MoonScript ConfigHandler.parse：只更新 @configuration 中已有的键
			auto read_bool = [&](const char *key, bool &target) {
				const auto it = main_obj.find(key);
				if (it != main_obj.end()) {
					try {
						target = static_cast<json::Boolean const &>(it->second);
					} catch (...) {
						LOG_D("mocha/config") << "Type mismatch for bool key: " << key;
					}
				}
			};

			auto read_int = [&](const char *key, int &target) {
				auto it = main_obj.find(key);
				if (it != main_obj.end()) {
					try {
						target = static_cast<int>(static_cast<json::Integer const &>(it->second));
					} catch (...) {
						LOG_D("mocha/config") << "Type mismatch for int key: " << key;
					}
				}
			};

			auto read_double = [&](const char *key, double &target) {
				auto it = main_obj.find(key);
				if (it != main_obj.end()) {
					try {
						// JSON 数值可能是 Integer 或 Double
						try {
							target = static_cast<json::Double const &>(it->second);
						} catch (...) {
							target = static_cast<double>(static_cast<json::Integer const &>(it->second));
						}
					} catch (...) {
						LOG_D("mocha/config") << "Type mismatch for double key: " << key;
					}
				}
			};

			// MoonScript 兼容字段名（camelCase）
			read_bool("xPosition", opts.x_position);
			read_bool("yPosition", opts.y_position);
			read_bool("origin", opts.origin);
			read_bool("absPos", opts.abs_pos);
			read_bool("xScale", opts.x_scale);
			read_bool("border", opts.border);
			read_bool("shadow", opts.shadow);
			read_bool("blur", opts.blur);
			read_double("blurScale", opts.blur_scale);
			read_bool("xRotation", opts.x_rotation);
			read_bool("yRotation", opts.y_rotation);
			read_bool("zRotation", opts.z_rotation);
			read_bool("zPosition", opts.z_position);
			read_bool("writeConf", opts.write_conf);
			read_bool("relative", opts.relative);
			read_int("startFrame", opts.start_frame);
			read_bool("linear", opts.linear);
			read_bool("clipOnly", opts.clip_only);
			read_bool("rectClip", opts.rect_clip);
			read_bool("vectClip", opts.vect_clip);
			read_bool("rcToVc", opts.rc_to_vc);
			read_bool("killTrans", opts.kill_trans);

			// C++ 扩展字段（MoonScript 版不会写入这些，但也不会因为它们存在而报错）
			read_bool("preview", opts.preview);
			read_bool("reverseTracking", opts.reverse_tracking);

			LOG_D("mocha/config") << "Configuration loaded from: " << path;
			return true;
		} catch (json::Exception const &e) {
			LOG_E("mocha/config") << "JSON parse error: " << e.what();
			return false;
		} catch (agi::Exception const &e) {
			LOG_E("mocha/config") << "Failed to read configuration: " << e.GetMessage();
			return false;
		} catch (...) {
			LOG_E("mocha/config") << "Unexpected error reading configuration";
			return false;
		}
	}

	bool MotionConfig::Save(const MotionOptions &opts, const std::string &version) {
		const auto path = GetConfigPath();

		try {
			// 先尝试加载已有 JSON 以保留其他 section（clip、trim 等）
			// 对应 MoonScript ConfigHandler 的多 section 架构
			json::Object root_obj;
			if (agi::fs::FileExists(path)) {
				try {
					auto existing = agi::json_util::parse(*agi::io::Open(path));
					// UnknownElement 禁止拷贝，需使用移动语义获取内部 Object
					root_obj = std::move(static_cast<json::Object &>(existing));
				} catch (...) {
					// 已有文件解析失败，使用空对象
					LOG_D("mocha/config") << "Existing config file corrupted, will overwrite";
				}
			}

			// 构建 main section
			json::Object main_obj;

			// MoonScript 兼容字段名（camelCase）
			main_obj["xPosition"] = json::UnknownElement(opts.x_position);
			main_obj["yPosition"] = json::UnknownElement(opts.y_position);
			main_obj["origin"] = json::UnknownElement(opts.origin);
			main_obj["absPos"] = json::UnknownElement(opts.abs_pos);
			main_obj["xScale"] = json::UnknownElement(opts.x_scale);
			main_obj["border"] = json::UnknownElement(opts.border);
			main_obj["shadow"] = json::UnknownElement(opts.shadow);
			main_obj["blur"] = json::UnknownElement(opts.blur);
			main_obj["blurScale"] = json::UnknownElement(opts.blur_scale);
			main_obj["xRotation"] = json::UnknownElement(opts.x_rotation);
			main_obj["yRotation"] = json::UnknownElement(opts.y_rotation);
			main_obj["zRotation"] = json::UnknownElement(opts.z_rotation);
			main_obj["zPosition"] = json::UnknownElement(opts.z_position);
			main_obj["writeConf"] = json::UnknownElement(opts.write_conf);
			main_obj["relative"] = json::UnknownElement(opts.relative);
			main_obj["startFrame"] = json::UnknownElement((int64_t) opts.start_frame);
			main_obj["linear"] = json::UnknownElement(opts.linear);
			main_obj["clipOnly"] = json::UnknownElement(opts.clip_only);
			main_obj["rectClip"] = json::UnknownElement(opts.rect_clip);
			main_obj["vectClip"] = json::UnknownElement(opts.vect_clip);
			main_obj["rcToVc"] = json::UnknownElement(opts.rc_to_vc);
			main_obj["killTrans"] = json::UnknownElement(opts.kill_trans);

			// C++ 扩展字段
			main_obj["preview"] = json::UnknownElement(opts.preview);
			main_obj["reverseTracking"] = json::UnknownElement(opts.reverse_tracking);

			// 更新 main section
			root_obj["main"] = json::UnknownElement(std::move(main_obj));

			// 注入版本号（对应 MoonScript: @configuration.__version = @version）
			root_obj["__version"] = json::UnknownElement(version);

			// 写入文件
			agi::io::Save file(path);
			agi::JsonWriter::Write(root_obj, file.Get());

			LOG_D("mocha/config") << "Configuration saved to: " << path;
			return true;
		} catch (agi::Exception const &e) {
			LOG_E("mocha/config") << "Failed to write configuration: " << e.GetMessage();
			return false;
		} catch (...) {
			LOG_E("mocha/config") << "Unexpected error writing configuration";
			return false;
		}
	}

	void MotionConfig::Remove() {
		const auto path = GetConfigPath();
		agi::fs::Remove(path);
	}

	bool MotionConfig::LoadClip(ClipTrackOptions &opts) {
		const auto path = GetConfigPath();
		if (!agi::fs::FileExists(path)) return false;

		try {
			auto root = agi::json_util::parse(*agi::io::Open(path));
			json::Object const &root_obj = root;

			const auto clip_it = root_obj.find("clip");
			if (clip_it == root_obj.end()) return false;

			json::Object const &clip_obj = clip_it->second;

			auto read_bool = [&](const char *key, bool &target) {
				const auto it = clip_obj.find(key);
				if (it != clip_obj.end()) {
					try { target = static_cast<json::Boolean const &>(it->second); }
					catch (...) {}
				}
			};

			auto read_int = [&](const char *key, int &target) {
				auto it = clip_obj.find(key);
				if (it != clip_obj.end()) {
					try { target = static_cast<int>(static_cast<json::Integer const &>(it->second)); }
					catch (...) {}
				}
			};

			read_bool("xPosition", opts.x_position);
			read_bool("yPosition", opts.y_position);
			read_bool("xScale", opts.x_scale);
			read_bool("zRotation", opts.z_rotation);
			read_bool("rectClip", opts.rect_clip);
			read_bool("vectClip", opts.vect_clip);
			read_bool("rcToVc", opts.rc_to_vc);
			read_int("startFrame", opts.start_frame);
			read_bool("relative", opts.relative);

			LOG_D("mocha/config") << "Clip configuration loaded";
			return true;
		} catch (...) {
			LOG_E("mocha/config") << "Error reading clip configuration";
			return false;
		}
	}

	bool MotionConfig::SaveClip(const ClipTrackOptions &opts) {
		const auto path = GetConfigPath();

		try {
			json::Object root_obj;
			if (agi::fs::FileExists(path)) {
				try {
					auto existing = agi::json_util::parse(*agi::io::Open(path));
					root_obj = std::move(static_cast<json::Object &>(existing));
				} catch (...) {}
			}

			json::Object clip_obj;
			clip_obj["xPosition"] = json::UnknownElement(opts.x_position);
			clip_obj["yPosition"] = json::UnknownElement(opts.y_position);
			clip_obj["xScale"] = json::UnknownElement(opts.x_scale);
			clip_obj["zRotation"] = json::UnknownElement(opts.z_rotation);
			clip_obj["rectClip"] = json::UnknownElement(opts.rect_clip);
			clip_obj["vectClip"] = json::UnknownElement(opts.vect_clip);
			clip_obj["rcToVc"] = json::UnknownElement(opts.rc_to_vc);
			clip_obj["startFrame"] = json::UnknownElement((int64_t) opts.start_frame);
			clip_obj["relative"] = json::UnknownElement(opts.relative);

			root_obj["clip"] = json::UnknownElement(std::move(clip_obj));

			agi::io::Save file(path);
			agi::JsonWriter::Write(root_obj, file.Get());

			LOG_D("mocha/config") << "Clip configuration saved";
			return true;
		} catch (...) {
			LOG_E("mocha/config") << "Error writing clip configuration";
			return false;
		}
	}
} // namespace mocha
