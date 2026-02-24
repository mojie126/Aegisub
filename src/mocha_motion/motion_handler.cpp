// Copyright (c) 2024-2026, Aegisub contributors
// 运动应用引擎实现
// 对应 MoonScript 版 a-mo.MotionHandler

#include "motion_handler.h"
#include "motion_math.h"
#include "motion_tags.h"
#include "motion_transform.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>

namespace mocha {
	MotionHandler::MotionHandler(const MotionOptions &options,
								DataHandler *main_data,
								DataHandler *rect_clip_data,
								DataHandler *vect_clip_data)
		: main_data_(main_data)
		, rect_clip_data_(rect_clip_data)
		, vect_clip_data_(vect_clip_data)
		, options_(options) {
		setup_callbacks();
	}

/// @brief 根据用户选项配置标签回调列表
///
/// 回调架构说明：
///   - callbacks_ 是一个 {正则模式, 回调函数} 的有序列表
///   - 在 apply_callbacks() 中，对文本逐个匹配正则并调用回调函数替换值
///   - 回调函数接收标签的当前值和帧号，返回计算后的新值
///   - 顺序重要：位置回调必须在 clip 回调之前，因为绝对位置模式下
///     位置回调会计算 x_delta_/y_delta_，clip 回调会使用这些偏移量
///
/// 回调配置根据 MotionOptions 决定启用哪些：
///   - clip_only=false 时才启用位置/缩放/旋转回调
///   - 位置：abs_pos 决定使用绝对位置回调还是相对位置回调
///   - 缩放：启用 x_scale 后还可附带 border/shadow/blur
///   - clip：取决于 rect_clip_data_/vect_clip_data_ 是否提供
	void MotionHandler::setup_callbacks() {
		callbacks_.clear();

		// 对应 MoonScript: unless 'SRS' == mainData.type or @options.main.clipOnly
		// SRS 类型仅包含矢量绘图数据，不含位置/缩放/旋转信息
		if (!options_.clip_only && !main_data_->is_srs()) {
			const bool need_pos = options_.x_position || options_.y_position || options_.x_scale || options_.z_rotation;

			if (need_pos) {
				if (options_.abs_pos) {
					// 绝对位置模式
					callbacks_.push_back(
						{
							R"((\\pos)\(([-.0-9]+,[-.0-9]+)\))",
							std::regex(R"((\\pos)\(([-.0-9]+,[-.0-9]+)\))"),
							[this](const std::string &v, int f) { return cb_absolute_position(v, f); }
						}
					);
				} else {
					// 相对位置模式
					callbacks_.push_back(
						{
							R"((\\pos)\(([-.0-9]+,[-.0-9]+)\))",
							std::regex(R"((\\pos)\(([-.0-9]+,[-.0-9]+)\))"),
							[this](const std::string &v, int f) { return cb_position(v, f); }
						}
					);
				}
			}

			if (options_.origin) {
				callbacks_.push_back(
					{
						R"((\\org)\(([-.0-9]+,[-.0-9]+)\))",
						std::regex(R"((\\org)\(([-.0-9]+,[-.0-9]+)\))"),
						[this](const std::string &v, int f) { return cb_origin(v, f); }
					}
				);
			}

			if (options_.x_scale) {
				// \fscx, \fscy
				callbacks_.push_back(
					{
						R"((\\fsc[xy])([.0-9]+))",
						std::regex(R"((\\fsc[xy])([.0-9]+))"),
						[this](const std::string &v, int f) { return cb_scale(v, f); }
					}
				);

				if (options_.border) {
					// \bord, \xbord, \ybord
					callbacks_.push_back(
						{
							R"((\\[xy]?bord)([.0-9]+))",
							std::regex(R"((\\[xy]?bord)([.0-9]+))"),
							[this](const std::string &v, int f) { return cb_scale(v, f); }
						}
					);
				}

				if (options_.shadow) {
					// \shad, \xshad, \yshad
					callbacks_.push_back(
						{
							R"((\\[xy]?shad)([-.0-9]+))",
							std::regex(R"((\\[xy]?shad)([-.0-9]+))"),
							[this](const std::string &v, int f) { return cb_scale(v, f); }
						}
					);
				}

				if (options_.blur) {
					// \blur
					callbacks_.push_back(
						{
							R"((\\blur)([.0-9]+))",
							std::regex(R"((\\blur)([.0-9]+))"),
							[this](const std::string &v, int f) { return cb_blur(v, f); }
						}
					);
				}
			}

			if (options_.x_rotation) {
				// \frx
				callbacks_.push_back(
					{
						R"((\\frx)([-.0-9]+))",
						std::regex(R"((\\frx)([-.0-9]+))"),
						[this](const std::string &v, int f) { return cb_rotate_x(v, f); }
					}
				);
			}

			if (options_.y_rotation) {
				// \fry
				callbacks_.push_back(
					{
						R"((\\fry)([-.0-9]+))",
						std::regex(R"((\\fry)([-.0-9]+))"),
						[this](const std::string &v, int f) { return cb_rotate_y(v, f); }
					}
				);
			}

			if (options_.z_rotation) {
				// \frz 或 \fr
				callbacks_.push_back(
					{
						R"((\\frz|\\fr)([-.0-9]+))",
						std::regex(R"((\\frz|\\fr)([-.0-9]+))"),
						[this](const std::string &v, int f) { return cb_rotate_z(v, f); }
					}
				);
			}

			if (options_.z_position) {
				// \z 深度
				callbacks_.push_back(
					{
						R"((\\z)([-.0-9]+))",
						std::regex(R"((\\z)([-.0-9]+))"),
						[this](const std::string &v, int f) { return cb_z_position(v, f); }
					}
				);
			}
		}

		// 矩形 clip 回调：SRS 数据不支持矩形 clip
		if (rect_clip_data_ && !rect_clip_data_->is_srs()) {
			callbacks_.push_back(
				{
					R"((\\i?clip)\(([-.0-9]+,[-.0-9]+,[-.0-9]+,[-.0-9]+)\))",
					std::regex(R"((\\i?clip)\(([-.0-9]+,[-.0-9]+,[-.0-9]+,[-.0-9]+)\))"),
					[this](const std::string &v, int f) { return cb_rect_clip(v, f); }
				}
			);
		}

		// 矢量 clip 回调：根据数据类型选择回调
		if (vect_clip_data_) {
			if (vect_clip_data_->is_srs()) {
				// SRS 模式：直接替换为预生成的绘图字符串
				callbacks_.push_back(
					{
						R"((\\i?clip)\(([^,]+)\))",
						std::regex(R"((\\i?clip)\(([^,]+)\))"),
						[this](const std::string &v, int f) { return cb_vect_clip_srs(v, f); }
					}
				);
			} else {
				// TSR 模式：逐坐标 positionMath 变换
				callbacks_.push_back(
					{
						R"((\\i?clip)\(([^,]+)\))",
						std::regex(R"((\\i?clip)\(([^,]+)\))"),
						[this](const std::string &v, int f) { return cb_vect_clip(v, f); }
					}
				);
			}
		}
	}

