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

/// @file auto4_lua_http.cpp
/// @brief aegisub.http lua API 实现，基于 libcurl，自动应用偏好设置中的代理配置
/// @ingroup scripting

#include "auto4_lua_http.h"

#include "proxy.h"

#include <libaegisub/lua/utils.h>

#include <curl/curl.h>

#include <string>
#include <vector>

using namespace agi::lua;

namespace {
	/// 选项表中的可选项
	struct HttpOptions {
		std::vector<std::string> headers; ///< "Name: value" 形式的请求头列表
		long timeout = 30; ///< 超时秒数
		bool follow_redirects = true; ///< 是否跟随重定向
	};

	/// curl 写回调，把响应体追加到 std::string
	size_t http_write_cb(const char *ptr, const size_t size, const size_t nmemb, void *userdata) {
		static_cast<std::string *>(userdata)->append(ptr, size * nmemb);
		return size * nmemb;
	}

	/// 从 lua 选项表解析 headers/timeout/follow_redirects
	HttpOptions parse_options(lua_State *L, const int idx) {
		HttpOptions opts;
		if (!lua_istable(L, idx))
			return opts;

		lua_getfield(L, idx, "headers");
		if (lua_istable(L, -1)) {
			lua_for_each(
				L, [&] {
					// key 在 -2，value 在 -1
					opts.headers.push_back(check_string(L, -2) + ": " + check_string(L, -1));
				}
			);
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "timeout");
		if (lua_isnumber(L, -1) && lua_tointeger(L, -1) > 0)
			opts.timeout = lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, idx, "follow_redirects");
		if (lua_isboolean(L, -1))
			opts.follow_redirects = !!lua_toboolean(L, -1);
		lua_pop(L, 1);

		return opts;
	}

	/// 初始化公共 curl 选项，应用代理与请求头
	/// @return 初始化失败返回 false，错误信息写入 err
	bool init_curl(CURL *curl, std::string const &url, HttpOptions const &opts, std::string &err) {
		if (!curl) {
			err = "Failed to initialize curl";
			return false;
		}
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, opts.follow_redirects ? 1L : 0L);
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, opts.timeout);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "Aegisub");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
		// 应用偏好设置中的代理
		proxy::ApplyToCurl(curl);
		return true;
	}

	/// 执行请求并检查结果
	/// @return 空串表示成功，否则返回错误描述
	std::string perform(CURL *curl) {
		const CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
			return curl_easy_strerror(res);

		long code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		if (code >= 400)
			return "HTTP error code: " + std::to_string(code);

		return {};
	}
}

int http_get(lua_State *L) {
	const std::string url = check_string(L, 1);
	const HttpOptions opts = parse_options(L, 2);

	std::string body, err;
	CURL *curl = curl_easy_init();
	if (!init_curl(curl, url, opts, err)) {
		if (curl) curl_easy_cleanup(curl);
		lua_pushnil(L);
		push_value(L, err);
		return 2;
	}
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

	curl_slist *headers = nullptr;
	for (auto const &h : opts.headers)
		headers = curl_slist_append(headers, h.c_str());
	if (headers)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	err = perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (!err.empty()) {
		lua_pushnil(L);
		push_value(L, err);
		return 2;
	}
	push_value(L, body);
	return 1;
}

int http_post(lua_State *L) {
	const std::string url = check_string(L, 1);
	const std::string post_data = check_string(L, 2);
	const HttpOptions opts = parse_options(L, 3);

	std::string body, err;
	CURL *curl = curl_easy_init();
	if (!init_curl(curl, url, opts, err)) {
		if (curl) curl_easy_cleanup(curl);
		lua_pushnil(L);
		push_value(L, err);
		return 2;
	}
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_data.size()));

	curl_slist *headers = nullptr;
	for (auto const &h : opts.headers)
		headers = curl_slist_append(headers, h.c_str());
	if (headers)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	err = perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (!err.empty()) {
		lua_pushnil(L);
		push_value(L, err);
		return 2;
	}
	push_value(L, body);
	return 1;
}

int http_download(lua_State *L) {
	const std::string url = check_string(L, 1);
	const std::string filename = check_string(L, 2);
	const HttpOptions opts = parse_options(L, 3);

	// Windows 上以二进制模式打开，避免换行转换破坏下载文件
	FILE *out = fopen(filename.c_str(), "wb");
	if (!out) {
		lua_pushnil(L);
		push_value(L, "Failed to open output file: " + filename);
		return 2;
	}

	std::string err;
	CURL *curl = curl_easy_init();
	if (!init_curl(curl, url, opts, err)) {
		if (curl) curl_easy_cleanup(curl);
		fclose(out);
		lua_pushnil(L);
		push_value(L, err);
		return 2;
	}
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);

	curl_slist *headers = nullptr;
	for (auto const &h : opts.headers)
		headers = curl_slist_append(headers, h.c_str());
	if (headers)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	err = perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	fclose(out);

	if (!err.empty()) {
		// 清理下载失败产生的不完整文件
		remove(filename.c_str());
		lua_pushnil(L);
		push_value(L, err);
		return 2;
	}
	push_value(L, true);
	return 1;
}
