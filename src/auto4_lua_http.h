// Copyright (c) 2026, mojie126
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

/// @file auto4_lua_http.h
/// @brief aegisub.http lua API 声明，供自动化脚本（DependencyControl 等）网络请求使用
/// @ingroup scripting

#pragma once

struct lua_State;

/// GET 请求，成功返回响应体字符串，失败返回 nil 与错误描述
int http_get(lua_State *L);

/// POST 请求，成功返回响应体字符串，失败返回 nil 与错误描述
int http_post(lua_State *L);

/// 下载文件到本地，成功返回 true，失败返回 nil 与错误描述
int http_download(lua_State *L);
