// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪数据解析器实现
// 对应 MoonScript 版 arch.PerspectiveMotion 中的 parse_powerpin_data / parse_single_pin

#include "perspective_data.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <cctype>
#include <cmath>

namespace mocha {
	bool PerspectiveDataHandler::ParseSinglePin(const std::vector<std::string> &lines,
												const std::string &marker,
												std::vector<double> &coords,
												int coord_index,
												std::vector<int> *frames) {
		// header 锚定与同名 marker 唯一性校验对应上游 parse_single_pin（#pin_pos != 1 即拒绝），
		// 数据行遇空行/下一节停止亦与上游 while 循环一致，本地比上游更严格的点：
		// 1) 非数字数据行直接判非法（上游静默截断到该行前的数据），
		// 2) 空行之后若再现数据行判非法（上游会静默接受截断/拼接损坏的数据），
		// 3) 数值要求完整消费且有限（NaN/Inf/尾随垃圾拒绝），
		// 4) ParsePowerPin 中 Frame 列一致性/严格递增校验（上游仅检查长度一致），
		// 这些严格化拒绝的都是语义必然错误的数据（错位/空洞会产生错误四边形），
		// 代价是重新追踪导致 Frame 错位等异常导出会被整段拒绝
		std::string pattern = "^Effects[\\t ]+CC Power Pin #1[\\t ]+CC Power Pin-" + marker + "[\\t ]*$";
		std::regex pin_header(pattern);

		int pin_pos = -1;
		for (size_t i = 0; i < lines.size(); ++i) {
			if (std::regex_search(lines[i], pin_header)) {
				pin_pos = static_cast<int>(i);
				break;
			}
		}

		if (pin_pos < 0)
			return false;

		// 唯一性：后续不得再出现同名 marker 的 header
		for (size_t i = static_cast<size_t>(pin_pos) + 1; i < lines.size(); ++i) {
			if (std::regex_search(lines[i], pin_header))
				return false;
		}

		// 数据行从 pin 头部的下两行开始
		coords.clear();
		if (frames)
			frames->clear();
		for (size_t i = static_cast<size_t>(pin_pos) + 2; i < lines.size(); ++i) {
			const auto &line = lines[i];

			// 检查是否以数字开头（Frame 列）
			bool starts_with_digit = false;
			for (char ch : line) {
				if (ch == ' ' || ch == '\t')
					continue;
				if (std::isdigit(static_cast<unsigned char>(ch))) {
					starts_with_digit = true;
				}
				break;
			}

			if (!starts_with_digit) {
				size_t first = line.find_first_not_of(" \t");
				const std::string trimmed = first == std::string::npos ? "" : line.substr(first);
				if (trimmed.rfind("Effects", 0) == 0 ||
					trimmed.rfind("End of Keyframe Data", 0) == 0)
					break;
				if (trimmed.empty()) {
					// 空行通常是数据块与下一节之间的正常分隔（上游同样在此停止），
					// 但若空行之后仍出现数据行，说明数据块中部存在空洞
					// （截断或拼接损坏），继续解析会静默接受残缺数据，应判定为非法
					for (size_t j = i + 1; j < lines.size(); ++j) {
						size_t next_first = lines[j].find_first_not_of(" \t");
						if (next_first == std::string::npos)
							continue;
						const std::string next = lines[j].substr(next_first);
						if (next.rfind("Effects", 0) == 0 ||
							next.rfind("End of Keyframe Data", 0) == 0)
							break;
						if (std::isdigit(static_cast<unsigned char>(next.front())))
							return false;
						break;
					}
					break;
				}
				return false;
			}

			// 按空白分割提取第 coord_index 列的值
			std::vector<std::string> tokens;
			std::istringstream iss(line);
			std::string token;
			while (iss >> token)
				tokens.push_back(token);

			if (static_cast<int>(tokens.size()) <= coord_index)
				return false;

			double value = 0;
			try {
				size_t consumed = 0;
				const auto &value_token = tokens[static_cast<size_t>(coord_index)];
				value = std::stod(value_token, &consumed);
				if (consumed != value_token.size())
					return false;
			} catch (...) {
				return false;
			}
			if (!std::isfinite(value) || !std::isfinite(static_cast<float>(value)))
				return false;
			coords.push_back(value);
			if (frames) {
				try {
					size_t consumed = 0;
					const int frame = std::stoi(tokens[0], &consumed);
					if (consumed != tokens[0].size())
						return false;
					frames->push_back(frame);
				} catch (...) {
					return false;
				}
			}
		}

		return !coords.empty();
	}

