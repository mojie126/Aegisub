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

/// @file proxy.h
/// @brief 代理设置读取与应用，供偏好设置、curl 调用点与自动化脚本网络 API 共用
/// @ingroup configuration_ui

#pragma once

#include <curl/curl.h>

#include <string>

namespace proxy {
	/// 代理模式，与 App/Proxy/Mode 选项取值一致
	enum class Mode : int {
		None = 0, ///< 无代理，直连
		Manual = 1, ///< 手动代理配置
		System = 2 ///< 系统代理设置，含自动检测与 PAC
	};

	/// @brief 当前代理配置快照
	struct Config {
		Mode mode = Mode::None;
		std::string host; ///< 代理主机名
		int port = 0; ///< 代理端口

		/// @brief 是否存在可用代理
		[[nodiscard]] bool Enabled() const { return mode == Mode::Manual && !host.empty() && port > 0; }
		/// @brief 生成 http://host:port 形式的代理地址
		[[nodiscard]] std::string Url() const {
			if (!Enabled())
				return {};
			return "http://" + host + ":" + std::to_string(port);
		}
	};

	/// @brief 读取偏好设置中的当前代理配置
	Config GetConfig();

	/// @brief 把代理配置应用到 curl 句柄
	/// @param curl 已初始化的 curl 句柄，未配置代理时不做任何设置
	void ApplyToCurl(CURL *curl);

	/// @brief 进程级应用代理配置
	/// @details Windows 通过 WinINet 的进程级代理配置生效，供同进程内
	/// 使用 WinINet 的组件（如 DependencyControl 的下载器）使用，
	/// 其他平台通过设置 http_proxy/https_proxy 环境变量生效
	void ApplyProcessProxy();
} // namespace proxy
