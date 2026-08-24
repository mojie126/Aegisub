// Copyright (c) 2026, Aegisub contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
// THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
// DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
// PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
// ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
// THIS SOFTWARE.

/// @file visual_tool_drag_selection.h
/// @brief 拖拽工具特征重建时的选择状态恢复逻辑
///
/// 字幕每次提交都会全量重建拖拽特征并丢弃特征级选择，
/// 此处以 (行唯一 Id, 特征类型) 为身份键保存并在重建后回填。
/// 行唯一 Id 跨撤销快照稳定（快照以 AssDialogueBase 切片存储，恢复时保留 Id），
/// 行被删除或不再显示时匹配自然失败，即退化为清空选择的旧行为。
///
/// @see visual_tool_drag.cpp :: VisualToolDrag::OnFileChanged()
/// @ingroup visual_ts

#pragma once

#include <optional>
#include <set>
#include <utility>

namespace visual_tool_drag_detail {
	/// 特征的选择身份：所属行唯一 ID 与特征类型值
	struct FeatureSelectionIdentity {
		int line_id = -1; ///< 行唯一 ID，跨提交与撤销快照稳定
		int feature_type = 0; ///< 特征类型值，同一行的各角色类型互异

		friend bool operator==(FeatureSelectionIdentity const &, FeatureSelectionIdentity const &) = default;

		friend auto operator<=>(FeatureSelectionIdentity const &, FeatureSelectionIdentity const &) = default;
	};

	/// @brief 提取选中特征集与主特征的身份键，供特征重建后恢复选择
	/// @param primary 最近点击的主特征，可为空指针
	/// @param sel_features 当前选中的特征指针集合
	/// @return 身份键集合与主特征身份键（无有效主特征时为 nullopt）
	///
	/// 特征类型需提供 line 成员（指向含 Id 成员的行对象）与 type 成员
	template<typename FeatureT, typename SelSet>
	std::pair<std::set<FeatureSelectionIdentity>, std::optional<FeatureSelectionIdentity>>
		CollectDragSelectionIdentity(FeatureT const *primary, SelSet const &sel_features) {
		std::set<FeatureSelectionIdentity> identities;
		for (auto const *feat : sel_features) {
			if (feat && feat->line)
				identities.insert({feat->line->Id, static_cast<int>(feat->type)});
		}

		std::optional<FeatureSelectionIdentity> primary_identity;
		if (primary && primary->line)
			primary_identity = FeatureSelectionIdentity{primary->line->Id, static_cast<int>(primary->type)};

		return {std::move(identities), primary_identity};
	}

	/// @brief 将身份键回填到重建后的特征集合，恢复选中集与主特征引用
	/// @param features 重建后的特征列表，元素需含 line 与 type 成员
	/// @param identities 重建前提取的身份键集合
	/// @param primary_identity 重建前的主特征身份键，可为 nullopt
	/// @param sel_features_out 输出：恢复后的特征指针集合
	/// @param primary_out 输出：恢复后的主特征指针，未命中时置空
	template<typename FeatureRange, typename SelSet, typename FeatureT>
	void RestoreDragSelection(FeatureRange &features,
							std::set<FeatureSelectionIdentity> const &identities,
							std::optional<FeatureSelectionIdentity> const &primary_identity,
							SelSet &sel_features_out, FeatureT *&primary_out) {
		primary_out = nullptr;
		for (auto &feat : features) {
			if (!feat.line) continue;

			FeatureSelectionIdentity identity{feat.line->Id, static_cast<int>(feat.type)};
			if (identities.contains(identity))
				sel_features_out.insert(&feat);
			if (primary_identity && *primary_identity == identity)
				primary_out = &feat;
		}
	}
} // namespace visual_tool_drag_detail
