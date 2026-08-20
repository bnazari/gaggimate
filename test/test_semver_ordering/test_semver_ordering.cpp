// Host-native sanity check of the OTA updater's version ordering against this
// fork's release-tag scheme (vX.Y.Z-bnazari, patch = fork release counter).
// (this fork)
//
// What is actually under test: from_string() / operator> from
// lib/OTA/src/semver_extensions.cpp on top of the vendored semver.c.
// GitHubOTA decides "update available" via update_required(latest, current)
// (lib/OTA/src/common.cpp:98), which is a thin `latest > current` wrapper —
// common.cpp itself drags in HTTPClient/WiFi and is not compilable on the
// host, so these tests assert through operator> directly.
//
// Scheme rules these tests pin down:
//  - the patch number does all the ordering work; the -bnazari suffix is a
//    visual marker only
//  - never put a dot in the suffix: from_string() splits on '.' and reads
//    only the third field, so "-bnazari.1" silently parses as "-bnazari"
//  - a post-tag dev build (git describe "v1.8.2-bnazari-5-g1234") must NOT be
//    offered its own base tag as an update (no downgrade offer)

#include "semver_extensions.cpp"

#include <unity.h>

void setUp() {}
void tearDown() {}

// git-describe output as stamped into BUILD_GIT_VERSION, minus the leading
// "v" (WebUIPlugin strips it before from_string).
static semver_t parse(const char *s) { return from_string(s); }

static void test_parse_release_tag() {
    semver_t v = parse("1.8.2-bnazari");
    TEST_ASSERT_EQUAL_INT(1, v.major);
    TEST_ASSERT_EQUAL_INT(8, v.minor);
    TEST_ASSERT_EQUAL_INT(2, v.patch);
    TEST_ASSERT_NOT_NULL(v.prerelease);
    TEST_ASSERT_EQUAL_STRING("bnazari", v.prerelease);
}

static void test_parse_dev_build() {
    semver_t v = parse("1.8.1-163-g152b7a03-dirty");
    TEST_ASSERT_EQUAL_INT(1, v.major);
    TEST_ASSERT_EQUAL_INT(8, v.minor);
    TEST_ASSERT_EQUAL_INT(1, v.patch);
    TEST_ASSERT_EQUAL_STRING("163-g152b7a03-dirty", v.prerelease);
}

// The reason the tag scheme bans dots in the suffix: everything past the
// third dot-separated field is silently dropped, so -bnazari.1 and -bnazari.2
// would compare equal and no update would ever be offered.
static void test_dotted_suffix_is_dropped() {
    semver_t v1 = parse("1.8.2-bnazari.1");
    semver_t v2 = parse("1.8.2-bnazari.2");
    TEST_ASSERT_EQUAL_STRING("bnazari", v1.prerelease);
    TEST_ASSERT_FALSE(v2 > v1);
    TEST_ASSERT_FALSE(v1 > v2);
}

// Non-semver input (controller reporting "dev" → "ev" after the v-strip)
// parses as 0.0.0 instead of aborting — the July boot-loop guard.
static void test_non_semver_parses_as_zero() {
    semver_t v = parse("ev");
    TEST_ASSERT_EQUAL_INT(0, v.major);
    TEST_ASSERT_EQUAL_INT(0, v.minor);
    TEST_ASSERT_EQUAL_INT(0, v.patch);
    TEST_ASSERT_TRUE(parse("1.8.2-bnazari") > v);
}

// USB-flashed dev build (described from an upstream tag) must see the first
// fork release as an update.
static void test_first_fork_release_updates_dev_build() {
    TEST_ASSERT_TRUE(parse("1.8.2-bnazari") > parse("1.8.1-163-g152b7a03-dirty"));
}

// Running the tagged release: same tag is not an update, next patch is.
static void test_release_to_release_ordering() {
    TEST_ASSERT_FALSE(parse("1.8.2-bnazari") > parse("1.8.2-bnazari"));
    TEST_ASSERT_TRUE(parse("1.8.3-bnazari") > parse("1.8.2-bnazari"));
    TEST_ASSERT_FALSE(parse("1.8.2-bnazari") > parse("1.8.3-bnazari"));
}

// Dev build past a tag (git describe "v1.8.2-bnazari-5-g1234"): its own base
// tag must not come back as a (downgrade) offer, but the next release must.
static void test_post_tag_dev_build() {
    semver_t dev = parse("1.8.2-bnazari-5-g1234");
    TEST_ASSERT_FALSE(parse("1.8.2-bnazari") > dev);
    TEST_ASSERT_TRUE(parse("1.8.3-bnazari") > dev);
}

// Rebase onto a newer upstream base bumps minor: v1.9.1-bnazari succeeds
// v1.8.x-bnazari and any dev build in between.
static void test_rebase_bumps_minor() {
    TEST_ASSERT_TRUE(parse("1.9.1-bnazari") > parse("1.8.4-bnazari"));
    TEST_ASSERT_TRUE(parse("1.9.1-bnazari") > parse("1.8.4-bnazari-12-gabcd123"));
}

// Documented quirk of the vendored semver.c: a prerelease suffix sorts ABOVE
// the plain version (inverted from the semver spec, where 1.8.2-x < 1.8.2).
// Harmless for this fork's scheme (the patch counter always decides before
// prereleases are compared), but pinned here so a future lib swap that flips
// it gets noticed.
static void test_vendored_prerelease_sorts_above_plain() {
    TEST_ASSERT_TRUE(parse("1.8.2-bnazari") > parse("1.8.2"));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_release_tag);
    RUN_TEST(test_parse_dev_build);
    RUN_TEST(test_dotted_suffix_is_dropped);
    RUN_TEST(test_non_semver_parses_as_zero);
    RUN_TEST(test_first_fork_release_updates_dev_build);
    RUN_TEST(test_release_to_release_ordering);
    RUN_TEST(test_post_tag_dev_build);
    RUN_TEST(test_rebase_bumps_minor);
    RUN_TEST(test_vendored_prerelease_sorts_above_plain);
    return UNITY_END();
}
