/**
 * @file test_subject_ctx.c
 *
 * Subject contexts (src/xml/lv_xml_subject_ctx.h): per-INSTANCE subjects.
 *
 * The case this exists for is a list. Two instances of one `card` component
 * both say `bind_text="name"`, and each must bind to its own subject. A
 * component scope can't express that, because the scope is shared by every
 * instance. A context is created per instance and is active while that
 * instance is built.
 *
 * Pinned here:
 *  - two instances of one component bind to different subjects
 *  - resolution order: component scope, then the context chain, then globals
 *  - nested components and a parent context are visible from inside an item
 *  - the active context is restored after creation, including when nested
 *  - a subject-bound `<repeat>` / `<if>` rebuilds against the context it was
 *    created in, even after the creator dropped its reference (the fragment
 *    record holds its own)
 *  - a context's resolver is asked after its own subjects and before its
 *    parent, with the context the lookup started in; the global resolver is
 *    asked after the globals, with the active context
 *
 * NO ASSERTION HERE DEPENDS ON MEASURED GEOMETRY.
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>
#include <string.h>

#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

/*---------------------------------------------------------------------------
 * Helpers
 *--------------------------------------------------------------------------*/

/* Tests keep subjects in static storage: a failed assertion longjmps past the
 * deinit at the end of a test, and teardown would then delete widgets that
 * still observe a dead stack frame. */
typedef struct {
    lv_subject_t subject;
    char buf[32];
    char prev[32];
} str_subject_t;

static void str_init(str_subject_t * s, const char * value)
{
    lv_subject_init_string(&s->subject, s->buf, s->prev, sizeof(s->buf), value);
}

static const char * CARD_XML =
    "<component>"
    "  <view extends='lv_obj'>"
    "    <lv_label name='lbl' bind_text='name'/>"
    "  </view>"
    "</component>";

/*---------------------------------------------------------------------------
 * Tests
 *--------------------------------------------------------------------------*/

static void test_two_instances_bind_to_their_own_subjects(void)
{
    ASSERT_XML_REGISTERS("card", CARD_XML);

    static str_subject_t a, b;
    str_init(&a, "Fan 0");
    str_init(&b, "Fan 1");

    lv_xml_subject_ctx_t * ctx_a = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_t * ctx_b = lv_xml_subject_ctx_create(NULL);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_subject_ctx_add(ctx_a, "name", &a.subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_subject_ctx_add(ctx_b, "name", &b.subject));

    lv_obj_t * card_a = lv_xml_create_with_ctx(lv_screen_active(), "card", NULL, ctx_a);
    lv_obj_t * card_b = lv_xml_create_with_ctx(lv_screen_active(), "card", NULL, ctx_b);
    TEST_ASSERT_NOT_NULL(card_a);
    TEST_ASSERT_NOT_NULL(card_b);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(card_a, "lbl"), "Fan 0");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(card_b, "lbl"), "Fan 1");

    /* Binding is live, and only on its own instance. */
    lv_subject_copy_string(&b.subject, "Part cooling");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(card_a, "lbl"), "Fan 0");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(card_b, "lbl"), "Part cooling");

    lv_obj_delete(card_a);
    lv_obj_delete(card_b);
    lv_xml_subject_ctx_delete(ctx_a);
    lv_xml_subject_ctx_delete(ctx_b);
    lv_subject_deinit(&a.subject);
    lv_subject_deinit(&b.subject);
}

static void test_component_scope_wins_over_context_and_context_over_globals(void)
{
    ASSERT_XML_REGISTERS("shadow",
                         "<component>"
                         "  <subjects><string name='local' value='from scope'/></subjects>"
                         "  <view extends='lv_obj'>"
                         "    <lv_label name='local_lbl' bind_text='local'/>"
                         "    <lv_label name='ctx_lbl' bind_text='shared'/>"
                         "    <lv_label name='global_lbl' bind_text='only_global'/>"
                         "  </view>"
                         "</component>");

    static str_subject_t g_shared, g_only, c_local, c_shared;
    str_init(&g_shared, "global shared");
    str_init(&g_only, "global only");
    str_init(&c_local, "ctx local");
    str_init(&c_shared, "ctx shared");
    lv_xml_register_subject(NULL, "shared", &g_shared.subject);
    lv_xml_register_subject(NULL, "only_global", &g_only.subject);

    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_add(ctx, "local", &c_local.subject);
    lv_xml_subject_ctx_add(ctx, "shared", &c_shared.subject);

    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "shadow", NULL, ctx);
    TEST_ASSERT_NOT_NULL(v);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "local_lbl"), "from scope");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "ctx_lbl"), "ctx shared");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "global_lbl"), "global only");

    lv_obj_delete(v);
    lv_xml_subject_ctx_delete(ctx);
    lv_xml_unregister_subject(NULL, "shared");
    lv_xml_unregister_subject(NULL, "only_global");
    lv_subject_deinit(&g_shared.subject);
    lv_subject_deinit(&g_only.subject);
    lv_subject_deinit(&c_local.subject);
    lv_subject_deinit(&c_shared.subject);
}

