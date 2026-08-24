/// @file drag_selection_restore.cpp
/// @brief 拖拽工具特征重建时选择状态恢复逻辑的单元测试
///
/// 被测对象为 visual_tool_drag_selection.h 中的纯模板函数，
/// 使用最小假特征类型实例化，验证身份键提取与回填行为。

#include <main.h>

#include "visual_tool_drag_selection.h"

#include <list>
#include <set>
#include <vector>

namespace {
	/// 行假对象：仅提供跨快照稳定的 Id
	struct FakeLine {
		int Id = 0;
	};

	/// 特征假对象：duck typing 满足 line 与 type 成员要求
	struct FakeFeature {
		FakeLine *line = nullptr;
		int type = 0;
	};

	/// 特征类型常量，模拟 Start/End/Origin 三种角色
	enum { F_START = 1, F_END = 2, F_ORIGIN = 3 };

	using namespace visual_tool_drag_detail;

	struct Fixture {
		std::list<FakeLine> lines;
		std::vector<std::unique_ptr<FakeFeature>> features;

		/// 添加一行并返回指针，行对象地址随 list 稳定
		FakeLine *AddLine(int id) {
			lines.push_back(FakeLine{id});
			return &lines.back();
		}

		/// 添加一个特征并返回裸指针（所有权由本夹具持有）
		FakeFeature *AddFeature(FakeLine *line, int type) {
			features.push_back(std::make_unique<FakeFeature>());
			auto *feat = features.back().get();
			feat->line = line;
			feat->type = type;
			return feat;
		}
	};
} // namespace

/// 多行多特征选中且主特征有效时应全部提取为身份键
TEST(DragSelectionRestore, Collect_IdentitiesFromSelectionAndPrimary) {
	Fixture f;
	auto *l1 = f.AddLine(101);
	auto *l2 = f.AddLine(202);
	auto *start1 = f.AddFeature(l1, F_START);
	auto *end2 = f.AddFeature(l2, F_END);

	std::set<FakeFeature *> sel{start1, end2};
	auto [identities, primary_id] = CollectDragSelectionIdentity(end2, sel);

	// 身份键常量避免花括号初始化列表中的逗号被断言宏误拆
	constexpr FeatureSelectionIdentity kLine1Start{101, F_START};
	constexpr FeatureSelectionIdentity kLine2End{202, F_END};

	EXPECT_EQ(2u, identities.size());
	EXPECT_NE(identities.end(), identities.find(kLine1Start));
	EXPECT_NE(identities.end(), identities.find(kLine2End));
	ASSERT_TRUE(primary_id.has_value());
	EXPECT_EQ(202, primary_id->line_id);
	EXPECT_EQ(F_END, primary_id->feature_type);
}

/// 主特征为空或无所属行时不应产生主身份键，空选中集应得到空集合
TEST(DragSelectionRestore, Collect_EmptyInputs) {
	Fixture f;
	auto *l1 = f.AddLine(1);
	auto *feat = f.AddFeature(l1, F_START);

	auto [ids_null_primary, primary_null] = CollectDragSelectionIdentity<FakeFeature>(nullptr, std::set<FakeFeature *>{feat});
	EXPECT_EQ(1u, ids_null_primary.size());
	EXPECT_FALSE(primary_null.has_value());

	// 主特征存在但未关联行：选择集与主键均为空
	auto *orphan = f.AddFeature(nullptr, F_END);
	auto [ids_orphan, primary_orphan] = CollectDragSelectionIdentity(orphan, std::set<FakeFeature *>{});
	EXPECT_TRUE(ids_orphan.empty());
	EXPECT_FALSE(primary_orphan.has_value());
}

