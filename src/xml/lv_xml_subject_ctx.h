/**
 * @file lv_xml_subject_ctx.h
 *
 * Subject contexts: named subjects that belong to one *instance* of a view
 * rather than to a component definition or to the global space.
 *
 * A component's `<subjects>` live in its scope, which is shared by every
 * instance built from it. That is the wrong shape for a list: ten instances of
 * `tool_card` must each bind `bind_text="name"` to *their own* tool's name. A
 * subject context provides that. The host creates a context, adds the
 * instance's subjects to it, and builds the view with
 * `lv_xml_create_with_ctx()`. While that view (and every component nested in
 * it) is being created, a subject name resolves in this order:
 *
 *   1. the subjects declared in the component's own scope
 *   2. the active context, then its parent, grandparent, ...
 *   3. the `globals` scope
 *
 * Contexts chain, so an item inside a list inside a screen can see the item's
 * subjects, then the screen's, then globals.
 *
 * Bindings resolve their subject once, at creation, so a context only has to
 * be active while a view is being built. Subject-bound `<repeat>` and `<if>`
 * rebuild later, asynchronously; they retain a reference to the context that
 * was active when they were created and re-activate it for the rebuild.
 *
 * Contexts are reference counted. `lv_xml_subject_ctx_delete()` drops the
 * creator's reference; a child context and every retained fragment hold their
 * own. Subjects are borrowed: a context never initialises, deinitialises or
 * frees a subject, and the caller must keep each subject alive for as long as
 * any widget is bound to it.
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Andy Everitt
 */

#ifndef LV_XML_SUBJECT_CTX_H
#define LV_XML_SUBJECT_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <lvgl.h>
#if LV_USE_XML

/**********************
 *      TYPEDEFS
 **********************/

typedef struct _lv_xml_subject_ctx_t lv_xml_subject_ctx_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a subject context.
 * @param parent    context to fall back to when a name is not found here, or NULL.
 *                  The new context holds a reference to it.
 * @return          the new context with a reference count of 1, or NULL on OOM
 */
lv_xml_subject_ctx_t * lv_xml_subject_ctx_create(lv_xml_subject_ctx_t * parent);

/**
 * Drop the creator's reference. The context is freed when the last reference
 * (child contexts, retained `<repeat>`/`<if>` fragments) is released.
 * @param ctx       context to release; NULL is ignored
 */
void lv_xml_subject_ctx_delete(lv_xml_subject_ctx_t * ctx);

/**
 * Take an additional reference.
 * @param ctx       context to retain; NULL is ignored
 * @return          `ctx`
 */
lv_xml_subject_ctx_t * lv_xml_subject_ctx_ref(lv_xml_subject_ctx_t * ctx);

/**
 * Release a reference taken with `lv_xml_subject_ctx_ref()`.
 * @param ctx       context to release; NULL is ignored
 */
void lv_xml_subject_ctx_unref(lv_xml_subject_ctx_t * ctx);

/**
 * Add a borrowed subject under `name`. A later add with the same name replaces
 * the earlier one; views already built keep the subject they bound to.
 * @param ctx       the context
 * @param name      name used by `bind_*`, `subject=` and expressions (copied)
 * @param subject   the subject; must outlive every widget bound to it
 * @return          `LV_RESULT_OK` on success
 */
lv_result_t lv_xml_subject_ctx_add(lv_xml_subject_ctx_t * ctx, const char * name, lv_subject_t * subject);

/**
 * Remove a subject name from this context only (not from its parents).
 * @param ctx       the context
 * @param name      name to remove
 * @return          `LV_RESULT_OK` if the name was found and removed
 */
lv_result_t lv_xml_subject_ctx_remove(lv_xml_subject_ctx_t * ctx, const char * name);

/**
 * Look a subject up in `ctx` and then in its parents.
 * @param ctx       start of the chain; NULL returns NULL
 * @param name      subject name
 * @return          the subject or NULL. Never logs.
 */
lv_subject_t * lv_xml_subject_ctx_find(const lv_xml_subject_ctx_t * ctx, const char * name);

/**
 * Attach application data to a context, e.g. the host object that owns it.
 * The engine never reads it. A host whose object dies before the context
 * (retained fragments can keep a context alive) should reset it to NULL.
 * @param ctx       the context
 * @param user_data any pointer, or NULL
 */
void lv_xml_subject_ctx_set_user_data(lv_xml_subject_ctx_t * ctx, void * user_data);

/**
 * Get the data set with `lv_xml_subject_ctx_set_user_data()`.
 * @param ctx       the context; NULL returns NULL
 * @return          the user data or NULL
 */
void * lv_xml_subject_ctx_get_user_data(const lv_xml_subject_ctx_t * ctx);

/**
 * Get the parent context.
 * @param ctx       the context
 * @return          the parent or NULL
 */
lv_xml_subject_ctx_t * lv_xml_subject_ctx_get_parent(const lv_xml_subject_ctx_t * ctx);

/**
 * The context subject lookups currently consult, or NULL outside
 * `lv_xml_create_with_ctx()` / `lv_xml_subject_ctx_activate()`.
 */
lv_xml_subject_ctx_t * lv_xml_subject_ctx_get_active(void);

/**
 * Make `ctx` the active context and return the one it replaces. Pair every
 * call with `lv_xml_subject_ctx_restore()`. Hosts normally use
 * `lv_xml_create_with_ctx()` instead; this is for code that creates widgets
 * through other paths (e.g. a custom widget's create callback).
 * @param ctx       context to activate (may be NULL to clear)
 * @return          the previously active context
 */
lv_xml_subject_ctx_t * lv_xml_subject_ctx_activate(lv_xml_subject_ctx_t * ctx);

/**
 * Restore the context returned by `lv_xml_subject_ctx_activate()`.
 * @param prev      the value `lv_xml_subject_ctx_activate()` returned
 */
void lv_xml_subject_ctx_restore(lv_xml_subject_ctx_t * prev);

/**
 * `lv_xml_create()` with `ctx` active for the duration of the creation.
 * @param parent    parent object
 * @param name      component or widget name
 * @param attrs     instance attributes, as for `lv_xml_create()`
 * @param ctx       subject context (may be NULL, which behaves like `lv_xml_create()`)
 * @return          the created object, or NULL on failure
 */
void * lv_xml_create_with_ctx(lv_obj_t * parent, const char * name, const char ** attrs,
                              lv_xml_subject_ctx_t * ctx);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* LV_XML_SUBJECT_CTX_H */
