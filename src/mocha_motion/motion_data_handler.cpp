// Copyright (c) 2024-2026, Aegisub contributors
// 运动追踪数据解析器实现
// 对应 MoonScript 版 a-mo.DataHandler

#include "motion_data_handler.h"

#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace mocha {

/// @brief 解析 AE 关键帧格式的追踪数据
///
/// AE 关键帧数据格式说明：
///   第1行: "Adobe After Effects 6.0 Keyframe Data"（版本标识）
///   接下来几行: 元数据（Units Per Second, Source Width, Source Height 等）
///   然后按段落分为: Position, Scale, Rotation 三个数据段
///   每段的数据行以制表符缩进，格式: 帧号 值1 [值2 [值3]]
///
/// 坐标缩放：
///   AE 导出的坐标基于源视频分辨率，需要缩放到脚本分辨率
///   x_pos_scale_ = script_res_x / source_width
///   y_pos_scale_ = script_res_y / source_height
///
/// @param raw_data AE 导出的文本数据
/// @param script_res_x 脚本水平分辨率（ASS PlayResX）
/// @param script_res_y 脚本垂直分辨率（ASS PlayResY）
/// @return 解析是否成功
bool DataHandler::parse(const std::string& raw_data, int script_res_x, int script_res_y) {
	tableize(raw_data);

	if (raw_lines_.empty()) return false;

	// 验证数据格式：首行应包含 "Adobe After Effects" 和 "Keyframe Data"
	if (raw_lines_[0].find("Adobe After Effects") == std::string::npos ||
		raw_lines_[0].find("Keyframe Data") == std::string::npos) {
		return false;
	}

	type_ = DataType::AE_KEYFRAME;

	// 解析 Source Width 和 Source Height
	if (raw_lines_.size() < 4) return false;

	// 从第3、4行提取源视频尺寸
	for (size_t i = 1; i < raw_lines_.size() && i < 10; ++i) {
		const auto& line = raw_lines_[i];
		if (line.find("Source Width") != std::string::npos) {
			auto sep_pos = line.find_last_of("\t ");
			if (sep_pos != std::string::npos) {
				source_width_ = std::stoi(line.substr(sep_pos + 1));
			}
		} else if (line.find("Source Height") != std::string::npos) {
			auto sep_pos = line.find_last_of("\t ");
			if (sep_pos != std::string::npos) {
				source_height_ = std::stoi(line.substr(sep_pos + 1));
			}
		} else if (line.find("Units Per Second") != std::string::npos) {
			auto sep_pos = line.find_last_of("\t ");
			if (sep_pos != std::string::npos) {
				frame_rate_ = std::stod(line.substr(sep_pos + 1));
			}
		}
	}

	if (source_width_ == 0 || source_height_ == 0) return false;

	x_pos_scale_ = static_cast<double>(script_res_x) / source_width_;
	y_pos_scale_ = static_cast<double>(script_res_y) / source_height_;

	parse_sections();

	// 验证所有数据数组长度一致
	if (length_ == 0 ||
		static_cast<int>(x_position.size()) != length_ ||
		static_cast<int>(y_position.size()) != length_ ||
		static_cast<int>(z_position.size()) != length_ ||
		static_cast<int>(x_scale.size()) != length_ ||
		static_cast<int>(y_scale.size()) != length_ ||
		static_cast<int>(x_rotation.size()) != length_ ||
		static_cast<int>(y_rotation.size()) != length_ ||
		static_cast<int>(z_rotation.size()) != length_) {
		return false;
	}

	return true;
}

bool DataHandler::parse_file(const std::string& file_path, int script_res_x, int script_res_y) {
	// 对应 MoonScript DataHandler.parseFile
	// 尝试将输入作为文件路径打开并读取内容
	// 对应 MoonScript: 支持带引号的路径 "C:\path\file.txt" → C:\path\file.txt
	std::string path = file_path;
	if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
		path = path.substr(1, path.size() - 2);
	}
	std::ifstream file(path);
	if (!file.is_open()) return false;

	std::string content((std::istreambuf_iterator<char>(file)),
	                     std::istreambuf_iterator<char>());
	file.close();

	if (content.empty()) return false;

	return parse(content, script_res_x, script_res_y);
}