static void test_without_a_context_lookup_is_unchanged(void)
{
    ASSERT_XML_REGISTERS("card", CARD_XML);

    static str_subject_t g;
    str_init(&g, "global name");
    lv_xml_register_subject(NULL, "name", &g.subject);

    lv_obj_t * v = XML_CREATE(lv_screen_active(), "card", NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "lbl"), "global name");

    lv_obj_t * v2 = lv_xml_create_with_ctx(lv_screen_active(), "card", NULL, NULL);
    TEST_ASSERT_NOT_NULL(v2);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v2, "lbl"), "global name");

    lv_obj_delete(v);
    lv_obj_delete(v2);
    lv_xml_unregister_subject(NULL, "name");
    lv_subject_deinit(&g.subject);
}

static void test_parent_context_is_searched_after_the_child(void)
{
    ASSERT_XML_REGISTERS("pair",
                         "<component>"
                         "  <view extends='lv_obj'>"
                         "    <lv_label name='item_lbl' bind_text='name'/>"
                         "    <lv_label name='screen_lbl' bind_text='title'/>"
                         "  </view>"
                         "</component>");

    static str_subject_t name, title, child_title;
    str_init(&name, "Tool 2");
    str_init(&title, "Tools");
    str_init(&child_title, "unused");

    lv_xml_subject_ctx_t * screen = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_add(screen, "title", &title.subject);
    lv_xml_subject_ctx_add(screen, "name", &child_title.subject);
    lv_xml_subject_ctx_t * item = lv_xml_subject_ctx_create(screen);
    lv_xml_subject_ctx_add(item, "name", &name.subject);

    TEST_ASSERT_EQUAL_PTR(screen, lv_xml_subject_ctx_get_parent(item));
    TEST_ASSERT_EQUAL_PTR(&name.subject, lv_xml_subject_ctx_find(item, "name"));
    TEST_ASSERT_EQUAL_PTR(&title.subject, lv_xml_subject_ctx_find(item, "title"));
    TEST_ASSERT_NULL(lv_xml_subject_ctx_find(screen, "nope"));

    /* The parent may be released by its creator while a child still uses it. */
    lv_xml_subject_ctx_delete(screen);

    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "pair", NULL, item);
    TEST_ASSERT_NOT_NULL(v);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "item_lbl"), "Tool 2");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "screen_lbl"), "Tools");

    lv_obj_delete(v);
    lv_xml_subject_ctx_delete(item); /* frees item, then the parent it kept alive */
    lv_subject_deinit(&name.subject);
    lv_subject_deinit(&title.subject);
    lv_subject_deinit(&child_title.subject);
}

static void test_nested_components_see_the_instance_context(void)
{
    ASSERT_XML_REGISTERS("name_label",
                         "<component>"
                         "  <view extends='lv_label' name='inner' bind_text='name'/>"
                         "</component>");
    ASSERT_XML_REGISTERS("outer",
                         "<component>"
                         "  <view extends='lv_obj'>"
                         "    <name_label/>"
                         "  </view>"
                         "</component>");

    static str_subject_t s;
    str_init(&s, "nested");
    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_add(ctx, "name", &s.subject);

    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "outer", NULL, ctx);
    TEST_ASSERT_NOT_NULL(v);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "inner"), "nested");

    lv_obj_delete(v);
    lv_xml_subject_ctx_delete(ctx);
    lv_subject_deinit(&s.subject);
}

