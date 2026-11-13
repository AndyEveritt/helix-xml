/**
 * @file test_widget_descriptions.c
 *
 * xmls/ describes each widget the engine registers: the attributes it takes,
 * their types and help, and its child elements. Tools read it (an editor's
 * completion, say), and it is written by hand, so this pins it to the engine:
 *
 *  - every attribute described is one the engine recognises: building an
 *    element with it raises no "Unknown attribute" warning. This holds only
 *    where the engine runs that check - on the widgets it registers itself, and
 *    on child elements whose apply chain takes part (see lv_xml_attr_check.h)
 *  - every widget and element the engine registers is described
 *  - every style property globals.xml describes (<styledef>) is one a <style>
 *    definition accepts
 *
 * What it cannot catch is the reverse of the first: an attribute the engine
 * accepts that no description mentions. The apply chains hold no list of the
 * names they match, so there is nothing to compare against - add the
 * description along with the attribute.
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"
#include "libs/expat/expat.h"
#include "xml/lv_xml_widget.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HELIX_XML_DESCRIPTION_DIR
#error "HELIX_XML_DESCRIPTION_DIR must name the xmls/ directory"
#endif

/*---------------------------------------------------------------------------
 * Unity fixture
 *--------------------------------------------------------------------------*/

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

/*---------------------------------------------------------------------------
 * The descriptions, as read from xmls/
 *--------------------------------------------------------------------------*/

#define NAME_MAX_LEN 96
#define ELEMENT_MAX_LEN (2 * NAME_MAX_LEN) /* "<widget>-<child>" */
#define MAX_ATTRS    1024
#define MAX_ELEMENTS 128
#define MAX_ENUMS    128
#define REPORT_LEN   2048 /* A test's list of what is wrong */

/** One described attribute */
typedef struct {
    char element[ELEMENT_MAX_LEN]; /**< as registered: "lv_label", "lv_obj-bind_flag_if_eq" */
    char attr[ELEMENT_MAX_LEN];    /**< as written, e.g. "value-animated" */
    char type[NAME_MAX_LEN];       /**< its described type, "" if none */
    bool is_arg;                   /**< an element's <arg>: required, so every build of the element has it */
} described_attr_t;

/** An enum type and a value it accepts, so an attribute of that type gets a valid value */
typedef struct {
    char name[NAME_MAX_LEN];  /**< e.g. "lv_align" */
    char first[NAME_MAX_LEN]; /**< its first value, e.g. "default" */
} described_enum_t;

static described_attr_t g_attrs[MAX_ATTRS];         /**< every described attribute */
static size_t g_attr_count;                          /**< entries used in g_attrs */
static char g_elements[MAX_ELEMENTS][ELEMENT_MAX_LEN]; /**< every described widget and element */
static size_t g_element_count;                       /**< entries used in g_elements */
static described_enum_t g_enums[MAX_ENUMS];          /**< every described enum type */
static size_t g_enum_count;                          /**< entries used in g_enums */
static described_attr_t g_style_props[MAX_ATTRS];   /**< every <styledef> property, as element "style" */
static size_t g_style_prop_count;                   /**< entries used in g_style_props */

/** Where the parser is in one description file */
typedef struct {
    char widget[NAME_MAX_LEN];     /**< from the file name, e.g. "lv_label" */
    char element[ELEMENT_MAX_LEN]; /**< the widget, or "<widget>-<child>" inside <element> */
    char prop[NAME_MAX_LEN];    /**< the <prop> being read, for its <param>s */
    int param_index;            /**< <param>s seen in that <prop> */
    char enumdef[NAME_MAX_LEN]; /**< the <enumdef> being read, "" outside one */
    bool enum_seen;             /**< its first <enum> has been recorded */
    bool is_widget;             /**< it describes a widget; globals.xml only holds shared types */
    bool in_styledef;           /**< inside globals.xml's <styledef>: the properties of a <style> */
} reader_t;

/** The value of attribute @p name in expat's @p attrs, or NULL */
static const char * attr_of(const XML_Char ** attrs, const char * name)
{
    for(int i = 0; attrs[i]; i += 2) {
        if(strcmp(attrs[i], name) == 0) return attrs[i + 1];
    }
    return NULL;
}

