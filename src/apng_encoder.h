/// @file apng_encoder.h
/// @see apng_encoder.cpp
/// @ingroup video_export
///
/// APNG 动画编码器，输出 8 位 RGBA 真彩色无损动画

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace agi {
	/// @brief APNG 动画编码器
	/// @details 基于 zlib 输出无损 APNG，
	/// 所有帧使用 dispose NONE 与 blend SOURCE 全帧输出，兼容所有 APNG 解码器，
	/// 首帧写入默认图像 IDAT，后续帧写入 fdAT
	class ApngEncoder {
	public:
		/// @brief 构造函数，打开输出文件并写入 PNG 签名、IHDR 与 acTL 块
		/// @param output_path 输出文件路径
		/// @param width 输出帧宽度
		/// @param height 输出帧高度
		/// @param total_frames 预期总帧数
		/// @param num_plays 循环次数，0 表示无限循环
		ApngEncoder(const std::wstring &output_path, int width, int height,
					uint32_t total_frames, uint32_t num_plays = 0);

		~ApngEncoder();

		ApngEncoder(const ApngEncoder &) = delete;

		ApngEncoder &operator=(const ApngEncoder &) = delete;

		/// @brief 编码器是否可用
		/// @return 文件打开且头部写入成功时返回 true
		[[nodiscard]] bool IsOk() const { return file != nullptr && !broken; }

		/// @brief 添加一帧 RGBA 数据
		/// @param rgba 像素数据，大小须为 width*height*4 字节
		/// @param delay_ms 该帧展示时长（毫秒）
		/// @return 写入失败或超出预期总帧数时返回 false
		bool AddFrame(const uint8_t *rgba, uint32_t delay_ms);

		/// @brief 完成写入
		/// @details 写入 IEND 块并关闭文件，
		/// 实际帧数与预期不一致（如用户取消导出）时回写修正 acTL 帧数，
		/// 保证输出始终为合法动画
		/// @return 写入失败或未写入任何帧时返回 false
		bool Finish();

	private:
		/// @brief 写入一个完整 PNG 块（长度、类型、数据、CRC）
		bool WriteChunk(const char type[4], const void *data, uint32_t size) const;

		/// @brief 对帧像素做扫描线过滤并 zlib 压缩
		bool EncodeFrameData(const uint8_t *rgba, std::vector<uint8_t> &compressed) const;

		std::FILE *file = nullptr; ///< 输出文件句柄
		int width = 0; ///< 输出帧宽度
		int height = 0; ///< 输出帧高度
		uint32_t total_frames = 0; ///< 预期总帧数
		uint32_t num_plays = 0; ///< 循环次数，0 表示无限循环
		uint32_t frames_written = 0; ///< 已写入帧数
		uint32_t next_sequence_number = 0; ///< fcTL 与 fdAT 共享的全局序列号
		long actl_num_frames_offset = 0; ///< acTL 块 num_frames 字段的文件偏移，用于完成时回写
		bool finished = false; ///< 是否已完成写入
		bool broken = false; ///< 是否发生不可恢复的错误
	};
}
