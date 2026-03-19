#include <main.h>
#include "../../src/version.h"

TEST(VersionCheck, BasicHigherMajor) {
	EXPECT_TRUE(IsNewerVersion("4.0.0", "3.4.4"));
}

TEST(VersionCheck, BasicHigherMinor) {
	EXPECT_TRUE(IsNewerVersion("3.5.0", "3.4.4"));
}

TEST(VersionCheck, BasicHigherPatch) {
	EXPECT_TRUE(IsNewerVersion("3.4.5", "3.4.4"));
}

TEST(VersionCheck, SameVersionNotNewer) {
	EXPECT_FALSE(IsNewerVersion("3.4.4", "3.4.4"));
}

TEST(VersionCheck, LowerVersionNotNewer) {
	EXPECT_FALSE(IsNewerVersion("3.4.3", "3.4.4"));
}

TEST(VersionCheck, VPrefixStripped) {
	EXPECT_TRUE(IsNewerVersion("v3.4.5", "3.4.4"));
	EXPECT_TRUE(IsNewerVersion("V3.4.5", "v3.4.4"));
}

TEST(VersionCheck, PreReleaseLowerThanRelease) {
	EXPECT_TRUE(IsNewerVersion("3.4.4", "3.4.4-RC1"));
	EXPECT_FALSE(IsNewerVersion("3.4.4-RC1", "3.4.4"));
}

TEST(VersionCheck, PreReleaseOrdering) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-RC2", "3.4.4-RC1"));
	EXPECT_FALSE(IsNewerVersion("3.4.4-RC1", "3.4.4-RC2"));
}

TEST(VersionCheck, FixSuffixHigherThanRelease) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-fix", "3.4.4"));
	EXPECT_FALSE(IsNewerVersion("3.4.4", "3.4.4-fix"));
}

TEST(VersionCheck, PatchSuffixHigherThanRelease) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-patch", "3.4.4"));
}

TEST(VersionCheck, HotfixSuffixHigherThanRelease) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-hotfix", "3.4.4"));
}

TEST(VersionCheck, RevSuffixHigherThanRelease) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-rev", "3.4.4"));
}

TEST(VersionCheck, FixSuffixCaseInsensitive) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-FIX", "3.4.4"));
	EXPECT_TRUE(IsNewerVersion("3.4.4-Fix", "3.4.4"));
}

TEST(VersionCheck, FixHigherThanPreRelease) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-fix", "3.4.4-RC1"));
}

TEST(VersionCheck, PreReleaseLowerThanFix) {
	EXPECT_FALSE(IsNewerVersion("3.4.4-RC1", "3.4.4-fix"));
}

TEST(VersionCheck, HigherMajorBeatsFixSuffix) {
	EXPECT_TRUE(IsNewerVersion("3.5.0", "3.4.4-fix"));
}

TEST(VersionCheck, CompoundFixSuffix) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-fix2", "3.4.4"));
	EXPECT_TRUE(IsNewerVersion("3.4.4-fix2", "3.4.4-fix1"));
	EXPECT_TRUE(IsNewerVersion("3.4.4-patch3", "3.4.4"));
	EXPECT_TRUE(IsNewerVersion("3.4.4-hotfix2", "3.4.4"));
	EXPECT_TRUE(IsNewerVersion("3.4.4-rev2", "3.4.4"));
}

TEST(VersionCheck, NumericSuffixOrdering) {
	EXPECT_TRUE(IsNewerVersion("3.4.4-fix10", "3.4.4-fix9"));
	EXPECT_TRUE(IsNewerVersion("3.4.4-RC10", "3.4.4-RC2"));
	EXPECT_FALSE(IsNewerVersion("3.4.4-fix9", "3.4.4-fix10"));
}

TEST(VersionCheck, HigherPatchPreReleaseNewerThanLowerPatch) {
	EXPECT_TRUE(IsNewerVersion("3.4.5-RC1", "3.4.4"));
	EXPECT_TRUE(IsNewerVersion("3.4.5-RC1", "3.4.4-fix"));
}
