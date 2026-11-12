/**
 * @file test_external_style.c
 *
 * `lv_xml_register_style_external()`: an `lv_style_t` the application owns,
 * made available to XML by name.
 *
 * XML applies a style with a `<style name="..." selector="..."/>` child
 * element (and `bind_style`); these tests use both.
 *
 * The property that matters is BORROWING. An application with a C theme keeps
 * its styles at fixed addresses and rewrites them on a theme switch. If the
 * engine copied the style at registration, XML widgets would keep the old
 * theme forever. These tests change the application's style after the widget
 * exists and check that the widget follows.
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/xml_assert.h"

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

static void test_style_element_applies_the_application_style(void)
{
    static lv_style_t card;
    lv_style_init(&card);
    lv_style_set_pad_all(&card, 12);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_register_style_external(NULL, "card", &card));

    ASSERT_XML_REGISTERS("panel",
                         "<component>"
                         "  <view extends='lv_obj' name='panel'><style name='card'/></view>"
                         "</component>");
    lv_obj_t * v = XML_CREATE(lv_screen_active(), "panel", NULL);
    ASSERT_STYLE_INT(v, LV_STYLE_PAD_TOP, LV_PART_MAIN, 12);

    /* A "theme switch": rewrite the application's style in place. */
    lv_style_set_pad_all(&card, 3);
    lv_obj_report_style_change(&card);
    ASSERT_STYLE_INT(v, LV_STYLE_PAD_TOP, LV_PART_MAIN, 3);

    lv_obj_delete(v);
    lv_style_reset(&card);
}

static void test_selector_applies_to_the_given_state(void)
{
    static lv_style_t pressed;
    lv_style_init(&pressed);
    lv_style_set_bg_opa(&pressed, 77);
    lv_xml_register_style_external(NULL, "pressed_bg", &pressed);

    ASSERT_XML_REGISTERS("btn",
                         "<component>"
                         "  <view extends='lv_obj' name='a'>"
                         "    <style name='pressed_bg' selector='pressed'/>"
                         "    <lv_obj name='b'><style name='pressed_bg' selector='pressed'/></lv_obj>"
                         "  </view>"
                         "</component>");
    lv_obj_t * v = XML_CREATE(lv_screen_active(), "btn", NULL);
    lv_obj_t * b = ASSERT_NAMED(v, "b");

    ASSERT_STYLE_INT(v, LV_STYLE_BG_OPA, LV_STATE_PRESSED, 77);
    ASSERT_STYLE_INT(b, LV_STYLE_BG_OPA, LV_STATE_PRESSED, 77);

    lv_obj_delete(v);
    lv_style_reset(&pressed);
}

static void test_bind_style_uses_the_application_style(void)
{
    static lv_style_t warn;
    lv_style_init(&warn);
    lv_style_set_border_width(&warn, 9);
    lv_xml_register_style_external(NULL, "warn", &warn);

    /* static: a failed assertion longjmps past the deinit below, and the
     * teardown would then delete a widget still observing a dead stack frame. */
    static lv_subject_t hot;
    lv_subject_init_int(&hot, 0);
    lv_xml_register_subject(NULL, "hot", &hot);

    ASSERT_XML_REGISTERS("temp",
                         "<component>"
                         "  <view extends='lv_obj' name='t'>"
                         "    <bind_style name='warn' subject='hot' ref_value='1'/>"
                         "  </view>"
                         "</component>");
    lv_obj_t * v = XML_CREATE(lv_screen_active(), "temp", NULL);
    /* No local border width: a local style property would outrank any added
     * style, bound or not. */
    const int32_t unbound = lv_obj_get_style_border_width(v, LV_PART_MAIN);
    TEST_ASSERT_NOT_EQUAL_INT32(9, unbound);
    lv_subject_set_int(&hot, 1);
    ASSERT_STYLE_INT(v, LV_STYLE_BORDER_WIDTH, LV_PART_MAIN, 9);
    lv_subject_set_int(&hot, 0);
    ASSERT_STYLE_INT(v, LV_STYLE_BORDER_WIDTH, LV_PART_MAIN, unbound);

    lv_obj_delete(v);
    lv_xml_unregister_subject(NULL, "hot");
    lv_subject_deinit(&hot);
    lv_style_reset(&warn);
}

