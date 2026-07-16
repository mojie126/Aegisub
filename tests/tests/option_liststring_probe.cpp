// Copyright (c) 2026, Aegisub Project
// All rights reserved.

/// @file option_liststring_probe.cpp
/// @brief 验证 ListString 选项的 Set 往返与订阅触发（诊断字体管理器持久化问题）

#include <libaegisub/fs.h>
#include <libaegisub/option.h>
#include <libaegisub/option_value.h>

#include <gtest/gtest.h>

#include <functional>
#include <vector>

static const char probe_config[] = R"raw({
	"App" : {
		"User Font Paths" : [{"string": ""}]
	}
})raw";

static const char probe_default[] = R"raw({
	"App" : {
		"User Font Paths" : [{"string": ""}]
	}
})raw";

TEST(option_liststring_probe, liststring_set_roundtrip) {
	agi::Options opt(probe_config, probe_default, agi::Options::FLUSH_SKIP);

	auto before = opt.Get("App/User Font Paths")->GetListString();
	EXPECT_EQ(before.size(), 1u);

	std::vector<std::string> new_paths = {"C:\\fonts\\a.ttf", "D:\\myfonts"};
	auto ov = std::make_unique<agi::OptionValueListString>("App/User Font Paths", new_paths);
	opt.Get("App/User Font Paths")->Set(ov.get());

	auto after = opt.Get("App/User Font Paths")->GetListString();
	ASSERT_EQ(after.size(), 2u);
	EXPECT_EQ(after[0], "C:\\fonts\\a.ttf");
	EXPECT_EQ(after[1], "D:\\myfonts");
}

TEST(option_liststring_probe, liststring_subscribe_trigger) {
	agi::Options opt(probe_config, probe_default, agi::Options::FLUSH_SKIP);

	int fired = 0;
	opt.Get("App/User Font Paths")->Subscribe([&fired] { fired++; });

	std::vector<std::string> new_paths = {"C:\\fonts\\a.ttf"};
	auto ov = std::make_unique<agi::OptionValueListString>("App/User Font Paths", new_paths);
	opt.Get("App/User Font Paths")->Set(ov.get());

	EXPECT_EQ(fired, 1);
}