/** Record that @p element is described */
static void add_element(const char * element)
{
    TEST_ASSERT_LESS_THAN_size_t_MESSAGE(MAX_ELEMENTS, g_element_count, "raise MAX_ELEMENTS");
    snprintf(g_elements[g_element_count++], ELEMENT_MAX_LEN, "%s", element);
}

/** Record attribute @p attr of @p element, of @p type (may be NULL); @p is_arg for an element's <arg> */
static void add_attr(const reader_t * r, const char * element, const char * attr, const char * type, bool is_arg)
{
    if(!r->is_widget) return;
    TEST_ASSERT_LESS_THAN_size_t_MESSAGE(MAX_ATTRS, g_attr_count, "raise MAX_ATTRS");
    described_attr_t * a = &g_attrs[g_attr_count++];
    snprintf(a->element, sizeof(a->element), "%s", element);
    snprintf(a->attr, sizeof(a->attr), "%s", attr);
    snprintf(a->type, sizeof(a->type), "%s", type ? type : "");
    a->is_arg = is_arg;

    /* `style_bg_color-selector` is `style_bg_color`, optionally suffixed with a selector */
    char * suffix = strstr(a->attr, "-selector");
    if(suffix && suffix[strlen("-selector")] == '\0') *suffix = '\0';
}

/** expat start-element handler: record what a description declares */
static void XMLCALL on_start(void * user_data, const XML_Char * tag, const XML_Char ** attrs)
{
    reader_t * r = user_data;
    const char * name = attr_of(attrs, "name");

    if(strcmp(tag, "element") == 0 && name) {
        snprintf(r->element, sizeof(r->element), "%s-%s", r->widget, name);
        add_element(r->element);
    }
    else if(strcmp(tag, "prop") == 0 && name && !r->in_styledef) {
        /* A <prop> made of <param>s takes the first param's type; the others are `<prop>-<param>` */
        snprintf(r->prop, sizeof(r->prop), "%s", name);
        r->param_index = 0;
        add_attr(r, r->element, name, attr_of(attrs, "type"), false);
    }
    else if(strcmp(tag, "param") == 0 && name) {
        if(r->param_index++ > 0) {
            char attr[ELEMENT_MAX_LEN];
            snprintf(attr, sizeof(attr), "%s-%s", r->prop, name);
            add_attr(r, r->element, attr, attr_of(attrs, "type"), false);
        }
    }
    else if(strcmp(tag, "arg") == 0 && name) {
        add_attr(r, r->element, name, attr_of(attrs, "type"), true);
    }
    else if(strcmp(tag, "styledef") == 0) {
        r->in_styledef = true;
    }
    else if(strcmp(tag, "prop") == 0 && name && r->in_styledef) {
        TEST_ASSERT_LESS_THAN_size_t_MESSAGE(MAX_ATTRS, g_style_prop_count, "raise MAX_ATTRS");
        described_attr_t * p = &g_style_props[g_style_prop_count++];
        snprintf(p->element, sizeof(p->element), "style");
        snprintf(p->attr, sizeof(p->attr), "%s", name);
        snprintf(p->type, sizeof(p->type), "%s", attr_of(attrs, "type") ? attr_of(attrs, "type") : "");
        p->is_arg = false;
    }
    else if(strcmp(tag, "enumdef") == 0 && name) {
        snprintf(r->enumdef, sizeof(r->enumdef), "%s", name);
        r->enum_seen = false;
    }
    else if(strcmp(tag, "enum") == 0 && name && r->enumdef[0] && !r->enum_seen) {
        TEST_ASSERT_LESS_THAN_size_t_MESSAGE(MAX_ENUMS, g_enum_count, "raise MAX_ENUMS");
        snprintf(g_enums[g_enum_count].name, NAME_MAX_LEN, "%s", r->enumdef);
        snprintf(g_enums[g_enum_count].first, NAME_MAX_LEN, "%s", name);
        g_enum_count++;
        r->enum_seen = true;
    }
}