static void test_component_scoped_external_style(void)
{
    ASSERT_XML_REGISTERS("lib",
                         "<component>"
                         "  <view extends='lv_obj'/>"
                         "</component>");
    static lv_style_t s;
    lv_style_init(&s);
    lv_style_set_pad_left(&s, 21);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          lv_xml_register_style_external(lv_xml_component_get_scope("lib"), "edge", &s));

    ASSERT_XML_REGISTERS("user",
                         "<component>"
                         "  <view extends='lv_obj' name='u'><style name='lib.edge'/></view>"
                         "</component>");
    lv_obj_t * v = XML_CREATE(lv_screen_active(), "user", NULL);
    ASSERT_STYLE_INT(v, LV_STYLE_PAD_LEFT, LV_PART_MAIN, 21);

    lv_obj_delete(v);
    lv_style_reset(&s);
}

static void test_xml_cannot_extend_an_application_style(void)
{
    static lv_style_t s;
    lv_style_init(&s);
    lv_style_set_pad_all(&s, 4);
    lv_xml_register_style_external(NULL, "fixed", &s);

    log_capture_start();
    const char * attrs[] = {"name", "fixed", "pad_all", "40", NULL, NULL};
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_register_style(NULL, attrs));
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("can't be extended"));

    /* And the application's style is untouched. */
    lv_style_value_t v;
    TEST_ASSERT_EQUAL_INT(LV_STYLE_RES_FOUND, lv_style_get_prop(&s, LV_STYLE_PAD_TOP, &v));
    TEST_ASSERT_EQUAL_INT32(4, v.num);

    lv_style_reset(&s);
}

static void test_reregistering_replaces_for_new_widgets(void)
{
    static lv_style_t first, second;
    lv_style_init(&first);
    lv_style_init(&second);
    lv_style_set_pad_top(&first, 1);
    lv_style_set_pad_top(&second, 2);

    lv_xml_register_style_external(NULL, "swap", &first);
    ASSERT_XML_REGISTERS("swapper",
                         "<component><view extends='lv_obj'><style name='swap'/></view></component>");
    lv_obj_t * a = XML_CREATE(lv_screen_active(), "swapper", NULL);
    lv_xml_register_style_external(NULL, "swap", &second);
    lv_obj_t * b = XML_CREATE(lv_screen_active(), "swapper", NULL);

    ASSERT_STYLE_INT(a, LV_STYLE_PAD_TOP, LV_PART_MAIN, 1);
    ASSERT_STYLE_INT(b, LV_STYLE_PAD_TOP, LV_PART_MAIN, 2);

    lv_obj_delete(a);
    lv_obj_delete(b);
    lv_style_reset(&first);
    lv_style_reset(&second);
}

static void test_rejects_null_arguments(void)
{
    static lv_style_t s;
    lv_style_init(&s);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_register_style_external(NULL, NULL, &s));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_register_style_external(NULL, "x", NULL));
    lv_style_reset(&s);
}

static void test_span_borrows_the_application_style(void)
{
#if !LV_XML_HAVE_SPAN_STYLE_STATIC
    TEST_IGNORE_MESSAGE("needs an LVGL with lv_spangroup_set_span_style_static()");
#else
    static lv_style_t loud;
    lv_style_init(&loud);
    lv_style_set_text_color(&loud, lv_color_hex(0x112233));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_register_style_external(NULL, "loud", &loud));

    ASSERT_XML_REGISTERS("spans",
                         "<component>"
                         "  <view extends='lv_spangroup'>"
                         "    <lv_spangroup-span text='T0:' style='loud'/>"
                         "  </view>"
                         "</component>");
    lv_obj_t * v = XML_CREATE(lv_screen_active(), "spans", NULL);
    lv_span_t * span = lv_spangroup_get_child(v, 0);
    TEST_ASSERT_NOT_NULL(span);
    TEST_ASSERT_EQUAL_PTR(&loud, lv_span_get_style(span)) ;

    lv_obj_delete(v);
    lv_style_reset(&loud);
#endif
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_span_borrows_the_application_style);
    RUN_TEST(test_style_element_applies_the_application_style);
    RUN_TEST(test_selector_applies_to_the_given_state);
    RUN_TEST(test_bind_style_uses_the_application_style);
    RUN_TEST(test_component_scoped_external_style);
    RUN_TEST(test_xml_cannot_extend_an_application_style);
    RUN_TEST(test_reregistering_replaces_for_new_widgets);
    RUN_TEST(test_rejects_null_arguments);
    return UNITY_END();
}