	std::vector<MotionLine> MotionHandler::apply_motion(
		std::vector<MotionLine> &lines,
		int collection_start_frame,
		std::function<int(int)> frame_from_ms,
		std::function<int(int)> ms_from_frame) {
		std::vector<MotionLine> result;

		for (auto &line : lines) {
			// 计算行相对追踪数据的起止帧
			int start_frame = frame_from_ms(line.start_time);
			int end_frame = frame_from_ms(line.end_time);

			// 相对于行集合起始帧的偏移（从1开始）
			line.relative_start = start_frame - collection_start_frame + 1;
			line.relative_end = end_frame - collection_start_frame;

			// 选择处理模式
			// 对应 MoonScript: linear and not (origin and hasOrg) and not ((clipData) and hasClip)
			bool use_linear = options_.linear
							&& !(options_.origin && line.has_org)
							&& !(line.has_clip && (rect_clip_data_ || vect_clip_data_));

			if (use_linear) {
				apply_linear(line, collection_start_frame, frame_from_ms, ms_from_frame, result);
			} else {
				apply_nonlinear(line, collection_start_frame, ms_from_frame, result);
			}
		}

		return result;
	}

	void MotionHandler::apply_linear(MotionLine &line,
									int collection_start_frame,
									std::function<int(int)> &frame_from_ms,
									std::function<int(int)> &ms_from_frame,
									std::vector<MotionLine> &result) {
		// 对应 MoonScript linear()
		// 计算 \t 的起止时间
		const int start_frame_abs = frame_from_ms(line.start_time);
		const int start_frame_time = ms_from_frame(start_frame_abs);
		const int frame_after_start = ms_from_frame(start_frame_abs + 1);
		const int frame_before_end_abs = frame_from_ms(line.end_time) - 1;
		const int frame_before_end_time = ms_from_frame(frame_before_end_abs);
		const int end_frame_time = ms_from_frame(frame_before_end_abs + 1);

		// 从首个字幕帧中点到行起始时间的偏移
		const int begin_time = static_cast<int>(std::floor(0.5 * (start_frame_time + frame_after_start))) - line.start_time;
		// 从最后字幕帧中点到行起始时间的偏移
		const int end_time = static_cast<int>(std::floor(0.5 * (frame_before_end_time + end_frame_time))) - line.start_time;

		std::string text = line.text;

		// 处理 \move（如果行本身有 \move，先插值为 \pos）
		// 对应 MoonScript: moveStartTime = msFromFrame(frame - 1) + data.t1
		// move.t1/t2 是相对于行起始时间的偏移，需转换为与 line.start_time 可比较的绝对时间
		if (line.move.has_value()) {
			const auto &move = line.move.value();
			// 对应 MoonScript: msFromFrame(frame - 1)，即首帧之前的帧时间
			const int prev_frame_time = ms_from_frame(start_frame_abs > 0 ? start_frame_abs - 1 : 0);
			const double move_start_time = prev_frame_time + move.t1;
			const double move_end_time = prev_frame_time + move.t2;
			double progress = 0;
			if (move_end_time != move_start_time)
				progress = (line.start_time - move_start_time) / (move_end_time - move_start_time);
			progress = std::max(0.0, std::min(1.0, progress));
			const double px = move.x1 + (move.x2 - move.x1) * progress;
			const double py = move.y1 + (move.y2 - move.y1) * progress;

			const std::regex move_re(R"(\\move\([^)]+\))");
			char buf[64];
			std::snprintf(buf, sizeof(buf), "\\pos(%g,%g)", math::round(px, 2), math::round(py, 2));
			text = std::regex_replace(text, move_re, buf);
		}

		// 对每个回调，计算首尾帧的值并生成 \t
		text = apply_callbacks_linear(text, line.relative_start, line.relative_end, begin_time, end_time);

		// 将 \pos(x1,y1)\t(t1,t2,\pos(x2,y2)) 合并为 \move(x1,y1,x2,y2,t1,t2)
		if (options_.x_position || options_.y_position) {
			std::regex pos_t_re(R"(\\pos\(([^)]+)\)\\t\((\d+,\d+),\\pos\(([^)]+)\)\))");
			text = std::regex_replace(text, pos_t_re, "\\move($1,$3,$2)");
		}

		MotionLine new_line = line;
		new_line.text = text;
		new_line.transforms_tokenized = false;
		new_line.was_linear = true;
		result.push_back(std::move(new_line));
	}

/// @brief 非线性模式：逐帧生成独立字幕行
///
/// 对应 MoonScript nonlinear()。
/// 核心流程：
///   1. 逆序遍历帧（从 relativeEnd 到 relativeStart，保证插入到事件列表后为正序）
///   2. 每帧计算精确的起止时间（10ms 对齐，避免渲染空隙）
///   3. 处理变换标签：kill_trans=true 时逐帧插值 \t，否则仅偏移时间
///   4. 处理 \fade：转换为逐帧 \alpha 值或偏移 \fade 时间参数
///   5. 处理 \move：插值为当前帧的 \pos
///   6. 计算追踪数据当前帧状态（position_math 依赖此状态）
///   7. 应用全部回调（位置/缩放/旋转/clip 标签替换）
///   8. 生成新行并设置卡拉OK偏移量
///
/// @param line 原始行（已预处理，包含 tokenized 变换标签）
/// @param collection_start_frame 行集合的绝对起始帧号
/// @param ms_from_frame 帧号→毫秒转换函数
/// @param result 输出行集合（追加到末尾）
	void MotionHandler::apply_nonlinear(const MotionLine &line,
										int collection_start_frame,
										std::function<int(int)> &ms_from_frame,
										std::vector<MotionLine> &result) {
		// 对应 MoonScript nonlinear()
		// 逆序遍历帧（MoonScript 从 relativeEnd 到 relativeStart）保证插入顺序
		int rel_start = line.relative_start;
		int rel_end = line.relative_end;

		for (int frame = rel_end; frame >= rel_start; --frame) {
			// 计算新行的起止时间（10ms 对齐）
			int raw_start_ms = ms_from_frame(collection_start_frame + frame - 1);
			int raw_end_ms = ms_from_frame(collection_start_frame + frame);

			int new_start_time = static_cast<int>(std::floor(std::max(0, raw_start_ms) / 10.0)) * 10;
			int new_end_time = static_cast<int>(std::floor(std::max(0, raw_end_ms) / 10.0)) * 10;

			// 当前帧相对于行首帧的时间偏移
			int first_frame_ms = ms_from_frame(collection_start_frame + rel_start - 1);
			int time_delta = new_start_time - static_cast<int>(std::floor(std::max(0, first_frame_ms) / 10.0)) * 10;

			// 生成新文本（处理变换标签）
			std::string new_text;
			int new_line_duration = new_end_time - new_start_time;
			if (options_.kill_trans) {
				new_text = line.interpolate_transforms_copy(
					time_delta, new_start_time,
					0, 0 // res_x, res_y 仅在 clip 插值时使用
				);
			} else {
				// 传递新行持续时间，用于抑制超出行范围的 \t 标签
				new_text = line.detokenize_transforms_copy(time_delta, new_line_duration);
			}

			// 处理 \fade 标签
			if (options_.kill_trans) {
				// 提取并移除 \fade，转换为 \alpha
				std::regex fade_re(R"(\\fade\(([^)]+)\))");
				std::smatch fade_match;
				std::optional<FullFadeData> fade;

				// 遍历所有标签块，提取并移除 \fade
				new_text = tag_utils::run_callback_on_overrides(
					new_text, [&](const std::string &block, int) {
						std::string result_block = block;
						std::smatch m;
						if (std::regex_search(result_block, m, fade_re)) {
							// 解析 \fade(a1,a2,a3,t1,t2,t3,t4)
							std::string vals = m[1].str();
							std::istringstream iss(vals);
							std::string token;
							std::vector<int> parts;
							while (std::getline(iss, token, ',')) {
								try { parts.push_back(std::stoi(token)); } catch (...) { break; }
							}
							if (parts.size() == 7) {
								fade = FullFadeData{
									parts[0], parts[1], parts[2],
									parts[3], parts[4], parts[5], parts[6]
								};
							}
							result_block = std::regex_replace(result_block, fade_re, "");
						}
						return result_block;
					}
				);

				if (fade.has_value()) {
					// 计算当前帧的淡入淡出因子
					// 对应 MoonScript 分段函数
					const auto &f = fade.value();
					double fade_factor;
					if (time_delta < f.t1)
						fade_factor = static_cast<double>(f.a1);
					else if (time_delta < f.t2)
						fade_factor = f.a1 + (f.a2 - f.a1) * static_cast<double>(time_delta - f.t1) / (f.t2 - f.t1);
					else if (time_delta < f.t3)
						fade_factor = static_cast<double>(f.a2);
					else if (time_delta < f.t4)
						fade_factor = f.a2 + (f.a3 - f.a2) * static_cast<double>(time_delta - f.t3) / (f.t4 - f.t3);
					else
						fade_factor = static_cast<double>(f.a3);

					// 不透明度因子 (0-1)
					double opacity = (255.0 - fade_factor) / 255.0;

					// 对所有 alpha 标签应用不透明度
					std::regex alpha_re(R"((\\[1234]?a(?:lpha)?)&H([0-9A-Fa-f]{2})&)");
					new_text = tag_utils::run_callback_on_overrides(
						new_text, [&](const std::string &block, int) {
							std::string result_block;
							std::sregex_iterator it(block.begin(), block.end(), alpha_re);
							std::sregex_iterator end;
							size_t last = 0;
							while (it != end) {
								const auto &m = *it;
								result_block += block.substr(last, m.position() - last);

								std::string alpha_tag = m[1].str();
								int alpha_val = std::stoi(m[2].str(), nullptr, 16);
								int new_alpha = math::round(255.0 - (opacity * (255.0 - alpha_val)));
								new_alpha = std::max(0, std::min(255, new_alpha));

								char buf[32];
								std::snprintf(buf, sizeof(buf), "%s&H%02X&", alpha_tag.c_str(), new_alpha);
								result_block += buf;

								last = m.position() + m.length();
								++it;
							}
							result_block += block.substr(last);
							return result_block;
						}
					);
				}
			} else {
				// 不插值变换时，仅调整 \fade 标签的时间参数
				std::regex fade_re(R"(\\fade\(([^)]+)\))");
				new_text = tag_utils::run_callback_on_overrides(
					new_text, [&](const std::string &block, int) {
						std::string result_block = block;
						std::smatch m;
						if (std::regex_search(result_block, m, fade_re)) {
							std::string vals = m[1].str();
							std::istringstream iss(vals);
							std::string token;
							std::vector<int> parts;
							while (std::getline(iss, token, ',')) {
								try { parts.push_back(std::stoi(token)); } catch (...) { break; }
							}
							if (parts.size() == 7) {
								// 偏移 t1-t4 参数
								for (int i = 3; i < 7; ++i) {
									parts[i] -= time_delta;
								}
								char buf[128];
								std::snprintf(
									buf, sizeof(buf), "\\fade(%d,%d,%d,%d,%d,%d,%d)",
									parts[0], parts[1], parts[2],
									parts[3], parts[4], parts[5], parts[6]
								);
								result_block = std::regex_replace(result_block, fade_re, buf);
							}
						}
						return result_block;
					}
				);
			}

			// 处理 \move（插值为 \pos）
			if (line.move.has_value()) {
				const auto &move = line.move.value();
				double progress = 0;
				if (move.t2 != move.t1)
					progress = static_cast<double>(time_delta - move.t1) / (move.t2 - move.t1);
				progress = std::max(0.0, std::min(1.0, progress));
				double px = move.x1 + (move.x2 - move.x1) * progress;
				double py = move.y1 + (move.y2 - move.y1) * progress;

				std::regex move_re(R"(\\move\([^)]+\))");
				char buf[64];
				std::snprintf(buf, sizeof(buf), "\\pos(%g,%g)", math::round(px, 2), math::round(py, 2));
				new_text = std::regex_replace(new_text, move_re, buf);
			}

			// 计算当前帧的追踪状态
			main_data_->calculate_current_state(frame);

			// 应用所有回调
			new_text = apply_callbacks(new_text, frame);

			// 生成新行
			MotionLine new_line = line;
			new_line.text = new_text;
			new_line.start_time = new_start_time;
			new_line.end_time = new_end_time;
			new_line.transforms_tokenized = false;
			new_line.karaoke_shift = (new_start_time - line.start_time) * 0.1;
			result.push_back(std::move(new_line));
		}
	}