void DataHandler::tableize(const std::string& raw_data) {
	raw_lines_.clear();
	std::istringstream stream(raw_data);
	std::string line;
	while (std::getline(stream, line)) {
		// 去除行尾的 \r
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (!line.empty()) {
			raw_lines_.push_back(line);
		}
	}
}

/// @brief 解析数据段落，提取 Position/Scale/Rotation 数值
///
/// 段落识别规则：
///   - 不以空白字符开头的行为段落标题（Position/Scale/Rotation）
///   - 以制表符或空格开头的行为数据行
///   - Position 段: 帧号 X Y [Z] → 缩放后存入 x_position/y_position
///   - Scale 段: 帧号 缩放值 → 存入 x_scale（y_scale 共享同一数组）
///   - Rotation 段: 帧号 旋转值 → 取反后存入 x/y/z_rotation
///     （AE 的旋转方向与 ASS 相反，需要取反）
///
/// 长度以 Position 段的数据行数为准
void DataHandler::parse_sections() {
	// 初始化数据数组
	x_position.clear();
	y_position.clear();
	z_position.clear();
	x_scale.clear();
	y_scale.clear();
	x_rotation.clear();
	y_rotation.clear();
	z_rotation.clear();

	int length = 0;
	int section = 0; // 0=无, 1=Position, 2=Scale, 3=XRot, 4=YRot, 5=ZRot

	for (const auto& line : raw_lines_) {
		// 检查是否是段落标题（不以空白开头）
		if (!line.empty() && line[0] != '\t' && line[0] != ' ') {
			if (line == "Position") {
				section = 1;
			} else if (line == "Scale") {
				section = 2;
			} else if (line == "X Rotation") {
				section = 3;
			} else if (line == "Y Rotation") {
				section = 4;
			} else if (line == "Rotation" || line == "Z Rotation") {
				section = 5;
			} else {
				section = 0;
			}
		} else {
			// 数据行（以制表符或空格开头）
			if (section == 0) continue;

			std::istringstream ss(line);
			double ignored_frame = 0;
			if (!(ss >> ignored_frame)) continue;
			(void)ignored_frame;

			std::vector<double> values;
			double value = 0;
			while (ss >> value)
				values.push_back(value);

			if (values.empty())
				continue;

			switch (section) {
				case 1: // Position
					x_position.push_back(x_pos_scale_ * values[0]);
					y_position.push_back(y_pos_scale_ * (values.size() >= 2 ? values[1] : 0.0));
					z_position.push_back(values.size() >= 3 ? values[2] : 0.0);
					++length;
					break;
				case 2: // Scale
					x_scale.push_back(values[0]);
					y_scale.push_back(values.size() >= 2 ? values[1] : values[0]);
					break;
				case 3: // X Rotation
					// AE 导出的旋转值需要取反
					x_rotation.push_back(-values[0]);
					break;
				case 4: // Y Rotation
					// AE 导出的旋转值需要取反
					y_rotation.push_back(-values[0]);
					break;
				case 5: // Z Rotation
					// AE 导出的旋转值需要取反
					z_rotation.push_back(-values[0]);
					break;
			}
		}
	}

	length_ = length;

	auto fill_to_length = [length](std::vector<double>& vec, double value) {
		if (static_cast<int>(vec.size()) < length) {
			vec.resize(length, value);
		}
	};

	fill_to_length(z_position, 0.0);
	fill_to_length(x_scale, 100.0);
	if (static_cast<int>(y_scale.size()) < length) {
		size_t old_size = y_scale.size();
		y_scale.resize(length, 100.0);
		for (size_t i = old_size; i < y_scale.size() && i < x_scale.size(); ++i) {
			y_scale[i] = x_scale[i];
		}
	}
	fill_to_length(x_rotation, 0.0);
	fill_to_length(y_rotation, 0.0);
	fill_to_length(z_rotation, 0.0);
}

void DataHandler::add_reference_frame(int frame) {
	if (is_srs()) return; // SRS 数据无需参考帧
	if (frame < 1 || frame > length_) return;

	start_frame = frame;
	// 数组索引从0开始，帧号从1开始
	int idx = frame - 1;
	x_start_position = x_position[idx];
	y_start_position = y_position[idx];
	z_start_position = z_position[idx];
	x_start_rotation = x_rotation[idx];
	y_start_rotation = y_rotation[idx];
	z_start_rotation = z_rotation[idx];
	x_start_scale = x_scale[idx];
	y_start_scale = y_scale[idx];
}