	bool PerspectiveDataHandler::ParsePowerPin(const std::string &raw_data) {
		quads_.clear();

		// 按行分割
		std::vector<std::string> lines;
		std::istringstream stream(raw_data);
		std::string line;
		while (std::getline(stream, line)) {
			// 去除末尾的 \r
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			lines.push_back(line);
		}

		// 验证是否包含 Power Pin 数据
		bool has_powerpin = false;
		for (const auto &l : lines) {
			if (l.find("CC Power Pin #1") != std::string::npos) {
				has_powerpin = true;
				break;
			}
		}
		if (!has_powerpin)
			return false;

		// 解析四个角点的 X 和 Y 坐标
		// Power Pin 角点顺序: TL=0002, TR=0003, BR=0005, BL=0004
		std::vector<double> x1, y1, x2, y2, x3, y3, x4, y4;
		std::vector<int> fx1, fy1, fx2, fy2, fx3, fy3, fx4, fy4;

		if (!ParseSinglePin(lines, "0002", x1, 1, &fx1) || // TopLeft X
			!ParseSinglePin(lines, "0002", y1, 2, &fy1) || // TopLeft Y
			!ParseSinglePin(lines, "0003", x2, 1, &fx2) || // TopRight X
			!ParseSinglePin(lines, "0003", y2, 2, &fy2) || // TopRight Y
			!ParseSinglePin(lines, "0005", x3, 1, &fx3) || // BottomRight X
			!ParseSinglePin(lines, "0005", y3, 2, &fy3) || // BottomRight Y
			!ParseSinglePin(lines, "0004", x4, 1, &fx4) || // BottomLeft X
			!ParseSinglePin(lines, "0004", y4, 2, &fy4)) // BottomLeft Y
			return false;

		// 验证长度一致
		size_t len = x1.size();
		if (len == 0 || y1.size() != len || x2.size() != len || y2.size() != len ||
			x3.size() != len || y3.size() != len || x4.size() != len || y4.size() != len)
			return false;

		// 验证四个角点的 Frame 列完全一致（损坏或拼接的数据直接拒绝）
		if (fx1.size() != len || fy1.size() != len || fx2.size() != len || fy2.size() != len ||
			fx3.size() != len || fy3.size() != len || fx4.size() != len || fy4.size() != len)
			return false;
		for (size_t i = 0; i < len; ++i) {
			if (fx1[i] != fy1[i] || fx1[i] != fx2[i] || fx1[i] != fy2[i] ||
				fx1[i] != fx3[i] || fx1[i] != fy3[i] || fx1[i] != fx4[i] || fx1[i] != fy4[i])
				return false;
			// Frame 列必须严格递增
			if (i > 0 && fx1[i] <= fx1[i - 1])
				return false;
		}

		// 组装每帧四边形
		for (size_t i = 0; i < len; ++i) {
			std::vector<Vector2D> quad = {
				Vector2D(static_cast<float>(x1[i]), static_cast<float>(y1[i])),
				Vector2D(static_cast<float>(x2[i]), static_cast<float>(y2[i])),
				Vector2D(static_cast<float>(x3[i]), static_cast<float>(y3[i])),
				Vector2D(static_cast<float>(x4[i]), static_cast<float>(y4[i])),
			};
			quads_.push_back(quad);
		}

		return true;
	}

	bool PerspectiveDataHandler::ParseFile(const std::string &file_path) {
		std::ifstream file(file_path);
		if (!file.is_open())
			return false;

		std::stringstream buffer;
		buffer << file.rdbuf();
		return ParsePowerPin(buffer.str());
	}

	bool PerspectiveDataHandler::BestEffortParse(const std::string &input) {
		// 优先尝试作为 Power-Pin 文本解析
		if (ParsePowerPin(input))
			return true;

		// 再尝试作为文件路径解析
		if (ParseFile(input))
			return true;

		return false;
	}

	const std::vector<Vector2D> *PerspectiveDataHandler::GetQuad(int frame) const {
		if (frame < 1 || frame > static_cast<int>(quads_.size()))
			return nullptr;
		return &quads_[static_cast<size_t>(frame - 1)];
	}

	bool PerspectiveDataHandler::CheckLength(int total_frames) const {
		return static_cast<int>(quads_.size()) == total_frames;
	}
} // namespace mocha