	std::string MotionHandler::apply_callbacks(const std::string &text, int frame) {
		std::string result = text;

		for (const auto &entry : callbacks_) {
			std::string temp;
			std::sregex_iterator it(result.begin(), result.end(), entry.pattern);
			std::sregex_iterator end;
			size_t last = 0;

			while (it != end) {
				const auto &m = *it;
				temp += result.substr(last, m.position() - last);

				// m[1] = 标签名，m[2] = 值
				std::string tag = m[1].str();
				std::string value = m[2].str();
				std::string new_value = entry.callback(value, frame);

				temp += tag + new_value;
				last = m.position() + m.length();
				++it;
			}
			temp += result.substr(last);
			result = std::move(temp);
		}

		return result;
	}

	std::string MotionHandler::apply_callbacks_linear(const std::string &text,
													int start_frame, int end_frame,
													int begin_time, int end_time) {
		std::string result = text;

		// 保存起始帧/结束帧各自的位置偏移量，防止回调间状态污染
		// 绝对位置模式下 cb_absolute_position 会设置 x_delta_/y_delta_，
		// 后续 clip 回调需要使用对应帧的偏移量而非上一个回调遗留的值
		double saved_start_x_delta = 0, saved_start_y_delta = 0;
		double saved_end_x_delta = 0, saved_end_y_delta = 0;

		for (const auto &entry : callbacks_) {
			std::string temp;
			std::sregex_iterator it(result.begin(), result.end(), entry.pattern);
			std::sregex_iterator end_it;
			size_t last = 0;

			while (it != end_it) {
				const auto &m = *it;
				temp += result.substr(last, m.position() - last);

				std::string tag = m[1].str();
				std::string value = m[2].str();

				// 恢复起始帧偏移量再计算起始帧值
				x_delta_ = saved_start_x_delta;
				y_delta_ = saved_start_y_delta;
				main_data_->calculate_current_state(start_frame);
				std::string start_value = entry.callback(value, start_frame);
				saved_start_x_delta = x_delta_;
				saved_start_y_delta = y_delta_;

				// 恢复结束帧偏移量再计算结束帧值
				x_delta_ = saved_end_x_delta;
				y_delta_ = saved_end_y_delta;
				main_data_->calculate_current_state(end_frame);
				std::string end_value = entry.callback(value, end_frame);
				saved_end_x_delta = x_delta_;
				saved_end_y_delta = y_delta_;

				// 生成 tag+startVal+\t(beginTime,endTime,tag+endVal)
				char buf[256];
				std::snprintf(
					buf, sizeof(buf), "%s%s\\t(%d,%d,%s%s)",
					tag.c_str(), start_value.c_str(),
					begin_time, end_time,
					tag.c_str(), end_value.c_str()
				);
				temp += buf;

				last = m.position() + m.length();
				++it;
			}
			temp += result.substr(last);
			result = std::move(temp);
		}

		return result;
	}

// --- 回调函数实现 ---

/// @brief 位置数学核心算法 - 坐标变换（旋转补偿 + 缩放）
///
/// 对应 MoonScript positionMath()。
/// 算法原理：
///   1. 计算当前坐标 (x, y) 相对于追踪数据起始位置 (start_x, start_y) 的偏移量
///   2. 将偏移量乘以缩放比例 (x_ratio, y_ratio)
///   3. 转换为极坐标 (radius, alpha)
///   4. 减去旋转差值 (z_rotation_diff)，实现旋转补偿
///   5. 转换回直角坐标并加上追踪数据当前位置
///
/// 数学公式：
///   dx = (x - x_start) * x_ratio
///   dy = (y - y_start) * y_ratio
///   r  = sqrt(dx² + dy²)
///   α  = atan2(dy, dx)
///   new_x = x_current + r * cos(α - Δθ)
///   new_y = y_current + r * sin(α - Δθ)
///
/// @param x 原始 X 坐标（可来自 \pos、\org 等标签）
/// @param y 原始 Y 坐标
/// @param data 追踪数据（包含起始/当前位置、缩放比、旋转差值）
/// @return 变换后的 (new_x, new_y) 坐标对
	std::pair<double, double> MotionHandler::position_math(double x, double y, DataHandler *data) {
		// 对应 MoonScript positionMath()
		// 相对起始位置的偏移，乘以缩放比例
		double dx = (x - data->x_start_position) * data->x_ratio;
		double dy = (y - data->y_start_position) * data->y_ratio;

		// 极坐标变换
		double radius = std::sqrt(dx * dx + dy * dy);
		double alpha = math::d_atan(dy, dx);

		// 旋转补偿后的新坐标
		double new_x = data->x_current_position + radius * math::d_cos(alpha - data->z_rotation_diff);
		double new_y = data->y_current_position + radius * math::d_sin(alpha - data->z_rotation_diff);

		return {new_x, new_y};
	}

