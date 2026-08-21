// Copyright (c) 2026, Aegisub Project http://www.aegisub.org/
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include <main.h>

#include <libaegisub/fs.h>
#include <libaegisub/lua/modules.h>

#include <lua.hpp>
#include <optional>
#include <string>

namespace {
// lfs 的入口函数在 lua/modules/lfs.cpp 中为 static，仅能经由
// luaopen_lfs_impl() 构建的 FFI 表访问，故按 aegisub.lfs 的
// attributes() 的调用方式访问 get_mode()
const char get_mode_chunk[] = R"LUA(
local path = ...
local ffi = require 'ffi'
local impl = require 'aegisub.__lfs_impl'
local err = ffi.new('char *[1]')
local mode = impl.get_mode(path, err)
if err[0] ~= nil then error(ffi.string(err[0]), 0) end
if mode == nil then return false end
return ffi.string(mode)
)LUA";

// 使测试数据与源文件编码无关
const char nonascii_dir[] = "data/lfs_M\xC3\xBCller";
const char nonascii_file[] = "data/lfs_M\xC3\xBCller/file";

class lagi_lua_lfs : public ::testing::Test {
protected:
	lua_State *L = nullptr;

	void SetUp() override {
		L = lua_open();
		ASSERT_NE(nullptr, L);
		agi::lua::preload_modules(L);

		agi::fs::CreateDirectory(agi::fs::path(nonascii_dir));
		agi::fs::Touch(agi::fs::path(nonascii_file));
	}

	void TearDown() override {
		agi::fs::Remove(agi::fs::path(nonascii_file));
		agi::fs::Remove(agi::fs::path(nonascii_dir));

		if (L) lua_close(L);
	}

	/// lfs 的 get_mode() 对给定路径报告的返回值，
	/// 为 nil 时对应空 optional，get_mode() 自身出错则判定测试失败
	std::optional<std::string> get_mode(const char *path) {
		if (luaL_loadstring(L, get_mode_chunk)) {
			ADD_FAILURE() << "failed to compile helper chunk: " << lua_tostring(L, -1);
			lua_pop(L, 1);
			return std::nullopt;
		}

		lua_pushstring(L, path);
		if (lua_pcall(L, 1, 1, 0)) {
			ADD_FAILURE() << "get_mode(" << path << ") failed: " << lua_tostring(L, -1);
			lua_pop(L, 1);
			return std::nullopt;
		}

		std::optional<std::string> ret;
		if (lua_type(L, -1) == LUA_TSTRING)
			ret = lua_tostring(L, -1);
		lua_pop(L, 1);
		return ret;
	}
};
}

TEST_F(lagi_lua_lfs, get_mode_reports_files_and_directories) {
	EXPECT_EQ("file", get_mode("data/file"));
	EXPECT_EQ("directory", get_mode("data/dir"));
	EXPECT_EQ(std::nullopt, get_mode("data/nonexistent"));
}

// 此前的缺陷是 get_mode() 把 path 参数收到的 UTF-8 字节直接交给 std::filesystem，
// 而非经 agi::fs::path 转换，导致 Windows 下 lfs.attributes(nonAsciiPath, "mode")
// 误报不存在，除非系统恰以 UTF-8 伪 ANSI 代码页（65001）运行
TEST_F(lagi_lua_lfs, get_mode_handles_non_ascii_paths) {
	EXPECT_EQ("directory", get_mode(nonascii_dir));
	EXPECT_EQ("file", get_mode(nonascii_file));
}
