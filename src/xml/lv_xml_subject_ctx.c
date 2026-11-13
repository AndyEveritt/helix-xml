/**
 * @file lv_xml_subject_ctx.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Andy Everitt
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_subject_ctx.h"
#include <lvgl.h>
#if LV_USE_XML

#include "lv_xml.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    char * name;
    lv_subject_t * subject;
} subject_entry_t;

struct _lv_xml_subject_ctx_t {
    lv_xml_subject_ctx_t * parent;  /**< holds a reference */
    lv_ll_t entries;                /**< subject_entry_t */
    uint32_t ref_cnt;
    void * user_data;               /**< owned by the application */
    lv_xml_subject_resolver_cb_t resolver; /**< creates subjects on demand, or NULL */
    void * resolver_data;           /**< passed to `resolver` */
};

/**********************
 *  STATIC PROTOTYPES
 **********************/
static subject_entry_t * find_local(const lv_xml_subject_ctx_t * ctx, const char * name);
static void ctx_free(lv_xml_subject_ctx_t * ctx);

/**********************
 *  STATIC VARIABLES
 **********************/

/* LVGL and the XML engine are single threaded, as is every other piece of
 * engine state. */
static lv_xml_subject_ctx_t * active_ctx;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_xml_subject_ctx_t * lv_xml_subject_ctx_create(lv_xml_subject_ctx_t * parent)
{
    lv_xml_subject_ctx_t * ctx = lv_malloc_zeroed(sizeof(lv_xml_subject_ctx_t));
    if(ctx == NULL) {
        LV_LOG_ERROR("OOM: failed to allocate subject context");
        return NULL;
    }
    lv_ll_init(&ctx->entries, sizeof(subject_entry_t));
    ctx->ref_cnt = 1;
    ctx->parent = lv_xml_subject_ctx_ref(parent);
    return ctx;
}

void lv_xml_subject_ctx_delete(lv_xml_subject_ctx_t * ctx)
{
    lv_xml_subject_ctx_unref(ctx);
}

lv_xml_subject_ctx_t * lv_xml_subject_ctx_ref(lv_xml_subject_ctx_t * ctx)
{
    if(ctx) ctx->ref_cnt++;
    return ctx;
}

void lv_xml_subject_ctx_unref(lv_xml_subject_ctx_t * ctx)
{
    /* Iterative over the parent chain: releasing the last reference to a deep
     * chain must not recurse once per level. */
    while(ctx) {
        LV_ASSERT_MSG(ctx->ref_cnt > 0, "subject context released more often than retained");
        if(ctx->ref_cnt == 0) return;
        if(--ctx->ref_cnt > 0) return;

        lv_xml_subject_ctx_t * parent = ctx->parent;
        if(active_ctx == ctx) {
            /* Freed while still active means a caller forgot to restore; fail
             * safe rather than leave a dangling pointer in every lookup. */
            LV_LOG_WARN("subject context freed while active");
            active_ctx = NULL;
        }
        ctx_free(ctx);
        ctx = parent;
    }
}

lv_result_t lv_xml_subject_ctx_add(lv_xml_subject_ctx_t * ctx, const char * name, lv_subject_t * subject)
{
    if(ctx == NULL || name == NULL || subject == NULL) return LV_RESULT_INVALID;

    subject_entry_t * e = find_local(ctx, name);
    if(e) {
        e->subject = subject;
        return LV_RESULT_OK;
    }

    e = lv_ll_ins_tail(&ctx->entries);
    if(e == NULL) {
        LV_LOG_ERROR("OOM: failed to add subject `%s` to context", name);
        return LV_RESULT_INVALID;
    }
    e->name = lv_strdup(name);
    if(e->name == NULL) {
        lv_ll_remove(&ctx->entries, e);
        lv_free(e);
        LV_LOG_ERROR("OOM: failed to add subject `%s` to context", name);
        return LV_RESULT_INVALID;
    }
    e->subject = subject;
    return LV_RESULT_OK;
}

lv_result_t lv_xml_subject_ctx_remove(lv_xml_subject_ctx_t * ctx, const char * name)
{
    if(ctx == NULL || name == NULL) return LV_RESULT_INVALID;
    subject_entry_t * e = find_local(ctx, name);
    if(e == NULL) return LV_RESULT_INVALID;
    lv_free(e->name);
    lv_ll_remove(&ctx->entries, e);
    lv_free(e);
    return LV_RESULT_OK;
}

void lv_xml_subject_ctx_set_resolver(lv_xml_subject_ctx_t * ctx, lv_xml_subject_resolver_cb_t cb, void * user_data)
{
    if(ctx == NULL) return;
    ctx->resolver = cb;
    ctx->resolver_data = user_data;
}

lv_subject_t * lv_xml_subject_ctx_find(const lv_xml_subject_ctx_t * ctx, const char * name)
{
    if(name == NULL) return NULL;
    const lv_xml_subject_ctx_t * origin = ctx;
    for(; ctx; ctx = ctx->parent) {
        subject_entry_t * e = find_local(ctx, name);
        if(e) return e->subject;
        if(ctx->resolver) {
            lv_subject_t * resolved = ctx->resolver(origin, name, ctx->resolver_data);
            if(resolved) return resolved;
        }
    }
    return NULL;
}

void lv_xml_subject_ctx_set_user_data(lv_xml_subject_ctx_t * ctx, void * user_data)
{
    if(ctx) ctx->user_data = user_data;
}

void * lv_xml_subject_ctx_get_user_data(const lv_xml_subject_ctx_t * ctx)
{
    return ctx ? ctx->user_data : NULL;
}

lv_xml_subject_ctx_t * lv_xml_subject_ctx_get_parent(const lv_xml_subject_ctx_t * ctx)
{
    return ctx ? ctx->parent : NULL;
}

lv_xml_subject_ctx_t * lv_xml_subject_ctx_get_active(void)
{
    return active_ctx;
}

lv_xml_subject_ctx_t * lv_xml_subject_ctx_activate(lv_xml_subject_ctx_t * ctx)
{
    lv_xml_subject_ctx_t * prev = active_ctx;
    active_ctx = ctx;
    return prev;
}

void lv_xml_subject_ctx_restore(lv_xml_subject_ctx_t * prev)
{
    active_ctx = prev;
}

void * lv_xml_create_with_ctx(lv_obj_t * parent, const char * name, const char ** attrs,
                              lv_xml_subject_ctx_t * ctx)
{
    /* Hold a reference for the duration: a widget's create callback could
     * otherwise release the last one mid-build. */
    lv_xml_subject_ctx_ref(ctx);
    lv_xml_subject_ctx_t * prev = lv_xml_subject_ctx_activate(ctx);
    void * obj = lv_xml_create(parent, name, attrs);
    lv_xml_subject_ctx_restore(prev);
    lv_xml_subject_ctx_unref(ctx);
    return obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static subject_entry_t * find_local(const lv_xml_subject_ctx_t * ctx, const char * name)
{
    subject_entry_t * e;
    LV_LL_READ(&ctx->entries, e) {
        if(lv_streq(e->name, name)) return e;
    }
    return NULL;
}

static void ctx_free(lv_xml_subject_ctx_t * ctx)
{
    subject_entry_t * e;
    LV_LL_READ(&ctx->entries, e) {
        lv_free(e->name);
    }
    lv_ll_clear(&ctx->entries);
    lv_free(ctx);
}

#endif /* LV_USE_XML */
