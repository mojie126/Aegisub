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

/// @file proxy.cpp
/// @brief 代理设置读取与应用实现
/// @ingroup configuration_ui

#include "proxy.h"

#include "options.h"

#include <curl/curl.h>

#include <cstdlib>

#ifdef _WIN32
#include <wininet.h>
#endif

namespace proxy {
	Config GetConfig() {
		Config cfg;
		cfg.mode = static_cast<Mode>(OPT_GET("App/Proxy/Mode")->GetInt());
		cfg.host = OPT_GET("App/Proxy/Http Host")->GetString();
		cfg.port = OPT_GET("App/Proxy/Http Port")->GetInt();
		return cfg;
	}

	void ApplyToCurl(CURL *curl) {
		const auto cfg = GetConfig();
		if (!curl || !cfg.Enabled())
			return;
		const auto url = cfg.Url();
		curl_easy_setopt(curl, CURLOPT_PROXY, url.c_str());
	}

	void ApplyProcessProxy() {
		const auto cfg = GetConfig();
		#ifdef _WIN32
		// 设置 http_proxy/https_proxy 环境变量，供进程内使用 libcurl 且未显式
		// 设置代理的组件使用（如 DM.DownloadManager 预编译 DLL 的下载请求）
		auto apply_proxy_env = [](std::string const &url) {
			if (url.empty()) {
				_putenv_s("http_proxy", "");
				_putenv_s("https_proxy", "");
			} else {
				_putenv_s("http_proxy", url.c_str());
				_putenv_s("https_proxy", url.c_str());
			}
		};
		// UTF-8 窄字符与宽字符互转，代理主机名含非 ASCII 字符时按字节
		// 逐个宽化/窄化会损坏编码，必须经系统代码页转换
		auto to_wide = [](std::string const &s) {
			std::wstring ret;
			if (s.empty()) return ret;
			const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
			ret.resize(len - 1);
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ret.data(), len);
			return ret;
		};
		auto to_narrow = [](std::wstring const &s) {
			std::string ret;
			if (s.empty()) return ret;
			const int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
			ret.resize(len - 1);
			WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, ret.data(), len, nullptr, nullptr);
			return ret;
		};
		// 通过 INTERNET_PER_CONN_OPTION_LIST 注入进程级代理配置
		// 旧式 INTERNET_OPTION_PROXY 注入会使 INTERNET_OPEN_TYPE_PRECONFIG
		// 请求返回 ERROR_INTERNET_CANNOT_CONNECT，DependencyControl 等使用
		// WinINet 下载的组件会全部无法连接
		INTERNET_PER_CONN_OPTION options[3] = {};
		unsigned long option_count = 0;
		std::string proxy_url;
		if (cfg.Enabled()) {
			// InternetSetOptionW 会拷贝代理字符串，局部变量即可
			// 先保存 Url 结果，避免 begin/end 迭代器来自不同临时对象
			proxy_url = cfg.Url();
			const std::wstring wproxy_url = to_wide(proxy_url);
			options[option_count].dwOption = INTERNET_PER_CONN_FLAGS;
			options[option_count].Value.dwValue = PROXY_TYPE_PROXY;
			option_count++;
			options[option_count].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
			options[option_count].Value.pszValue = const_cast<wchar_t *>(wproxy_url.c_str());
			option_count++;
			options[option_count].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
			options[option_count].Value.pszValue = const_cast<wchar_t *>(L"<local>");
			option_count++;
		} else if (cfg.mode == Mode::System) {
			// 系统代理模式：读取系统配置中的显式代理，注入进程级配置并应用到环境变量，自动检测与 PAC 场景无法转换则仅清除覆盖
			// 注意不能注入 DIRECT 清除，PRECONFIG 请求优先使用进程级
			// 注入的默认连接配置，DIRECT 会让 WinINet 组件强制直连
			INTERNET_PER_CONN_OPTION query_options[2] = {};
			INTERNET_PER_CONN_OPTION_LIST query_list = {};
			query_options[0].dwOption = INTERNET_PER_CONN_FLAGS;
			query_options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
			query_list.dwSize = sizeof(query_list);
			query_list.dwOptionCount = 2;
			query_list.pOptions = query_options;
			unsigned long query_size = sizeof(query_list);
			if (InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &query_list, &query_size)
				&& (query_options[0].Value.dwValue & PROXY_TYPE_PROXY) && query_options[1].Value.pszValue) {
				const std::wstring wsys_url(query_options[1].Value.pszValue);
				proxy_url = "http://" + to_narrow(wsys_url);
				const std::wstring wproxy_url = to_wide(proxy_url);
				options[option_count].dwOption = INTERNET_PER_CONN_FLAGS;
				options[option_count].Value.dwValue = PROXY_TYPE_PROXY;
				option_count++;
				options[option_count].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
				options[option_count].Value.pszValue = const_cast<wchar_t *>(wproxy_url.c_str());
				option_count++;
				options[option_count].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
				options[option_count].Value.pszValue = const_cast<wchar_t *>(L"<local>");
				option_count++;
			} else {
				// 无系统代理：清除进程内注入的代理覆盖，PRECONFIG 回退注册表直连
				options[0].dwOption = INTERNET_PER_CONN_FLAGS;
				options[0].Value.dwValue = PROXY_TYPE_DIRECT;
				option_count = 1;
			}
		} else {
			// 直连模式清除进程内注入的代理覆盖，并清空环境变量
			options[0].dwOption = INTERNET_PER_CONN_FLAGS;
			options[0].Value.dwValue = PROXY_TYPE_DIRECT;
			option_count = 1;
		}
		apply_proxy_env(proxy_url);
		INTERNET_PER_CONN_OPTION_LIST option_list = {};
		option_list.dwSize = sizeof(option_list);
		option_list.dwOptionCount = option_count;
		option_list.pOptions = options;
		if (InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &option_list, sizeof(option_list))) {
			InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
			InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
		}
		#else
		// 系统代理模式不干预环境变量，libcurl 使用进程继承的 http_proxy/https_proxy
		if (cfg.mode == Mode::System)
			return;
		if (cfg.Enabled()) {
			auto url = cfg.Url();
			setenv("http_proxy", url.c_str(), 1);
			setenv("https_proxy", url.c_str(), 1);
		} else {
			unsetenv("http_proxy");
			unsetenv("https_proxy");
		}
		#endif
	}
}