void DataHandler::calculate_current_state(int frame) {
	if (is_srs()) return; // SRS 数据无需计算状态
	if (frame < 1 || frame > length_) return;

	int idx = frame - 1;
	x_current_position = x_position[idx];
	y_current_position = y_position[idx];
	z_current_position = z_position[idx];
	x_ratio = (x_start_scale != 0) ? x_scale[idx] / x_start_scale : 1.0;
	y_ratio = (y_start_scale != 0) ? y_scale[idx] / y_start_scale : 1.0;
	x_rotation_diff = x_rotation[idx] - x_start_rotation;
	y_rotation_diff = y_rotation[idx] - y_start_rotation;
	z_position_diff = z_position[idx] - z_start_position;
	z_rotation_diff = z_rotation[idx] - z_start_rotation;
}

void DataHandler::strip_fields(const MotionOptions& options) {
	if (is_srs()) return; // SRS 数据无需字段过滤
	// 对应 MoonScript 的 stripFields：将未选择的字段重置为起始值
	if (!options.x_position) {
		std::fill(x_position.begin(), x_position.end(), x_start_position);
	}
	if (!options.y_position) {
		std::fill(y_position.begin(), y_position.end(), y_start_position);
	}
	if (!options.z_position) {
		std::fill(z_position.begin(), z_position.end(), z_start_position);
	}
	if (!options.x_scale) {
		std::fill(x_scale.begin(), x_scale.end(), x_start_scale);
		std::fill(y_scale.begin(), y_scale.end(), y_start_scale);
	}
	if (!options.x_rotation) {
		std::fill(x_rotation.begin(), x_rotation.end(), x_start_rotation);
	}
	if (!options.y_rotation) {
		std::fill(y_rotation.begin(), y_rotation.end(), y_start_rotation);
	}
	if (!options.z_rotation) {
		std::fill(z_rotation.begin(), z_rotation.end(), z_start_rotation);
	}
}

bool DataHandler::check_length(int total_frames) const {
	return total_frames == length_;
}

void DataHandler::reverse_data() {
	if (is_srs()) {
		// SRS 数据反转绘图数组
		std::reverse(srs_drawings_.begin(), srs_drawings_.end());
		return;
	}
	// 反转所有追踪数据数组，使帧顺序翻转
	// 对应原始 dialog_mocha.cpp 中 parseData(insertFromStart=true) 的行为
	// 用于反向追踪场景：Mocha 从最后一帧向第一帧追踪时，导出的数据是倒序的
	std::reverse(x_position.begin(), x_position.end());
	std::reverse(y_position.begin(), y_position.end());
	std::reverse(z_position.begin(), z_position.end());
	std::reverse(x_scale.begin(), x_scale.end());
	std::reverse(y_scale.begin(), y_scale.end());
	std::reverse(x_rotation.begin(), x_rotation.end());
	std::reverse(y_rotation.begin(), y_rotation.end());
	std::reverse(z_rotation.begin(), z_rotation.end());
}

// ============================================================================
// SRS (Shake Rotoshape) 解析实现
// 对应 MoonScript a-mo.ShakeShapeHandler
// ============================================================================

/// SRS 顶点数据结构（使用浮点坐标，与 MoonScript 保持一致）
struct SrsVertex {
	double vx, vy, lx, ly, rx, ry;
};

/// 创建 SRS 顶点，Y 坐标翻转（对应 MoonScript updateCurve）
/// 保留浮点精度，不进行取整
static SrsVertex make_srs_vertex(const double vals[6], int height) {
	SrsVertex v;
	v.vx = vals[0];
	v.vy = height - vals[1];
	v.lx = vals[2];
	v.ly = height - vals[3];
	v.rx = vals[4];
	v.ry = height - vals[5];
	return v;
}

