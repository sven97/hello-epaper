#include <unity.h>
#include "logic/url_template.h"

void setUp() {}
void tearDown() {}

void test_replaces_all_tokens() {
    std::string out = renderUrlTemplate(
        "https://x/{width}/{height}?r={seed}", 42UL, 1200, 1600);
    TEST_ASSERT_EQUAL_STRING("https://x/1200/1600?r=42", out.c_str());
}

void test_repeated_token() {
    std::string out = renderUrlTemplate("{seed}-{seed}", 7UL, 1, 1);
    TEST_ASSERT_EQUAL_STRING("7-7", out.c_str());
}

void test_no_tokens_passthrough() {
    std::string out = renderUrlTemplate("https://example.com/pic.jpg", 9UL, 1200, 1600);
    TEST_ASSERT_EQUAL_STRING("https://example.com/pic.jpg", out.c_str());
}

void test_landscape_dims() {
    std::string out = renderUrlTemplate("{width}x{height}", 0UL, 1600, 1200);
    TEST_ASSERT_EQUAL_STRING("1600x1200", out.c_str());
}

void test_empty_template() {
    TEST_ASSERT_EQUAL_STRING("", renderUrlTemplate("", 1UL, 2, 3).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_replaces_all_tokens);
    RUN_TEST(test_repeated_token);
    RUN_TEST(test_no_tokens_passthrough);
    RUN_TEST(test_landscape_dims);
    RUN_TEST(test_empty_template);
    return UNITY_END();
}
