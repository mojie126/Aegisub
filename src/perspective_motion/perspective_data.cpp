// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪数据解析器实现
// 对应 MoonScript 版 arch.PerspectiveMotion 中的 parse_powerpin_data / parse_single_pin

#include "perspective_data.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <cctype>

namespace mocha {
	bool PerspectiveDataHandler::ParseSinglePin(const std::vector<std::string> &lines,
												const std::string &marker,
												std::vector<double> &coords,
												int coord_index) {
		std::string pattern = "Effects[\\t ]+CC Power Pin #1[\\t ]+CC Power Pin-" + marker;
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

		// 数据行从 pin 头部的下两行开始
		coords.clear();
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

			if (!starts_with_digit)
				break;

			// 按空白分割提取第 coord_index 列的值
			std::vector<std::string> tokens;
			std::istringstream iss(line);
			std::string token;
			while (iss >> token)
				tokens.push_back(token);

			if (static_cast<int>(tokens.size()) > coord_index) {
				try {
					coords.push_back(std::stod(tokens[static_cast<size_t>(coord_index)]));
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

		if (!ParseSinglePin(lines, "0002", x1, 1) || // TopLeft X
			!ParseSinglePin(lines, "0002", y1, 2) || // TopLeft Y
			!ParseSinglePin(lines, "0003", x2, 1) || // TopRight X
			!ParseSinglePin(lines, "0003", y2, 2) || // TopRight Y
			!ParseSinglePin(lines, "0005", x3, 1) || // BottomRight X
			!ParseSinglePin(lines, "0005", y3, 2) || // BottomRight Y
			!ParseSinglePin(lines, "0004", x4, 1) || // BottomLeft X
			!ParseSinglePin(lines, "0004", y4, 2)) // BottomLeft Y
			return false;

		// 验证长度一致
		size_t len = x1.size();
		if (len == 0 || y1.size() != len || x2.size() != len || y2.size() != len ||
			x3.size() != len || y3.size() != len || x4.size() != len || y4.size() != len)
			return false;

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