static void test_active_context_is_restored_after_creation(void)
{
    ASSERT_XML_REGISTERS("card", CARD_XML);

    static str_subject_t s;
    str_init(&s, "x");
    lv_xml_subject_ctx_t * outer = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_t * inner = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_add(inner, "name", &s.subject);

    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_active());

    lv_xml_subject_ctx_t * prev = lv_xml_subject_ctx_activate(outer);
    TEST_ASSERT_NULL(prev);
    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "card", NULL, inner);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_PTR(outer, lv_xml_subject_ctx_get_active());
    lv_xml_subject_ctx_restore(prev);
    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_active());

    lv_obj_delete(v);
    lv_xml_subject_ctx_delete(outer);
    lv_xml_subject_ctx_delete(inner);
    lv_subject_deinit(&s.subject);
}

static void test_add_replaces_and_remove_only_touches_this_context(void)
{
    static str_subject_t a, b, p;
    str_init(&a, "a");
    str_init(&b, "b");
    str_init(&p, "p");
    lv_xml_subject_ctx_t * parent = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(parent);
    lv_xml_subject_ctx_add(parent, "v", &p.subject);
    lv_xml_subject_ctx_add(ctx, "v", &a.subject);
    lv_xml_subject_ctx_add(ctx, "v", &b.subject);
    TEST_ASSERT_EQUAL_PTR(&b.subject, lv_xml_subject_ctx_find(ctx, "v"));

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, lv_xml_subject_ctx_remove(ctx, "v"));
    TEST_ASSERT_EQUAL_PTR(&p.subject, lv_xml_subject_ctx_find(ctx, "v"));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_subject_ctx_remove(ctx, "v"));

    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_subject_ctx_add(NULL, "v", &a.subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_subject_ctx_add(ctx, NULL, &a.subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_subject_ctx_add(ctx, "v", NULL));

    lv_xml_subject_ctx_delete(ctx);
    lv_xml_subject_ctx_delete(parent);
    lv_subject_deinit(&a.subject);
    lv_subject_deinit(&b.subject);
    lv_subject_deinit(&p.subject);
}

static void test_user_data_belongs_to_one_context(void)
{
    int host;
    lv_xml_subject_ctx_t * parent = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(parent);
    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_user_data(ctx));

    lv_xml_subject_ctx_set_user_data(ctx, &host);
    TEST_ASSERT_EQUAL_PTR(&host, lv_xml_subject_ctx_get_user_data(ctx));
    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_user_data(parent));

    lv_xml_subject_ctx_set_user_data(ctx, NULL);
    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_user_data(ctx));
    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_user_data(NULL));
    lv_xml_subject_ctx_set_user_data(NULL, &host); /* ignored */

    lv_xml_subject_ctx_delete(ctx);
    lv_xml_subject_ctx_delete(parent);
}

static void test_subject_bound_repeat_rebuilds_against_its_creation_context(void)
{
    /* The count lives in the context, so the <repeat> is retained. Each
     * iteration binds `name`, which only exists in the context. The rebuild
     * runs from an observer long after lv_xml_create_with_ctx() returned. */
    ASSERT_XML_REGISTERS("rep",
                         "<component>"
                         "  <view extends='lv_obj'>"
                         "    <lv_obj name='list'>"
                         "      <repeat count='n'>"
                         "        <lv_label bind_text='name'/>"
                         "      </repeat>"
                         "    </lv_obj>"
                         "  </view>"
                         "</component>");

    static lv_subject_t n;
    lv_subject_init_int(&n, 2);
    static str_subject_t name;
    str_init(&name, "row");

    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_add(ctx, "n", &n);
    lv_xml_subject_ctx_add(ctx, "name", &name.subject);

    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "rep", NULL, ctx);
    TEST_ASSERT_NOT_NULL(v);
    lv_obj_t * list = ASSERT_NAMED(v, "list");
    ASSERT_CHILD_COUNT(list, 2);
    ASSERT_LABEL_TEXT(lv_obj_get_child(list, 0), "row");

    /* The creator lets go; the retained fragment keeps the context alive. */
    lv_xml_subject_ctx_delete(ctx);
    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_active());

    lv_subject_set_int(&n, 3);
    helix_test_pump(10);
    ASSERT_CHILD_COUNT(list, 3);
    for(uint32_t i = 0; i < 3; i++) {
        ASSERT_LABEL_TEXT(lv_obj_get_child(list, (int32_t)i), "row");
    }

    /* Rebuilt labels are bound, not just initialised from the value. */
    lv_subject_copy_string(&name.subject, "renamed");
    ASSERT_LABEL_TEXT(lv_obj_get_child(list, 2), "renamed");

    /* Rebuilding is not left with the context active. */
    TEST_ASSERT_NULL(lv_xml_subject_ctx_get_active());

    lv_obj_delete(v); /* releases the fragment's reference, freeing ctx */
    helix_test_pump(10);
    lv_subject_deinit(&n);
    lv_subject_deinit(&name.subject);
}

