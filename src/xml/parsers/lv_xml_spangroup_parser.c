/**
 * @file lv_xml_spangroup_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_spangroup_parser.h"
#if LV_USE_XML && LV_USE_SPAN

#include <lvgl.h>
#include <lvgl_private.h>
#include "../lv_xml_private.h"

/*********************
 *      DEFINES
 *********************/
#ifndef LV_XML_HAVE_SPAN_STYLE_STATIC
    /* Set to 1 when building against an LVGL that has lv_spangroup_set_span_style_static() */
    #define LV_XML_HAVE_SPAN_STYLE_STATIC 0
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_span_overflow_t spangroup_overflow_to_enum(const char * txt);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void * lv_xml_spangroup_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_spangroup_create(lv_xml_state_get_parent(state));

    return item;
}


void lv_xml_spangroup_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    lv_xml_attr_check_enter(state);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("overflow", name)) lv_spangroup_set_overflow(item, spangroup_overflow_to_enum(value));
        else if(lv_streq("max_lines", name)) lv_spangroup_set_max_lines(item, lv_xml_atoi(value));
        else if(lv_streq("indent", name)) lv_spangroup_set_indent(item, lv_xml_atoi(value));
        else lv_xml_attr_check_miss(state, name);
    }
}

void * lv_xml_spangroup_span_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_spangroup_add_span(lv_xml_state_get_parent(state));
    return item;
}

void lv_xml_spangroup_span_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(state);
    LV_UNUSED(attrs);

    lv_obj_t * spangroup = lv_xml_state_get_parent(state);
    lv_span_t * span = lv_xml_state_get_item(state);

    lv_xml_attr_check_enter(state);
    /* Read out of band from inside the bind_text branch, so no if/else-if arm
     * ever matches its name. */
    lv_xml_attr_check_consume(state, "bind_text-fmt");

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("text", name)) lv_spangroup_set_span_text(spangroup, span, value);
        else if(lv_streq("style", name)) {
            lv_xml_style_t * style_dsc = lv_xml_get_style_by_name(&state->scope, value);
            if(style_dsc == NULL) {
                LV_LOG_WARN("No style \"%s\" for a spangroup span", value);
            }
#if LV_XML_HAVE_SPAN_STYLE_STATIC
            else if(style_dsc->external) {
                /* An application style is borrowed, like everywhere else: the span keeps
                 * pointing at it, so rewriting it (a theme switch) restyles the span. Needs
                 * an LVGL with lv_spangroup_set_span_style_static(); without it the span
                 * gets a copy, as below. */
                lv_spangroup_set_span_style_static(spangroup, span, style_dsc->external);
            }
#endif
            else {
                lv_spangroup_set_span_style(spangroup, span, lv_xml_style_get_lv_style(style_dsc));
            }
        }
        else if(lv_streq("bind_text", name)) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject == NULL) {
                LV_LOG_WARN("Subject \"%s\" doesn't exist in spangroup span bind_text", value);
                continue;
            }
            const char * fmt = lv_xml_get_value_of(attrs, "bind_text-fmt");
            if(fmt) {
                fmt = lv_strdup(fmt);
                lv_obj_add_event_cb(spangroup, lv_event_free_user_data_cb, LV_EVENT_DELETE, (void *) fmt);
            }
            lv_spangroup_bind_span_text(spangroup, span, subject, fmt);
        }
        else lv_xml_attr_check_miss(state, name);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_span_overflow_t spangroup_overflow_to_enum(const char * txt)
{
    if(lv_streq("clip", txt)) return LV_SPAN_OVERFLOW_CLIP;
    if(lv_streq("ellipsis", txt)) return LV_SPAN_OVERFLOW_ELLIPSIS;

    LV_LOG_WARN("%s is an unknown value for span's overflow", txt);
    return 0; /*Return 0 in lack of a better option. */
}

#endif /* LV_USE_XML */