	std::string MotionHandler::cb_position(const std::string &value, int frame) {
		// 对应 MoonScript position()
		// 解析 "x,y"
		std::regex xy_re(R"(([-.0-9]+),([-.0-9]+))");
		std::smatch m;
		if (!std::regex_search(value, m, xy_re))
			return "(" + value + ")";

		double x = std::stod(m[1].str());
		double y = std::stod(m[2].str());

		auto [new_x, new_y] = position_math(x, y, main_data_);

		char buf[64];
		std::snprintf(buf, sizeof(buf), "(%g,%g)", math::round(new_x, 2), math::round(new_y, 2));
		return buf;
	}

	std::string MotionHandler::cb_absolute_position(const std::string &value, int frame) {
		// 对应 MoonScript absolutePosition()
		std::regex xy_re(R"(([-.0-9]+),([-.0-9]+))");
		std::smatch m;
		if (!std::regex_search(value, m, xy_re))
			return "(" + value + ")";

		double x = std::stod(m[1].str());
		double y = std::stod(m[2].str());

		// 位置直接使用追踪数据
		// frame 是 1-based 帧号，C++ vector 是 0-indexed，需要 frame - 1
		int idx = frame - 1;
		if (idx < 0) idx = 0;
		if (idx >= static_cast<int>(main_data_->x_position.size()))
			idx = static_cast<int>(main_data_->x_position.size()) - 1;

		x_delta_ = main_data_->x_position[idx] - x;
		y_delta_ = main_data_->y_position[idx] - y;

		char buf[64];
		std::snprintf(
			buf, sizeof(buf), "(%g,%g)",
			math::round(main_data_->x_position[idx], 2),
			math::round(main_data_->y_position[idx], 2)
		);
		return buf;
	}