static void test_reactive_if_reads_context_subjects(void)
{
    ASSERT_XML_REGISTERS("cond",
                         "<component>"
                         "  <view extends='lv_obj'>"
                         "    <lv_obj name='holder'>"
                         "      <if cond='on gt 0'><lv_obj name='yes'/><else/><lv_obj name='no'/></if>"
                         "    </lv_obj>"
                         "  </view>"
                         "</component>");

    static lv_subject_t on;
    lv_subject_init_int(&on, 0);
    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_add(ctx, "on", &on);

    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "cond", NULL, ctx);
    TEST_ASSERT_NOT_NULL(v);
    helix_test_pump(10);
    ASSERT_NAMED(v, "no");
    ASSERT_NO_NAMED(v, "yes");

    lv_xml_subject_ctx_delete(ctx);
    lv_subject_set_int(&on, 1);
    helix_test_pump(10);
    ASSERT_NAMED(v, "yes");
    ASSERT_NO_NAMED(v, "no");

    lv_obj_delete(v);
    helix_test_pump(10);
    lv_subject_deinit(&on);
}

/*---------------------------------------------------------------------------
 * Resolvers
 *--------------------------------------------------------------------------*/

/* What the test resolvers were last asked, and what they answer */
static const lv_xml_subject_ctx_t * g_resolver_origin;
static char g_resolver_name[64];
static str_subject_t g_resolved;
static lv_subject_t g_resolved_int;

/* Answers "row.on|int" with g_resolved_int and other names starting with "row." with g_resolved, and notes the
 * question */
static lv_subject_t * row_resolver(const lv_xml_subject_ctx_t * origin, const char * name, void * user_data)
{
    LV_UNUSED(user_data);
    g_resolver_origin = origin;
    lv_strlcpy(g_resolver_name, name, sizeof(g_resolver_name));
    if(strcmp(name, "row.on|int") == 0) return &g_resolved_int;
    return strncmp(name, "row.", 4) == 0 ? &g_resolved.subject : NULL;
}

static void test_a_context_resolver_is_asked_after_its_subjects_and_before_its_parent(void)
{
    static str_subject_t own, parents;
    str_init(&g_resolved, "resolved");
    str_init(&own, "own");
    str_init(&parents, "parent's");

    lv_xml_subject_ctx_t * parent = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_add(parent, "row.parent", &parents.subject);
    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(parent);
    lv_xml_subject_ctx_add(ctx, "row.own", &own.subject);
    lv_xml_subject_ctx_set_resolver(ctx, row_resolver, NULL);

    /* Its own subject wins over the resolver */
    TEST_ASSERT_EQUAL_PTR(&own.subject, lv_xml_subject_ctx_find(ctx, "row.own"));
    /* The resolver wins over the parent */
    TEST_ASSERT_EQUAL_PTR(&g_resolved.subject, lv_xml_subject_ctx_find(ctx, "row.parent"));
    /* A name the resolver declines carries on to the parent */
    TEST_ASSERT_NULL(lv_xml_subject_ctx_find(ctx, "other"));
    TEST_ASSERT_EQUAL_STRING("other", g_resolver_name);

    /* Asked through a child, the resolver is told where the lookup started */
    lv_xml_subject_ctx_t * child = lv_xml_subject_ctx_create(ctx);
    TEST_ASSERT_EQUAL_PTR(&g_resolved.subject, lv_xml_subject_ctx_find(child, "row.x"));
    TEST_ASSERT_EQUAL_PTR(child, g_resolver_origin);

    /* Removing it leaves only the subjects */
    lv_xml_subject_ctx_set_resolver(ctx, NULL, NULL);
    TEST_ASSERT_EQUAL_PTR(&parents.subject, lv_xml_subject_ctx_find(ctx, "row.parent"));

    lv_xml_subject_ctx_delete(child);
    lv_xml_subject_ctx_delete(ctx);
    lv_xml_subject_ctx_delete(parent);
    lv_subject_deinit(&own.subject);
    lv_subject_deinit(&parents.subject);
    lv_subject_deinit(&g_resolved.subject);
}

