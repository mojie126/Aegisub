// Copyright (c) 2026, Aegisub contributors
// 按时间查找字幕行工具函数的单元测试

#include <main.h>

#include "../src/line_at_time.h"

#include <libaegisub/ass/time.h>

#include <vector>

using namespace agi;

class LineAtTimeTest : public libagi {};

namespace {
	/// 与 AssDialogue 具有相同 Start/End 接口的测试桩
	struct TestLine {
		agi::Time Start;
		agi::Time End;
	};
}

TEST(LineAtTimeTest, EmptyList) {
	std::vector<TestLine *> lines;
	EXPECT_EQ(FindLineAtTime(lines, 0), nullptr);
	EXPECT_EQ(FindLineAtTime(lines, 1000), nullptr);
}

TEST(LineAtTimeTest, WithinLine) {
	TestLine l1 = {0, 1000};
	TestLine l2 = {1000, 2000};
	TestLine l3 = {2000, 3000};
	std::vector<TestLine *> lines = {&l1, &l2, &l3};

	EXPECT_EQ(FindLineAtTime(lines, 0), &l1);
	EXPECT_EQ(FindLineAtTime(lines, 500), &l1);
	EXPECT_EQ(FindLineAtTime(lines, 999), &l1);
	// 区间左闭右开，起点属于下一行
	EXPECT_EQ(FindLineAtTime(lines, 1000), &l2);
	EXPECT_EQ(FindLineAtTime(lines, 1500), &l2);
	EXPECT_EQ(FindLineAtTime(lines, 2500), &l3);
}

TEST(LineAtTimeTest, GapFallsBackToLastStartedLine) {
	TestLine l1 = {0, 1000};
	TestLine l2 = {1000, 2000};
	TestLine l3 = {2000, 3000};
	std::vector<TestLine *> lines = {&l1, &l2, &l3};

	// 时间位于行间空隙时，返回开始时间不晚于该时间的最后一行
	EXPECT_EQ(FindLineAtTime(lines, 3000), &l3);
	EXPECT_EQ(FindLineAtTime(lines, 5000), &l3);
}

TEST(LineAtTimeTest, BeforeFirstLineReturnsNull) {
	TestLine l1 = {1000, 2000};
	std::vector<TestLine *> lines = {&l1};

	EXPECT_EQ(FindLineAtTime(lines, 0), nullptr);
	EXPECT_EQ(FindLineAtTime(lines, 999), nullptr);
}

TEST(LineAtTimeTest, UnsortedLines) {
	// 行列表顺序不代表时间顺序（如插入新行），但包含时间的行仍应被优先返回
	TestLine l1 = {0, 100};
	TestLine l2 = {5000, 6000};
	TestLine l3 = {200, 300};
	std::vector<TestLine *> lines = {&l1, &l2, &l3};

	EXPECT_EQ(FindLineAtTime(lines, 250), &l3);
	EXPECT_EQ(FindLineAtTime(lines, 50), &l1);
	EXPECT_EQ(FindLineAtTime(lines, 5500), &l2);
	// 行间空隙回退到遍历中最后一个已开始的行
	EXPECT_EQ(FindLineAtTime(lines, 400), &l3);
}
