// Copyright (c) 2005, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file version.cpp
/// @brief Derive and return various information about the build and version at runtime
/// @ingroup main
///

#include "version.h"
#include "git_version.h"

#include <cctype>
#include <sstream>
#include <vector>

#ifdef _DEBUG
	#define DEBUG_SUFFIX " [DEBUG VERSION]"
#else
	#define DEBUG_SUFFIX ""
#endif

#if defined(BUILD_CREDIT) && !TAGGED_RELEASE
	#define BUILD_CREDIT_SUFFIX ", " BUILD_CREDIT
#else
	#define BUILD_CREDIT_SUFFIX ""
#endif

const char *GetAegisubLongVersionString() {
	return BUILD_GIT_VERSION_STRING BUILD_CREDIT_SUFFIX DEBUG_SUFFIX;
}

const char *GetAegisubShortVersionString() {
	return BUILD_GIT_VERSION_STRING DEBUG_SUFFIX;
}

#ifdef BUILD_CREDIT
const char *GetAegisubBuildTime() {
	return __DATE__ " " __TIME__;
}

const char *GetAegisubBuildCredit() {
	return BUILD_CREDIT;
}
#endif

bool GetIsOfficialRelease() {
#ifdef AEGI_OFFICIAL_RELEASE
	return AEGI_OFFICIAL_RELEASE;
#else
	return false;
#endif
}

const char *GetVersionNumber() {
	return BUILD_GIT_VERSION_STRING;
}

int GetSVNRevision() {
#ifdef BUILD_GIT_VERSION_NUMBER
	return BUILD_GIT_VERSION_NUMBER;
#else
	return 0;
#endif
}

/// @brief 判断后缀是否为补丁发布标记（高于同版本号的正式版）
/// @details 提取后缀中的字母前缀部分进行匹配，支持 fix2、patch3 等带数字的复合后缀
static bool IsPostReleaseSuffix(const std::string& suffix) {
	std::string prefix;
	for (char c : suffix) {
		if (std::isalpha(static_cast<unsigned char>(c)))
			prefix.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		else
			break;
	}
	// 将常见的后缀（包括 feature/feat）视为补丁类发布标记（高于同版本号的正式版）
	return prefix == "fix" || prefix == "patch" || prefix == "hotfix" || prefix == "rev" || prefix == "feature" || prefix == "feat";
}

/// @brief 获取后缀的优先级权重
/// @return 补丁发布 = 1，正式版（空后缀）= 0，预发布 = -1
static int GetSuffixWeight(const std::string& suffix) {
	if (suffix.empty()) return 0;
	return IsPostReleaseSuffix(suffix) ? 1 : -1;
}

bool IsNewerVersion(const std::string& remote, const std::string& local) {
	auto strip_v = [](const std::string& s) -> std::string {
		if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
			return s.substr(1);
		return s;
	};

	auto split_pre = [](const std::string& s) -> std::pair<std::string, std::string> {
		auto pos = s.find('-');
		if (pos != std::string::npos)
			return {s.substr(0, pos), s.substr(pos + 1)};
		return {s, ""};
	};

	auto parse_ver = [](const std::string& v) -> std::vector<int> {
		std::vector<int> parts;
		std::istringstream iss(v);
		std::string part;
		while (std::getline(iss, part, '.')) {
			try { parts.push_back(std::stoi(part)); }
			catch (...) { parts.push_back(0); }
		}
		while (parts.size() < 3) parts.push_back(0);
		return parts;
	};

	std::string r = strip_v(remote);
	std::string l = strip_v(local);

	auto [r_ver, r_pre] = split_pre(r);
	auto [l_ver, l_pre] = split_pre(l);

	auto rv = parse_ver(r_ver);
	auto lv = parse_ver(l_ver);

	for (size_t i = 0; i < 3; ++i) {
		if (rv[i] > lv[i]) return true;
		if (rv[i] < lv[i]) return false;
	}

	// 版本号相同时，按后缀权重比较：预发布(-1) < 正式版(0) < 补丁发布(1)
	int rw = GetSuffixWeight(r_pre);
	int lw = GetSuffixWeight(l_pre);
	if (rw != lw) return rw > lw;

	// 同类后缀：分离字母前缀和尾部数字，先按前缀字典序，再按数字升序
	auto split_suffix = [](const std::string& s) -> std::pair<std::string, int> {
		size_t num_start = s.size();
		while (num_start > 0 && std::isdigit(static_cast<unsigned char>(s[num_start - 1])))
			--num_start;
		int num = 0;
		if (num_start < s.size()) {
			try { num = std::stoi(s.substr(num_start)); }
			catch (...) {}
		}
		std::string alpha = s.substr(0, num_start);
		for (char& c : alpha)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return {alpha, num};
	};

	auto [r_alpha, r_num] = split_suffix(r_pre);
	auto [l_alpha, l_num] = split_suffix(l_pre);
	if (r_alpha != l_alpha) return r_alpha > l_alpha;
	return r_num > l_num;
}