static void test_a_widget_binds_to_a_resolved_subject(void)
{
    ASSERT_XML_REGISTERS("resolved_card",
                         "<component>"
                         "  <view extends='lv_obj'>"
                         "    <lv_label name='lbl' bind_text='row.items[{index}].name|upper'/>"
                         "  </view>"
                         "</component>");
    str_init(&g_resolved, "Hot end");

    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(NULL);
    lv_xml_subject_ctx_set_resolver(ctx, row_resolver, NULL);
    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "resolved_card", NULL, ctx);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("row.items[{index}].name|upper", g_resolver_name);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "lbl"), "Hot end");

    lv_subject_copy_string(&g_resolved.subject, "Bed");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "lbl"), "Bed");

    lv_obj_delete(v);
    lv_xml_subject_ctx_delete(ctx);
    lv_subject_deinit(&g_resolved.subject);
}

static void test_the_global_resolver_is_asked_after_the_globals(void)
{
    ASSERT_XML_REGISTERS("global_resolved",
                         "<component>"
                         "  <view extends='lv_obj'>"
                         "    <lv_label name='global_lbl' bind_text='row.global'/>"
                         "    <lv_label name='resolved_lbl' bind_text='row.other'/>"
                         "    <lv_obj name='cond_obj'>"
                         "      <bind_flag_if cond='row.on|int gt 0' flag='hidden'/>"
                         "    </lv_obj>"
                         "  </view>"
                         "</component>");
    static str_subject_t global;
    str_init(&global, "global");
    str_init(&g_resolved, "resolved");
    lv_subject_init_int(&g_resolved_int, 1);
    lv_xml_register_subject(NULL, "row.global", &global.subject);

    lv_xml_subject_ctx_t * ctx = lv_xml_subject_ctx_create(NULL);
    lv_xml_set_subject_resolver(row_resolver, NULL);

    lv_obj_t * v = lv_xml_create_with_ctx(lv_screen_active(), "global_resolved", NULL, ctx);
    TEST_ASSERT_NOT_NULL(v);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "global_lbl"), "global");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(v, "resolved_lbl"), "resolved");
    /* A cond= expression resolves a path-like name through it too */
    TEST_ASSERT_EQUAL_STRING("row.on|int", g_resolver_name);
    TEST_ASSERT_EQUAL_PTR(ctx, g_resolver_origin);
    TEST_ASSERT_TRUE(lv_obj_has_flag(ASSERT_NAMED(v, "cond_obj"), LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(&g_resolved_int, 0);
    TEST_ASSERT_FALSE(lv_obj_has_flag(ASSERT_NAMED(v, "cond_obj"), LV_OBJ_FLAG_HIDDEN));

    lv_xml_set_subject_resolver(NULL, NULL);
    lv_obj_delete(v);
    lv_xml_subject_ctx_delete(ctx);
    lv_xml_unregister_subject(NULL, "row.global");
    lv_subject_deinit(&global.subject);
    lv_subject_deinit(&g_resolved.subject);
    lv_subject_deinit(&g_resolved_int);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_two_instances_bind_to_their_own_subjects);
    RUN_TEST(test_component_scope_wins_over_context_and_context_over_globals);
    RUN_TEST(test_without_a_context_lookup_is_unchanged);
    RUN_TEST(test_parent_context_is_searched_after_the_child);
    RUN_TEST(test_nested_components_see_the_instance_context);
    RUN_TEST(test_active_context_is_restored_after_creation);
    RUN_TEST(test_add_replaces_and_remove_only_touches_this_context);
    RUN_TEST(test_user_data_belongs_to_one_context);
    RUN_TEST(test_subject_bound_repeat_rebuilds_against_its_creation_context);
    RUN_TEST(test_reactive_if_reads_context_subjects);
    RUN_TEST(test_a_context_resolver_is_asked_after_its_subjects_and_before_its_parent);
    RUN_TEST(test_a_widget_binds_to_a_resolved_subject);
    RUN_TEST(test_the_global_resolver_is_asked_after_the_globals);
    return UNITY_END();
}
