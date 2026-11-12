/**
 * @file lv_xml_label_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_label_parser.h"
#if LV_USE_XML && LV_USE_LABEL

#include <lvgl.h>
#include <lvgl_private.h>
#include "../lv_xml_private.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void * lv_xml_label_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_label_create(lv_xml_state_get_parent(state));

    return item;
}

void lv_xml_label_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    lv_xml_attr_check_enter(state);
    /* Read out of band from inside the bind_text branch, so no if/else-if arm
     * ever matches its name. */
    lv_xml_attr_check_consume(state, "bind_text-fmt");

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("text", name)) lv_label_set_text(item, value);
        else if(lv_streq("long_mode", name)) lv_label_set_long_mode(item, lv_xml_label_long_mode_to_enum(value));
#if LV_USE_TRANSLATION
        else if(lv_streq("translation_tag", name)) {
            /*A component's unset translation_tag prop arrives as ""*/
            if(value[0] != '\0') lv_label_set_translation_tag(item, value);
        }
#endif
        else if(lv_streq("bind_text", name)) {
            /* Empty subject = intentional "no binding" (an optional subject left
             * at its empty default, same convention as the obj parser's binds).
             * Skip silently so the static text stands. */
            if(value[0] == '\0') continue;
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject == NULL) {
                LV_LOG_WARN("Subject \"%s\" doesn't exist in label bind_text", value);
                continue;
            }
            const char * fmt = lv_xml_get_value_of(attrs, "bind_text-fmt");
            if(fmt) {
                fmt = lv_strdup(fmt);
                lv_obj_add_event_cb(item, lv_event_free_user_data_cb, LV_EVENT_DELETE, (void *) fmt);
            }
            lv_label_bind_text(item, subject, fmt);
        }
        else lv_xml_attr_check_miss(state, name);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


#endif /* LV_USE_XML */