/** expat end-element handler: leave an <element> or <enumdef> */
static void XMLCALL on_end(void * user_data, const XML_Char * tag)
{
    reader_t * r = user_data;
    if(strcmp(tag, "element") == 0) snprintf(r->element, sizeof(r->element), "%s", r->widget);
    else if(strcmp(tag, "enumdef") == 0) r->enumdef[0] = '\0';
    else if(strcmp(tag, "styledef") == 0) r->in_styledef = false;
}

/** Read description @p file_name, of the widget it is named after ("globals" describes no widget) */
static void read_description(const char * file_name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", HELIX_XML_DESCRIPTION_DIR, file_name);
    FILE * f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char * text = malloc((size_t)size);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_size_t(size, fread(text, 1, (size_t)size, f));
    fclose(f);

    reader_t r = {0};
    snprintf(r.widget, sizeof(r.widget), "%.*s", (int)(strlen(file_name) - strlen(".xml")), file_name);
    snprintf(r.element, sizeof(r.element), "%s", r.widget);
    r.is_widget = strcmp(r.widget, "globals") != 0;
    if(r.is_widget) add_element(r.widget);

    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, &r);
    XML_SetElementHandler(parser, on_start, on_end);
    const bool parsed = XML_Parse(parser, text, (int)size, 1) == XML_STATUS_OK;
    XML_ParserFree(parser);
    free(text);
    TEST_ASSERT_TRUE_MESSAGE(parsed, helix_xml_assert_msgf("%s is not well-formed XML", path));
}

/** Read every description in xmls/ */
static void read_descriptions(void)
{
    g_attr_count = g_element_count = g_enum_count = g_style_prop_count = 0;
    DIR * dir = opendir(HELIX_XML_DESCRIPTION_DIR);
    TEST_ASSERT_NOT_NULL_MESSAGE(dir, HELIX_XML_DESCRIPTION_DIR);
    for(struct dirent * entry = readdir(dir); entry; entry = readdir(dir)) {
        const size_t len = strlen(entry->d_name);
        if(len > 4 && strcmp(entry->d_name + len - 4, ".xml") == 0) read_description(entry->d_name);
    }
    closedir(dir);
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, g_element_count, "no descriptions in xmls/");
}

/** Whether @p element is described */
static bool is_described(const char * element)
{
    for(size_t i = 0; i < g_element_count; i++) {
        if(strcmp(g_elements[i], element) == 0) return true;
    }
    return false;
}

/** A value of described type @p type, for building an element with the attribute */
static const char * sample_value(const char * type)
{
    if(strncmp(type, "enum:", 5) == 0) {
        /* `enum:lv_chart_axis+` takes several ORed; one will do */
        char name[NAME_MAX_LEN];
        snprintf(name, sizeof(name), "%s", type + 5);
        name[strcspn(name, "+[")] = '\0';
        for(size_t i = 0; i < g_enum_count; i++) {
            if(strcmp(g_enums[i].name, name) == 0) return g_enums[i].first;
        }
        return "";
    }
    if(strcmp(type, "bool") == 0 || strncmp(type, "flag:", 5) == 0) return "false";
    if(strcmp(type, "int") == 0 || strcmp(type, "coords") == 0 || strcmp(type, "opa") == 0 ||
       strcmp(type, "deg") == 0 || strcmp(type, "radius") == 0) {
        return "10"; /* Not 0: some sizes and counts can't be 0, e.g. a QR code's */
    }
    if(strcmp(type, "color") == 0) return "0x000000";
    if(strcmp(type, "style") == 0) return "s"; /* Every test component defines it */
    return "";
}

/** Each test component's start: style `s`, for attributes that name a style, and an open view */
#define COMPONENT_HEAD "<component><styles><style name=\"s\"/></styles><view extends=\"lv_obj\">"

/*---------------------------------------------------------------------------
 * Tests
 *--------------------------------------------------------------------------*/

