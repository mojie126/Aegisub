#include <main.h>

#include "ass_info.h"
#include "auto4_lua.h"

#include <memory>
#include <vector>

TEST(LuaAutomationTest, ScopedTrackedAssEntryUnregistersOnNormalExit) {
	std::vector<AssEntry *> allocated_lines;
	auto line = std::make_unique<AssInfo>("Title", "Test");
	AssEntry *raw = line.get();

	{
		Automation4::ScopedTrackedAssEntry tracked_entry(allocated_lines, raw);
		ASSERT_EQ(1u, allocated_lines.size());
		EXPECT_EQ(raw, allocated_lines.front());
	}

	EXPECT_TRUE(allocated_lines.empty());
}

TEST(LuaAutomationTest, ScopedTrackedAssEntryOnlyRemovesItsOwnEntry) {
	std::vector<AssEntry *> allocated_lines;
	auto preserved_line = std::make_unique<AssInfo>("ScriptType", "v4.00+");
	auto tracked_line = std::make_unique<AssInfo>("Title", "Test");
	AssEntry *preserved = preserved_line.get();
	AssEntry *tracked = tracked_line.get();

	allocated_lines.push_back(preserved);
	{
		Automation4::ScopedTrackedAssEntry tracked_entry(allocated_lines, tracked);
		ASSERT_EQ(2u, allocated_lines.size());
		EXPECT_EQ(preserved, allocated_lines.front());
		EXPECT_EQ(tracked, allocated_lines.back());
	}

	ASSERT_EQ(1u, allocated_lines.size());
	EXPECT_EQ(preserved, allocated_lines.front());
}