	std::string MotionHandler::cb_origin(const std::string &value, int frame) {
		// 对应 MoonScript origin()
		std::regex xy_re(R"(([-.0-9]+),([-.0-9]+))");
		std::smatch m;
		if (!std::regex_search(value, m, xy_re))
			return "(" + value + ")";

		double ox = std::stod(m[1].str());
		double oy = std::stod(m[2].str());

		auto [new_ox, new_oy] = position_math(ox, oy, main_data_);

		char buf[64];
		std::snprintf(buf, sizeof(buf), "(%g,%g)", math::round(new_ox, 2), math::round(new_oy, 2));
		return buf;
	}

	std::string MotionHandler::cb_scale(const std::string &value, int frame) {
		// 对应 MoonScript scale()
		// scale *= xRatio
		double scale_val;
		try { scale_val = std::stod(value); } catch (...) { return value; }
		scale_val *= main_data_->x_ratio;

		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", math::round(scale_val, 2));
		return buf;
	}

	std::string MotionHandler::cb_blur(const std::string &value, int frame) {
		// 对应 MoonScript blur()
		// ratio = 1 - (1 - xRatio) * blurScale
		// result = blur * ratio
		double blur_val;
		try { blur_val = std::stod(value); } catch (...) { return value; }
		double ratio = main_data_->x_ratio;
		ratio = 1.0 - (1.0 - ratio) * options_.blur_scale;
		blur_val *= ratio;

		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", math::round(blur_val, 2));
		return buf;
	}