/// 重建后同 Id 同类型的特征应被回填，主特征引用指向新对象
TEST(DragSelectionRestore, Restore_MatchesByIdentity) {
	Fixture f;
	auto old_l1 = f.AddLine(7);
	auto old_start = f.AddFeature(old_l1, F_START);
	auto old_end = f.AddFeature(old_l1, F_END);

	auto [identities, primary_id] = CollectDragSelectionIdentity(old_end, std::set<FakeFeature *>{old_start, old_end});

	// 模拟重建：旧行对象销毁，新行对象携带相同 Id
	f.features.clear();
	auto *new_line = new FakeLine{7};
	std::vector<FakeFeature> rebuilt{
		FakeFeature{new_line, F_START},
		FakeFeature{new_line, F_END},
	};

	std::set<FakeFeature *> restored_sel;
	FakeFeature *restored_primary = nullptr;
	RestoreDragSelection(rebuilt, identities, primary_id, restored_sel, restored_primary);

	EXPECT_EQ(2u, restored_sel.size());
	EXPECT_NE(restored_sel.end(), restored_sel.find(&rebuilt[0]));
	EXPECT_NE(restored_sel.end(), restored_sel.find(&rebuilt[1]));
	EXPECT_EQ(&rebuilt[1], restored_primary);
	delete new_line;
}

/// 行删除后身份键无法命中，选中集保持为空且主特征置空
TEST(DragSelectionRestore, Restore_LineRemovedDegeneratesToEmpty) {
	Fixture f;
	auto removed_line = f.AddLine(9);
	auto old_feat = f.AddFeature(removed_line, F_ORIGIN);

	auto [identities, primary_id] = CollectDragSelectionIdentity(old_feat, std::set<FakeFeature *>{old_feat});

	// 重建后的列表不含 Id=9 的行
	auto *other_line1 = new FakeLine{10};
	auto *other_line2 = new FakeLine{11};
	std::vector<FakeFeature> rebuilt{
		FakeFeature{other_line1, F_START},
		FakeFeature{other_line2, F_START},
	};

	std::set<FakeFeature *> restored_sel;
	FakeFeature *restored_primary = nullptr;
	RestoreDragSelection(rebuilt, identities, primary_id, restored_sel, restored_primary);

	EXPECT_TRUE(restored_sel.empty());
	EXPECT_EQ(nullptr, restored_primary);
	delete other_line1;
	delete other_line2;
}

/// 同 Id 但类型不同的特征不得误配，防止跨角色错误恢复
TEST(DragSelectionRestore, Restore_RoleMismatchNotRestored) {
	Fixture f;
	auto *old_line = f.AddLine(5);
	auto *old_origin = f.AddFeature(old_line, F_ORIGIN);

	auto [identities, primary_id] = CollectDragSelectionIdentity(old_origin, std::set<FakeFeature *>{old_origin});

	// 重建后该行只剩 Start 类型特征，Origin 已不存在
	auto *new_line = new FakeLine{5};
	std::vector<FakeFeature> rebuilt{
		FakeFeature{new_line, F_START},
	};

	std::set<FakeFeature *> restored_sel;
	FakeFeature *restored_primary = nullptr;
	RestoreDragSelection(rebuilt, identities, primary_id, restored_sel, restored_primary);

	EXPECT_TRUE(restored_sel.empty());
	EXPECT_EQ(nullptr, restored_primary);
	delete new_line;
}

/// 输入全空时提取与回填均应安全返回空结果
TEST(DragSelectionRestore, RoundTrip_AllEmpty) {
	auto [identities, primary_id] = CollectDragSelectionIdentity<FakeFeature>(nullptr, std::set<FakeFeature *>{});
	EXPECT_TRUE(identities.empty());
	EXPECT_FALSE(primary_id.has_value());

	std::vector<FakeFeature> rebuilt;
	std::set<FakeFeature *> restored_sel;
	FakeFeature *restored_primary = nullptr;
	RestoreDragSelection(rebuilt, identities, primary_id, restored_sel, restored_primary);
	EXPECT_TRUE(restored_sel.empty());
	EXPECT_EQ(nullptr, restored_primary);
}
