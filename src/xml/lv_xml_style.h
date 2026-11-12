/**
 * @file lv_xml_style.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_STYLE_H
#define LV_XML_STYLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <lvgl.h>
#include "lv_xml_types.h"

#if LV_USE_XML

/**********************
 *      TYPEDEFS
 **********************/

/** A style color property set from a global `#const`, kept so the style can
 *  follow a const change (lv_xml_reapply_style_tokens). */
typedef struct {
    lv_style_prop_t prop;
    char * const_name;
} lv_xml_style_token_t;

typedef struct _lv_xml_style_t {
    const char * name;
    const char * long_name;
    lv_style_t style;
    /* Non-NULL for a style registered with lv_xml_register_style_external():
     * the application owns this lv_style_t and the engine only hands out the
     * pointer. `style` above stays empty. Always read the style to apply through
     * lv_xml_style_get_lv_style(). */
    lv_style_t * external;
    /* Engine-owned transition. LVGL stores only a pointer in the style, and
     * lv_style_reset() does not follow it, so the record owns both allocations.
     * trans_authored_time is the pre-scale duration: scaling reads it rather
     * than the live value, so repeated scaling does not compound. */
    lv_style_transition_dsc_t * trans_dsc;
    lv_style_prop_t * trans_props;
    uint32_t trans_authored_time;
    lv_xml_style_token_t * tokens;
    uint32_t token_cnt;
} lv_xml_style_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Add a style to `ctx` and set the style properties from `attrs`
 * @param scope     add styles here. (Constants should be already added as style properties might use them)
 * @param attrs     list of attribute names and values
 */
lv_result_t lv_xml_register_style(lv_xml_component_scope_t * scope, const char ** attrs);

/**
 * Make an application-owned `lv_style_t` available to XML under `name`, so
 * `styles="name"`, `<style name=...>` references and `bind_style` can use a
 * style built in C. The engine borrows the pointer: it never initialises,
 * resets or frees it, and changes the application makes to it (e.g. a theme
 * switch followed by `lv_obj_report_style_change()`) reach every widget using
 * it. The style must outlive every widget it is applied to.
 * Registering a name that already exists replaces it for widgets created
 * afterwards. An external style cannot be extended with XML properties.
 * @param scope     scope to register in, or NULL for `globals`
 * @param name      style name (copied)
 * @param style     the application's style
 * @return          `LV_RESULT_OK` on success
 */
lv_result_t lv_xml_register_style_external(lv_xml_component_scope_t * scope, const char * name,
                                           lv_style_t * style);

/**
 * The `lv_style_t` an XML style record applies: the application's style for an
 * external registration, otherwise the engine-owned one.
 */
static inline lv_style_t * lv_xml_style_get_lv_style(lv_xml_style_t * xs)
{
    return xs->external ? xs->external : &xs->style;
}

/**
 * Decompose a string like `"style1:pressed:checked:knob"` to style name and selector
 * @param txt           the input string
 * @param selector      store the selectors here
 * @return              the style name or `NULL` on any error
 */
const char * lv_xml_style_string_process(char * txt, lv_style_selector_t * selector);

/**
 * Free the `#const` records of a style (its lv_style_t is left as it is).
 * @param xs    the style; NULL does nothing
 */
void lv_xml_style_tokens_clear(lv_xml_style_t * xs);

/**
 * Find a style by name which was added by `lv_xml_register_style`
 * @param scope     the default context to search in
 * @param name      the name of the style. Can start with a component name prefix (e.g. `my_button.blue`) to overwrite the ctx
 * @return          the style structure
 */
lv_xml_style_t * lv_xml_get_style_by_name(lv_xml_component_scope_t * scope, const char * name);

/**
 * Get a gradient descriptor defined for a component
 * @param scope component context where the gradient should be found
 * @param name  name of the gradient
 * @return      a gradient descriptor
 */
lv_grad_dsc_t * lv_xml_component_get_grad(lv_xml_component_scope_t * scope, const char * name);

/**
 * Drop the style's transition, freeing the engine-owned descriptor and its
 * property array. Safe on a style that has none.
 */
void lv_xml_style_transition_clear(lv_xml_style_t * xs);

/**
 * Scale every declared transition duration. 256 runs them as authored, 0
 * disables motion. Applies to descriptors already registered and to any
 * registered afterwards.
 * @param scale_256     the scale, as a fixed-point fraction of 256 (256 = 1.0)
 */
void lv_xml_set_transition_scale(uint32_t scale_256);

/**
 * Get the scale set by `lv_xml_set_transition_scale()`. 256 by default.
 * @return              the current scale, as a fixed-point fraction of 256
 */
uint32_t lv_xml_get_transition_scale(void);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_STYLE_H*/