	std::string MotionHandler::cb_rotate_x(const std::string &value, int frame) {
		double rot;
		try { rot = std::stod(value); } catch (...) { return value; }
		rot += main_data_->x_rotation_diff;

		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", math::round(rot, 2));
		return buf;
	}

	std::string MotionHandler::cb_rotate_y(const std::string &value, int frame) {
		double rot;
		try { rot = std::stod(value); } catch (...) { return value; }
		rot += main_data_->y_rotation_diff;
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", math::round(rot, 2));
		return buf;
	}

	std::string MotionHandler::cb_rotate_z(const std::string &value, int frame) {
		double rot;
		try { rot = std::stod(value); } catch (...) { return value; }
		rot += main_data_->z_rotation_diff;

		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", math::round(rot, 2));
		return buf;
	}

	std::string MotionHandler::cb_z_position(const std::string &value, int frame) {
		// 3D 深度：\z += zPositionDiff
		double z;
		try { z = std::stod(value); } catch (...) { return value; }
		z += main_data_->z_position_diff;

		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", math::round(z, 2));
		return buf;
	}

	std::string MotionHandler::cb_rect_clip(const std::string &value, int frame) {
		// 对应 MoonScript rectangularClip()
		if (rect_clip_data_) {
			rect_clip_data_->calculate_current_state(frame);
			// 矩形 clip 不使用旋转
			double saved_rot = rect_clip_data_->z_rotation_diff;
			rect_clip_data_->z_rotation_diff = 0;

			// 替换每对坐标
			std::regex coord_re(R"(([-.0-9]+),([-.0-9]+))");
			std::string result;
			std::sregex_iterator it(value.begin(), value.end(), coord_re);
			std::sregex_iterator end;
			size_t last = 0;

			while (it != end) {
				const auto &m = *it;
				result += value.substr(last, m.position() - last);

				double x = std::stod(m[1].str()) + x_delta_;
				double y = std::stod(m[2].str()) + y_delta_;
				auto [new_x, new_y] = position_math(x, y, rect_clip_data_);

				char buf[64];
				std::snprintf(buf, sizeof(buf), "%g,%g", math::round(new_x, 2), math::round(new_y, 2));
				result += buf;

				last = m.position() + m.length();
				++it;
			}
			result += value.substr(last);

			rect_clip_data_->z_rotation_diff = saved_rot;
			return "(" + result + ")";
		}

		return "(" + value + ")";
	}

