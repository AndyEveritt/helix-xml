/**
 * @file test_style_record.c
 *
 * Style records the application owns (`lv_xml_style_init()`,
 * `lv_xml_style_set_props()`, `lv_xml_style_deinit()`): an application theme
 * keeps one per style it builds from XML attributes, outside any scope.
 *
 * Also `radius="circle"`, which themes and widgets use for pills and dots.
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

static lv_style_value_t get_prop(lv_style_t * style, lv_style_prop_t prop)
{
    lv_style_value_t v = {0};
    TEST_ASSERT_EQUAL_INT(LV_STYLE_RES_FOUND, lv_style_get_prop(style, prop, &v));
    return v;
}

static void test_a_record_takes_style_attributes(void)
{
    lv_xml_style_t rec;
    lv_xml_style_init(&rec, "card");
    const char * attrs[] = {"bg_color", "0x123456", "pad_all", "7", "radius", "circle", NULL, NULL};
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_style_set_props(&rec, NULL, attrs));

    TEST_ASSERT_EQUAL_HEX32(0x123456, lv_color_to_u32(get_prop(&rec.style, LV_STYLE_BG_COLOR).color) & 0xFFFFFF);
    TEST_ASSERT_EQUAL_INT(7, get_prop(&rec.style, LV_STYLE_PAD_LEFT).num);
    TEST_ASSERT_EQUAL_INT(LV_RADIUS_CIRCLE, get_prop(&rec.style, LV_STYLE_RADIUS).num);

    /* A second call adds to and overwrites the first */
    const char * more[] = {"pad_all", "2", "border_width", "3", NULL, NULL};
    lv_xml_style_set_props(&rec, NULL, more);
    TEST_ASSERT_EQUAL_INT(2, get_prop(&rec.style, LV_STYLE_PAD_LEFT).num);
    TEST_ASSERT_EQUAL_INT(3, get_prop(&rec.style, LV_STYLE_BORDER_WIDTH).num);
    TEST_ASSERT_EQUAL_HEX32(0x123456, lv_color_to_u32(get_prop(&rec.style, LV_STYLE_BG_COLOR).color) & 0xFFFFFF);

    lv_xml_style_deinit(&rec);
    TEST_ASSERT_NULL(rec.name);
}

static void test_a_record_owns_its_transition(void)
{
    lv_xml_style_t rec;
    lv_xml_style_init(&rec, "dragging");
    const char * attrs[] = {"transition", "outline_width 80ms ease_in_out", NULL, NULL};
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_style_set_props(&rec, NULL, attrs));

    const lv_style_transition_dsc_t * t = get_prop(&rec.style, LV_STYLE_TRANSITION).ptr;
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_PTR(rec.trans_dsc, t);
    TEST_ASSERT_EQUAL_UINT32(80, t->time);
    TEST_ASSERT_EQUAL_INT(LV_STYLE_OUTLINE_WIDTH, t->props[0]);

    /* Merging copies the pointer; the record keeps owning the descriptor */
    lv_style_t applied;
    lv_style_init(&applied);
    lv_style_merge(&applied, &rec.style);
    TEST_ASSERT_EQUAL_PTR(t, get_prop(&applied, LV_STYLE_TRANSITION).ptr);
    lv_style_reset(&applied);

    lv_xml_style_deinit(&rec); /* frees the descriptor; ASan checks nothing leaks */
}

static void test_inline_radius_accepts_circle(void)
{
    ASSERT_XML_REGISTERS("pill",
                         "<component>"
                         "  <view extends='lv_obj' style_radius='circle'/>"
                         "</component>");
    lv_obj_t * v = XML_CREATE(lv_screen_active(), "pill", NULL);
    ASSERT_STYLE_INT(v, LV_STYLE_RADIUS, LV_PART_MAIN, LV_RADIUS_CIRCLE);
    lv_obj_delete(v);
}

static void test_grid_tracks_can_fit_their_content(void)
{
    ASSERT_XML_REGISTERS("grid",
                         "<component>"
                         "  <view extends='lv_obj' style_layout='grid'"
                         "        style_grid_column_dsc_array='content fr(9) 40'"
                         "        style_grid_row_dsc_array='content fr(1)'/>"
                         "</component>");
    lv_obj_t * v = XML_CREATE(lv_screen_active(), "grid", NULL);
    const int32_t * cols = lv_obj_get_style_grid_column_dsc_array(v, LV_PART_MAIN);
    TEST_ASSERT_NOT_NULL(cols);
    TEST_ASSERT_EQUAL_INT(LV_GRID_CONTENT, cols[0]);
    TEST_ASSERT_EQUAL_INT(LV_GRID_FR(9), cols[1]);
    TEST_ASSERT_EQUAL_INT(40, cols[2]);
    TEST_ASSERT_EQUAL_INT(LV_GRID_TEMPLATE_LAST, cols[3]);
    const int32_t * rows = lv_obj_get_style_grid_row_dsc_array(v, LV_PART_MAIN);
    TEST_ASSERT_EQUAL_INT(LV_GRID_CONTENT, rows[0]);
    lv_obj_delete(v);
}

static void test_a_registered_style_still_extends(void)
{
    const char * first[] = {"name", "s", "pad_all", "4", NULL, NULL};
    const char * second[] = {"name", "s", "border_width", "1", NULL, NULL};
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_register_style(NULL, first));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_register_style(NULL, second));
    lv_xml_style_t * xs = lv_xml_get_style_by_name(NULL, "s");
    TEST_ASSERT_NOT_NULL(xs);
    TEST_ASSERT_EQUAL_INT(4, get_prop(&xs->style, LV_STYLE_PAD_TOP).num);
    TEST_ASSERT_EQUAL_INT(1, get_prop(&xs->style, LV_STYLE_BORDER_WIDTH).num);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_record_takes_style_attributes);
    RUN_TEST(test_a_record_owns_its_transition);
    RUN_TEST(test_inline_radius_accepts_circle);
    RUN_TEST(test_grid_tracks_can_fit_their_content);
    RUN_TEST(test_a_registered_style_still_extends);
    return UNITY_END();
}