bool DataHandler::parse_srs(const std::string& raw_data, int script_height) {
	// 验证头部（对应 MoonScript: ^shake_shape_data 4.0，锚定到行首并检查版本号）
	if (raw_data.find("shake_shape_data 4.0") != 0) {
		return false;
	}

	srs_tableize(raw_data);

	if (srs_num_shapes_ <= 0 || srs_raw_vertices_.empty()) {
		return false;
	}

	// 帧数 = 总 vertex_data 行数 / 形状数
	length_ = static_cast<int>(srs_raw_vertices_.size()) / srs_num_shapes_;
	if (length_ <= 0) return false;

	type_ = DataType::SHAKE_SHAPE;
	srs_create_drawings(script_height);

	return !srs_drawings_.empty();
}

bool DataHandler::parse_srs_file(const std::string& file_path, int script_height) {
	// 对应 MoonScript ShakeShapeHandler: 支持带引号的路径
	std::string path = file_path;
	if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
		path = path.substr(1, path.size() - 2);
	}
	std::ifstream file(path);
	if (!file.is_open()) return false;

	std::string content((std::istreambuf_iterator<char>(file)),
	                     std::istreambuf_iterator<char>());
	file.close();

	if (content.empty()) return false;

	return parse_srs(content, script_height);
}

bool DataHandler::best_effort_parse(const std::string& input, int script_res_x, int script_res_y) {
	// 对应 MoonScript DataWrapper.bestEffortParsingAttempt
	// 按优先级尝试不同的解析策略

	if (input.find("Adobe After Effects") == 0) {
		// 明确为 AE 关键帧格式，仅尝试 TSR 解析
		// 对应 MoonScript DataWrapper: AE 头部匹配后仅尝试 DataHandler
		if (parse(input, script_res_x, script_res_y)) return true;
	} else if (input.find("shake_shape_data 4.0") == 0) {
		// 明确为 SRS 格式
		if (parse_srs(input, script_res_y)) return true;
	} else {
		// 无法确定格式，先尝试作为文件路径
		if (parse_file(input, script_res_x, script_res_y)) return true;
		if (parse_srs_file(input, script_res_y)) return true;
	}

	return false;
}

std::string DataHandler::get_srs_drawing(int frame) const {
	if (!is_srs()) return "";
	int idx = frame - 1;
	if (idx < 0 || idx >= static_cast<int>(srs_drawings_.size())) return "";
	return srs_drawings_[idx];
}

void DataHandler::srs_tableize(const std::string& raw_data) {
	// 提取 num_shapes
	auto shapes_pos = raw_data.find("num_shapes");
	if (shapes_pos != std::string::npos) {
		std::istringstream ss(raw_data.substr(shapes_pos));
		std::string label;
		ss >> label >> srs_num_shapes_;
	}

	// 收集所有 vertex_data 行
	srs_raw_vertices_.clear();
	std::istringstream stream(raw_data);
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.find("vertex_data") != std::string::npos) {
			srs_raw_vertices_.push_back(line);
		}
	}
}

void DataHandler::srs_create_drawings(int script_height) {
	// 对应 MoonScript ShakeShapeHandler.createDrawings
	// 对每一帧，将所有形状的 vertex_data 转换为 ASS 绘图命令拼接
	srs_drawings_.clear();
	srs_drawings_.reserve(length_);

	for (int base_idx = 0; base_idx < length_; ++base_idx) {
		std::string result;
		for (int curve_idx = base_idx;
		     curve_idx < srs_num_shapes_ * length_;
		     curve_idx += length_) {
			if (curve_idx >= static_cast<int>(srs_raw_vertices_.size())) break;

			std::string drawing = srs_convert_vertex(srs_raw_vertices_[curve_idx], script_height);
			if (!result.empty() && !drawing.empty()) {
				result += ' ';
			}
			result += drawing;
		}
		srs_drawings_.push_back(result);
	}
}

