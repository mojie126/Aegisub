// 测试桩文件：提供单测二进制所需的主项目符号的轻型实现
// 避免链接完整的 utils.cpp（依赖 wxWidgets）等重型文件

#include <string>

/// vector2d.cpp / vector3d.cpp 中 Str() 方法依赖此函数
/// 单测中不调用 Str()，因此桩实现可为空
std::string float_to_string(double val, int precision) {
	return std::to_string(val);
}

// perspective_processor.cpp 调用此函数获取真实文本尺寸
// 单测中无 GDI 上下文，返回 false 以使用估算回退
#include "../src/ass_style.h"
AssEntryGroup AssStyle::Group() const { return AssEntryGroup::STYLE; }

namespace Automation4 {
	bool CalculateTextExtents(AssStyle *, std::string const &, double &, double &, double &, double &) {
		return false;
	}
}