/** Every attribute described is one the engine recognises */
static void test_every_described_attribute_is_recognised(void)
{
    read_descriptions();

    static char failures[REPORT_LEN];
    failures[0] = '\0';
    for(size_t i = 0; i < g_attr_count; i++) {
        const described_attr_t * a = &g_attrs[i];

        /* The attribute, and the element's required <arg>s, which some elements can't be built without */
        char attrs[1024];
        snprintf(attrs, sizeof(attrs), "%s=\"%s\"", a->attr, sample_value(a->type));
        for(size_t j = 0; j < g_attr_count; j++) {
            const described_attr_t * arg = &g_attrs[j];
            if(j != i && arg->is_arg && strcmp(arg->element, a->element) == 0) {
                const size_t used = strlen(attrs);
                snprintf(attrs + used, sizeof(attrs) - used, " %s=\"%s\"", arg->attr, sample_value(arg->type));
            }
        }

        /* `lv_obj-x` is written <x> inside any widget; `lv_chart-series` is written whole, inside <lv_chart> */
        char xml[2048];
        const char * dash = strchr(a->element, '-');
        if(dash == NULL) {
            snprintf(xml, sizeof(xml), COMPONENT_HEAD "<%s %s/></view></component>",
                     a->element, attrs);
        }
        else {
            const int parent_len = (int)(dash - a->element);
            const char * child = strncmp(a->element, "lv_obj-", 7) == 0 ? dash + 1 : a->element;
            snprintf(xml, sizeof(xml), COMPONENT_HEAD "<%.*s><%s %s/></%.*s></view></component>",
                     parent_len, a->element, child, attrs, parent_len, a->element);
        }

        char component[32];
        snprintf(component, sizeof(component), "desc_%u", (unsigned)i);
        ASSERT_XML_REGISTERS(component, xml);
        log_capture_start();
        lv_obj_t * built = XML_CREATE(helix_test_env_screen(), component, NULL);
        log_capture_stop();
        lv_obj_delete(built);
        lv_xml_component_unregister(component);

        if(log_contains("Unknown attribute")) {
            const size_t used = strlen(failures);
            snprintf(failures + used, sizeof(failures) - used, "\n  %s %s", a->element, a->attr);
        }
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", failures,
                                     "described attributes the engine reports as unknown: fix xmls/ or the parser");
}

/** lv_xml_widget_foreach() callback: append @p processor to the list @p user_data if it is undescribed */
static void note_if_undescribed(const lv_widget_processor_t * processor, void * user_data)
{
    char * missing = user_data;
    if(processor->builtin && !is_described(processor->name)) {
        const size_t used = strlen(missing);
        snprintf(missing + used, REPORT_LEN - used, "\n  %s", processor->name);
    }
}

/** Every widget and element the engine registers is described */
static void test_every_registered_widget_is_described(void)
{
    read_descriptions();

    static char missing[REPORT_LEN];
    missing[0] = '\0';
    lv_xml_widget_foreach(note_if_undescribed, missing);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", missing, "registered widgets with no description in xmls/");
}

/** Every style property described is one a <style> definition accepts */
static void test_every_described_style_property_is_accepted(void)
{
    read_descriptions();
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, g_style_prop_count, "globals.xml describes no style properties");

    static char failures[REPORT_LEN];
    failures[0] = '\0';
    for(size_t i = 0; i < g_style_prop_count; i++) {
        const described_attr_t * p = &g_style_props[i];
        /* A transition's other parts are optional, but it needs to know what animates */
        const char * also = strncmp(p->attr, "transition_", 11) == 0 && strcmp(p->attr, "transition_props") != 0
                            ? " transition_props=\"bg_color\"" : "";
        const char * value = strcmp(p->attr, "transition") == 0 ? "bg_color 100" : sample_value(p->type);

        char xml[1024];
        snprintf(xml, sizeof(xml),
                 "<component><styles><style name=\"t\" %s=\"%s\"%s/></styles><view extends=\"lv_obj\"/></component>",
                 p->attr, value, also);
        char component[32];
        snprintf(component, sizeof(component), "style_%u", (unsigned)i);
        log_capture_start();
        ASSERT_XML_REGISTERS(component, xml);
        log_capture_stop();
        lv_xml_component_unregister(component);

        if(log_contains("style property is not supported")) {
            const size_t used = strlen(failures);
            snprintf(failures + used, sizeof(failures) - used, "\n  %s", p->attr);
        }
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", failures,
                                     "described style properties a <style> doesn't accept: fix globals.xml or the parser");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_described_attribute_is_recognised);
    RUN_TEST(test_every_registered_widget_is_described);
    RUN_TEST(test_every_described_style_property_is_accepted);
    return UNITY_END();
}