	std::string MotionHandler::cb_vect_clip(const std::string &value, int frame) {
		// 对应 MoonScript vectorClip()
		if (vect_clip_data_) {
			vect_clip_data_->calculate_current_state(frame);

			// 替换矢量 clip 中的每对空格分隔的坐标
			std::regex coord_re(R"(([-.0-9]+) ([-.0-9]+))");
			std::string result;
			std::sregex_iterator it(value.begin(), value.end(), coord_re);
			std::sregex_iterator end;
			size_t last = 0;

			while (it != end) {
				const auto &m = *it;
				result += value.substr(last, m.position() - last);

				double x = std::stod(m[1].str()) + x_delta_;
				double y = std::stod(m[2].str()) + y_delta_;
				auto [new_x, new_y] = position_math(x, y, vect_clip_data_);

				char buf[64];
				std::snprintf(buf, sizeof(buf), "%g %g", math::round(new_x, 2), math::round(new_y, 2));
				result += buf;

				last = m.position() + m.length();
				++it;
			}
			result += value.substr(last);

			return "(" + result + ")";
		}

		return "(" + value + ")";
	}

	std::string MotionHandler::cb_vect_clip_srs(const std::string &value, int frame) {
		// 对应 MoonScript vectorClipSRS()
		// SRS 数据追加到原始 clip 内容之后（原始 clip 绘图保留）
		// MoonScript: return '(' .. clip .. ' ' .. @vectClipData.data[frame]\sub(1,-2) .. ')'
		if (vect_clip_data_ && vect_clip_data_->is_srs()) {
			std::string drawing = vect_clip_data_->get_srs_drawing(frame);
			if (!drawing.empty()) {
				// 对应 MoonScript \sub(1,-2)：移除最后一个字符（通常为换行或空格）
				if (!drawing.empty()) {
					drawing.pop_back();
				}
				// 将 SRS 绘图数据追加到原始 clip 内容之后
				return "(" + value + " " + drawing + ")";
			}
		}
		return "(" + value + ")";
	}
} // namespace mocha
