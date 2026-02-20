// Copyright (c) 2024-2026, Aegisub contributors
// 运动追踪数据解析器声明
// 对应 MoonScript 版 a-mo.DataHandler + a-mo.ShakeShapeHandler + a-mo.DataWrapper
// 支持两种数据格式：
//   - TSR (AE 关键帧) / AE_KEYFRAME：位置、缩放、旋转数据
//   - SRS (Shake Rotoshape) / SHAKE_SHAPE：矢量绘图路径数据

#pragma once

#include "motion_common.h"
#include <string>
#include <vector>

namespace mocha {

/// 运动追踪数据解析器
/// 支持 AE 关键帧（TSR）和 Shake Rotoshape（SRS）两种格式
/// 对应 MoonScript 版 DataHandler + ShakeShapeHandler + DataWrapper
class DataHandler {
public:
	DataHandler() = default;

	/// 自动检测格式并解析（对应 MoonScript DataWrapper.bestEffortParsingAttempt）
	/// 按以下顺序尝试：
	///   1. 以 "Adobe After Effects" 开头 → 尝试 TSR 解析
	///   2. 以 "shake_shape_data" 开头 → 尝试 SRS 解析
	///   3. 其他 → 先试文件路径解析，再试 SRS 解析
	/// @param input 原始数据文本或文件路径
	/// @param script_res_x 脚本水平分辨率
	/// @param script_res_y 脚本垂直分辨率
	/// @return 解析是否成功
	bool best_effort_parse(const std::string& input, int script_res_x, int script_res_y);

	/// 从原始字符串解析 TSR 数据
	/// @param raw_data AE 关键帧格式的文本数据
	/// @param script_res_x 脚本水平分辨率
	/// @param script_res_y 脚本垂直分辨率
	/// @return 解析是否成功
	bool parse(const std::string& raw_data, int script_res_x, int script_res_y);

	/// 从文件路径读取并解析数据
	/// 对应 MoonScript DataHandler.parseFile
	/// @param file_path 文件路径
	/// @param script_res_x 脚本水平分辨率
	/// @param script_res_y 脚本垂直分辨率
	/// @return 解析是否成功
	bool parse_file(const std::string& file_path, int script_res_x, int script_res_y);

	/// 解析 SRS (Shake Rotoshape) 格式数据
	/// 对应 MoonScript ShakeShapeHandler
	/// @param raw_data SRS 格式文本数据
	/// @param script_height 脚本垂直分辨率（用于 Y 坐标翻转）
	/// @return 解析是否成功
	bool parse_srs(const std::string& raw_data, int script_height);

	/// 从文件路径读取并解析 SRS 数据
	/// @param file_path 文件路径
	/// @param script_height 脚本垂直分辨率
	/// @return 解析是否成功
	bool parse_srs_file(const std::string& file_path, int script_height);

	/// 设置参考帧（起始帧）：仅对 TSR 数据有效，SRS 为空操作
	/// @param frame 参考帧索引（从1开始）
	void add_reference_frame(int frame);

	/// 计算指定帧的当前状态：仅对 TSR 数据有效，SRS 为空操作
	/// @param frame 帧索引（从1开始）
	void calculate_current_state(int frame);

	/// 根据选项禁用未选择的字段：仅对 TSR 数据有效
	/// @param options 运动选项
	void strip_fields(const MotionOptions& options);

	/// 检查数据长度是否匹配
	bool check_length(int total_frames) const;

	/// 反转追踪数据数组顺序（用于反向追踪模式）
	/// 对应原始 parseData 中 insertFromStart 的行为
	void reverse_data();

	/// 获取数据总帧数
	int length() const { return length_; }

	/// 获取数据源类型
	DataType type() const { return type_; }

	/// 判断是否为 SRS 数据
	bool is_srs() const { return type_ == DataType::SHAKE_SHAPE; }

	/// 获取 SRS 帧对应的 ASS 绘图字符串
	/// @param frame 帧索引（从1开始）
	/// @return ASS 绘图字符串，索引越界返回空
	std::string get_srs_drawing(int frame) const;

	/// 获取源视频宽度
	int source_width() const { return source_width_; }

	/// 获取源视频高度
	int source_height() const { return source_height_; }

	/// 获取帧率
	double frame_rate() const { return frame_rate_; }

	// 原始帧数据数组（索引从0开始）
	// 仅 TSR 数据使用
	std::vector<double> x_position;
	std::vector<double> y_position;
	std::vector<double> z_position;
	std::vector<double> x_scale;
	std::vector<double> y_scale;
	std::vector<double> x_rotation;
	std::vector<double> y_rotation;
	std::vector<double> z_rotation;

	// 参考帧状态（仅 TSR 使用）
	int start_frame = 0;
	double x_start_position = 0;
	double y_start_position = 0;
	double z_start_position = 0;
	double x_start_scale = 0;
	double y_start_scale = 0;
	double x_start_rotation = 0;
	double y_start_rotation = 0;
	double z_start_rotation = 0;

	// 当前帧计算结果（仅 TSR 使用）
	double x_current_position = 0;
	double y_current_position = 0;
	double z_current_position = 0;
	double x_ratio = 1.0;
	double y_ratio = 1.0;
	double x_rotation_diff = 0;
	double y_rotation_diff = 0;
	double z_position_diff = 0;
	double z_rotation_diff = 0;

private:
	/// 将原始文本按行分割
	void tableize(const std::string& raw_data);

	/// 解析 TSR 分割后的数据段
	void parse_sections();

	/// SRS 内部：收集 vertex_data 行，计算帧数
	void srs_tableize(const std::string& raw_data);

	/// SRS 内部：将顶点数据转换为 ASS 绘图字符串
	void srs_create_drawings(int script_height);

	/// SRS 内部：将单个 vertex_data 行转换为 ASS 绘图命令
	/// 对应 MoonScript convertVertex()
	static std::string srs_convert_vertex(const std::string& vertex_line, int script_height);

	DataType type_ = DataType::NONE;
	int length_ = 0;
	int source_width_ = 0;
	int source_height_ = 0;
	double frame_rate_ = 0;
	double x_pos_scale_ = 1.0;
	double y_pos_scale_ = 1.0;
	std::vector<std::string> raw_lines_;

	// SRS 数据
	std::vector<std::string> srs_raw_vertices_; // vertex_data 原始行
	std::vector<std::string> srs_drawings_;     // 每帧的 ASS 绘图字符串
	int srs_num_shapes_ = 0;
};

} // namespace mocha