/// @brief 将单个 vertex_data 行转换为 ASS 绘图命令字符串
///
/// SRS vertex_data 行格式：
///   vertex_data V1_vx V1_vy V1_lx V1_ly V1_rx V1_ry V1_ig*6 [V2 的 12 个值] ...
///
/// 每个顶点包含 12 个浮点数：
///   - 前 6 个：vx vy lx ly rx ry（顶点坐标、左控制点、右控制点）
///   - 后 6 个：忽略
///
/// 绘图命令生成规则（对应 MoonScript convertVertex）：
///   - 第一个顶点：移动命令 "m vx vy"
///   - 后续顶点：
///     若 prev.rx==prev.vx && prev.ry==prev.vy && curr.lx==curr.vx && curr.ly==curr.vy
///       → 直线命令 "l vx vy"
///     否则 → 贝塞尔命令 "b prev.rx prev.ry curr.lx curr.ly curr.vx curr.vy"
///   - 最后闭合回第一个顶点（同样的直线/贝塞尔判断）
///
/// Y 坐标翻转：y = script_height - raw_y（Shake 坐标系 Y 轴向上）
std::string DataHandler::srs_convert_vertex(const std::string& vertex_line, int script_height) {
	// 跳过 "vertex_data" 前缀并解析所有浮点数
	std::vector<double> numbers;
	std::istringstream ss(vertex_line);
	std::string token;
	while (ss >> token) {
		if (token == "vertex_data") continue;
		try {
			numbers.push_back(std::stod(token));
		} catch (...) {
			continue;
		}
	}

	// 每个顶点 12 个浮点数，至少需要 1 个顶点
	if (numbers.size() < 12) return "";

	int num_vertices = static_cast<int>(numbers.size()) / 12;

	// 解析所有顶点
	std::vector<SrsVertex> vertices;
	vertices.reserve(num_vertices);
	for (int i = 0; i < num_vertices; ++i) {
		vertices.push_back(make_srs_vertex(&numbers[i * 12], script_height));
	}

	// 浮点坐标格式化辅助函数（使用 %.14g 匹配 Lua tostring 精度，去除尾部零）
	auto fmt = [](double v) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.14g", v);
		return std::string(buf);
	};

	// 浮点数近似相等判断（用于贝塞尔/直线判定）
	auto feq = [](double a, double b) {
		return std::fabs(a - b) < 1e-6;
	};

	// 生成绘图命令
	std::string draw;
	char curve_state = 'm';

	// 第一个顶点：移动命令
	const SrsVertex& first = vertices[0];
	draw += "m " + fmt(first.vx) + " " + fmt(first.vy) + " ";

	if (num_vertices == 1) return draw;

	// 后续顶点
	for (int i = 1; i < num_vertices; ++i) {
		const SrsVertex& prev = vertices[i - 1];
		const SrsVertex& curr = vertices[i];

		if (feq(prev.rx, prev.vx) && feq(prev.ry, prev.vy) &&
		    feq(curr.lx, curr.vx) && feq(curr.ly, curr.vy)) {
			// 直线
			if (curve_state != 'l') {
				curve_state = 'l';
				draw += "l ";
			}
			draw += fmt(curr.vx) + " " + fmt(curr.vy) + " ";
		} else {
			// 贝塞尔曲线
			// 对应 MoonScript: unless curveState == 'b' → 仅在状态切换时添加 'b' 命令前缀
			if (curve_state != 'b') {
				curve_state = 'b';
				draw += "b ";
			}
			draw += fmt(prev.rx) + " " + fmt(prev.ry)
			      + " " + fmt(curr.lx) + " " + fmt(curr.ly)
			      + " " + fmt(curr.vx) + " " + fmt(curr.vy) + " ";
		}
	}

	// 闭合：从最后一个顶点回到第一个顶点
	const SrsVertex& last = vertices[num_vertices - 1];
	if (feq(last.rx, last.vx) && feq(last.ry, last.vy) &&
	    feq(first.lx, first.vx) && feq(first.ly, first.vy)) {
		// 直线闭合
		if (curve_state != 'l') {
			draw += "l ";
		}
		draw += fmt(first.vx) + " " + fmt(first.vy) + " ";
	} else {
		// 贝塞尔闭合
		// 对应 MoonScript: unless curveState == 'b' → 仅在状态切换时添加 'b' 命令前缀
		if (curve_state != 'b') {
			draw += "b ";
		}
		draw += fmt(last.rx) + " " + fmt(last.ry)
		      + " " + fmt(first.lx) + " " + fmt(first.ly)
		      + " " + fmt(first.vx) + " " + fmt(first.vy) + " ";
	}

	return draw;
}

} // namespace mocha
