#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <zlib.h>
#include <libwapcaplet/libwapcaplet.h>
#include <dom/core/string.h>

/* ===== Library-level forward type declarations ===== */
/* These types are NOT in libc headers; they are from libcss/libdom/etc */
typedef intptr_t css_fixed;
typedef uint32_t css_color;
typedef uint8_t css_unit;
typedef intptr_t css_error;
typedef intptr_t css_origin;
typedef intptr_t css_media;
typedef intptr_t css_select_ctx;
typedef intptr_t css_select_results;
typedef intptr_t dom_keyboard_event;
typedef intptr_t dom_event;
typedef intptr_t dom_html_text_area_element;
typedef intptr_t dom_html_select_element;
typedef intptr_t dom_html_script_element;
typedef intptr_t dom_html_options_collection;
typedef intptr_t dom_html_option_element;
typedef intptr_t dom_html_olist_element;
typedef intptr_t dom_html_li_element;
typedef intptr_t dom_html_input_element;
typedef intptr_t dom_html_button_element;
typedef intptr_t dom_html_form_element;
typedef intptr_t dom_html_collection;
typedef intptr_t css_computed_style;
typedef intptr_t css_stylesheet;
typedef intptr_t css_stylesheet_params;
typedef intptr_t lwc_node;
typedef intptr_t dom_element;
typedef intptr_t dom_node;
typedef intptr_t dom_hubbub_error;
typedef intptr_t dom_hubbub_parser;
typedef intptr_t dom_hubbub_parser_params;
typedef intptr_t dom_document;
typedef intptr_t dom_namednodemap;
typedef intptr_t dom_nodelist;
typedef intptr_t parserutils_error;
typedef intptr_t css_font_family;
typedef intptr_t css_computed_content_item;
typedef intptr_t css_computed_counter;
struct css_computed_clip_rect { intptr_t dummy; };


/* ===== Constants ===== */
#define DOM_NO_ERR 0
#define DOM_NOT_SUPPORTED_ERR 1
#define DOM_NOT_FOUND_ERR 2
#define DOM_HUBBUB_OK 0
#define DOM_HUBBUB_NOMEM 1
#define lwc_error_ok 0
#define lwc_error_oom 1
#define PARSERUTILS_OK 0
#define CSS_OK 0
#define Z_OK 0
#define Z_STREAM_END 1

/* ===== css_computed_* stubs ===== */
uint8_t css_computed_align_items(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_align_self(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_background_attachment(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_background_color(const css_computed_style *style, css_color *color) { (void)style; *color = 0; return 0; }
uint8_t css_computed_background_image(const css_computed_style *style, lwc_string **image) { (void)style; *image = NULL; return 0; }
uint8_t css_computed_background_position(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_background_repeat(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_border_bottom_color(const css_computed_style *style, css_color *color) { (void)style; *color = 0; return 0; }
uint8_t css_computed_border_bottom_style(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_bottom_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_collapse(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_border_left_color(const css_computed_style *style, css_color *color) { (void)style; *color = 0; return 0; }
uint8_t css_computed_border_left_style(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_left_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_right_color(const css_computed_style *style, css_color *color) { (void)style; *color = 0; return 0; }
uint8_t css_computed_border_right_style(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_right_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_spacing(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_top_color(const css_computed_style *style, css_color *color) { (void)style; *color = 0; return 0; }
uint8_t css_computed_border_top_style(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_border_top_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_bottom(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_box_sizing(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_caption_side(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_clear(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_clip(const css_computed_style *style, struct css_computed_clip_rect *rect) { (void)style; (void)rect; return 0; }
uint8_t css_computed_color(const css_computed_style *style, css_color *color) { (void)style; *color = 0; return 0; }
uint8_t css_computed_content(const css_computed_style *style, const css_computed_content_item **content) { (void)style; *content = NULL; return 0; }
uint8_t css_computed_counter_increment(const css_computed_style *style, const css_computed_counter **counters, int32_t *count) { (void)style; *counters = NULL; *count = 0; return 0; }
uint8_t css_computed_counter_reset(const css_computed_style *style, const css_computed_counter **counters, int32_t *count) { (void)style; *counters = NULL; *count = 0; return 0; }
uint8_t css_computed_cursor(const css_computed_style *style, lwc_string **image) { (void)style; *image = NULL; return 0; }
uint8_t css_computed_direction(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_display(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_display_static(const css_computed_style *style, bool *static_pos) { (void)style; *static_pos = false; return 0; }
uint8_t css_computed_empty_cells(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_flex_basis(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_flex_direction(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_flex_grow(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_flex_shrink(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_flex_wrap(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_float(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_font_family(const css_computed_style *style, const css_font_family **family) { (void)style; *family = NULL; return 0; }
uint8_t css_computed_font_size(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_font_style(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_font_variant(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_font_weight(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_format_list_style(const css_computed_style *style, char *buf, size_t len) { (void)style; (void)buf; (void)len; return 0; }
uint8_t css_computed_height(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_left(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_letter_spacing(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_line_height(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_list_style_image(const css_computed_style *style, lwc_string **image) { (void)style; *image = NULL; return 0; }
uint8_t css_computed_list_style_position(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_list_style_type(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_margin_bottom(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_margin_left(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_margin_right(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_margin_top(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_max_height(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_max_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_min_height(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_min_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_opacity(const css_computed_style *style, css_fixed *opacity) { (void)style; *opacity = (css_fixed)0; return 0; }
uint8_t css_computed_outline_color(const css_computed_style *style, css_color *color) { (void)style; *color = 0; return 0; }
uint8_t css_computed_outline_style(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_outline_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_overflow_x(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_overflow_y(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_padding_bottom(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_padding_left(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_padding_right(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_padding_top(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_position(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_quotes(const css_computed_style *style, lwc_string **open_quote, lwc_string **close_quote, int32_t *count) { (void)style; *open_quote = NULL; *close_quote = NULL; *count = 0; return 0; }
uint8_t css_computed_right(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
css_error css_computed_style_compose(css_computed_style *style, const css_computed_style *parent, css_computed_style **combined) { (void)style; (void)parent; (void)combined; return CSS_OK; }
css_error css_computed_style_destroy(css_computed_style *style) { (void)style; return CSS_OK; }
uint8_t css_computed_table_layout(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_text_align(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_text_decoration(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_text_indent(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_text_transform(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_top(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_unicode_bidi(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_vertical_align(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_visibility(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_white_space(const css_computed_style *style) { (void)style; return 0; }
uint8_t css_computed_width(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_width_px(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_word_spacing(const css_computed_style *style, css_fixed *length, css_unit *unit) { (void)style; *length = (css_fixed)0; *unit = 0; return 0; }
uint8_t css_computed_z_index(const css_computed_style *style, int32_t *val) { (void)style; *val = 0; return 0; }

/* ===== Other CSS stubs ===== */
css_error css_libcss_node_data_handler(void *node, void *pw) { (void)node; (void)pw; return CSS_OK; }
css_error css_select_ctx_append_sheet(css_select_ctx *ctx, css_stylesheet *sheet, css_origin origin, css_media ***media, uint64_t *next) { (void)ctx; (void)sheet; (void)origin; (void)media; (void)next; return CSS_OK; }
css_error css_select_ctx_create(css_select_ctx **ctx) { (void)ctx; return CSS_OK; }
css_error css_select_ctx_destroy(css_select_ctx *ctx) { (void)ctx; return CSS_OK; }
css_error css_select_default_style(css_select_ctx *ctx, css_select_results **results) { (void)ctx; (void)results; return CSS_OK; }
css_error css_select_results_destroy(css_select_results *results) { (void)results; return CSS_OK; }
css_error css_select_style(css_select_ctx *ctx, void *node, css_media *media, css_select_results **results) { (void)ctx; (void)node; (void)media; (void)results; return CSS_OK; }
css_error css_stylesheet_append_data(css_stylesheet *stylesheet, const uint8_t *data, size_t len) { (void)stylesheet; (void)data; (void)len; return CSS_OK; }
css_error css_stylesheet_create(const css_stylesheet_params *params, css_stylesheet **stylesheet) { (void)params; (void)stylesheet; return CSS_OK; }
css_error css_stylesheet_data_done(css_stylesheet *stylesheet) { (void)stylesheet; return CSS_OK; }
css_error css_stylesheet_destroy(css_stylesheet *stylesheet) { (void)stylesheet; return CSS_OK; }
css_error css_stylesheet_get_url(css_stylesheet *sheet, lwc_string **url) { (void)sheet; *url = NULL; return CSS_OK; }
bool css_stylesheet_quirks_allowed(css_stylesheet *sheet) { (void)sheet; return false; }
css_error css_stylesheet_register_import(css_stylesheet *sheet, lwc_string *url, lwc_string *media) { (void)sheet; (void)url; (void)media; return CSS_OK; }
size_t css_stylesheet_size(css_stylesheet *sheet) { (void)sheet; return 0; }
css_fixed css_unit_font_size_len2pt(css_fixed length, css_unit unit, css_fixed dpi) { (void)length; (void)unit; (void)dpi; return (css_fixed)0; }
css_fixed css_unit_len2device_px(css_fixed length, css_unit unit, unsigned long hward) { (void)length; (void)unit; (void)hward; return (css_fixed)0; }

/* ===== DOM stubs ===== */
dom_element *dom_element_named_ancestor_node(dom_element *elem) { (void)elem; return NULL; }
dom_element *dom_element_named_parent_node(dom_element *elem) { (void)elem; return NULL; }
dom_element *dom_element_parent_node(dom_element *elem) { (void)elem; return NULL; }
dom_exception dom_html_button_element_get_disabled(dom_html_button_element *elem, bool *disabled) { (void)elem; *disabled = false; return DOM_NO_ERR; }
dom_exception dom_html_button_element_get_form(dom_html_button_element *elem, dom_html_form_element **form) { (void)elem; *form = NULL; return DOM_NO_ERR; }
dom_exception dom_html_button_element_get_name(dom_html_button_element *elem, dom_string **name) { (void)elem; *name = NULL; return DOM_NO_ERR; }
dom_exception dom_html_button_element_get_type(dom_html_button_element *elem, dom_string **type) { (void)elem; *type = NULL; return DOM_NO_ERR; }
dom_exception dom_html_button_element_get_value(dom_html_button_element *elem, dom_string **value) { (void)elem; *value = NULL; return DOM_NO_ERR; }
dom_exception dom_html_collection_get_length(dom_html_collection *coll, uint32_t *len) { (void)coll; *len = 0; return DOM_NO_ERR; }
dom_exception dom_html_collection_item(dom_html_collection *coll, uint32_t index, dom_node **node) { (void)coll; (void)index; *node = NULL; return DOM_NOT_FOUND_ERR; }
dom_exception dom_html_collection_unref(dom_html_collection *coll) { (void)coll; return DOM_NO_ERR; }
dom_exception dom_html_form_element_get_accept_charset(dom_html_form_element *elem, dom_string **val) { (void)elem; *val = NULL; return DOM_NO_ERR; }
dom_exception dom_html_form_element_get_action(dom_html_form_element *elem, dom_string **val) { (void)elem; *val = NULL; return DOM_NO_ERR; }
dom_exception dom_html_form_element_get_elements(dom_html_form_element *elem, dom_html_collection **coll) { (void)elem; *coll = NULL; return DOM_NO_ERR; }
dom_exception dom_html_form_element_get_enctype(dom_html_form_element *elem, dom_string **val) { (void)elem; *val = NULL; return DOM_NO_ERR; }
dom_exception dom_html_form_element_get_method(dom_html_form_element *elem, dom_string **val) { (void)elem; *val = NULL; return DOM_NO_ERR; }
dom_exception dom_html_form_element_get_target(dom_html_form_element *elem, dom_string **val) { (void)elem; *val = NULL; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_checked(dom_html_input_element *elem, bool *checked) { (void)elem; *checked = false; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_disabled(dom_html_input_element *elem, bool *disabled) { (void)elem; *disabled = false; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_form(dom_html_input_element *elem, dom_html_form_element **form) { (void)elem; *form = NULL; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_max_length(dom_html_input_element *elem, int32_t *len) { (void)elem; *len = 0; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_name(dom_html_input_element *elem, dom_string **name) { (void)elem; *name = NULL; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_read_only(dom_html_input_element *elem, bool *readonly) { (void)elem; *readonly = false; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_type(dom_html_input_element *elem, dom_string **type) { (void)elem; *type = NULL; return DOM_NO_ERR; }
dom_exception dom_html_input_element_get_value(dom_html_input_element *elem, dom_string **value) { (void)elem; *value = NULL; return DOM_NO_ERR; }
dom_exception dom_html_input_element_set_checked(dom_html_input_element *elem, bool checked) { (void)elem; (void)checked; return DOM_NO_ERR; }
dom_exception dom_html_input_element_set_value(dom_html_input_element *elem, dom_string *value) { (void)elem; (void)value; return DOM_NO_ERR; }
dom_exception dom_html_li_element_get_value(dom_html_li_element *elem, int32_t *val) { (void)elem; *val = 0; return DOM_NO_ERR; }
dom_exception dom_html_olist_element_get_start(dom_html_olist_element *elem, int32_t *val) { (void)elem; *val = 0; return DOM_NO_ERR; }
dom_exception dom_html_option_element_get_selected(dom_html_option_element *elem, bool *sel) { (void)elem; *sel = false; return DOM_NO_ERR; }
dom_exception dom_html_option_element_get_value(dom_html_option_element *elem, dom_string **value) { (void)elem; *value = NULL; return DOM_NO_ERR; }
dom_exception dom_html_option_element_set_selected(dom_html_option_element *elem, bool sel) { (void)elem; (void)sel; return DOM_NO_ERR; }
dom_exception dom_html_options_collection_get_length(dom_html_options_collection *coll, uint32_t *len) { (void)coll; *len = 0; return DOM_NO_ERR; }
dom_exception dom_html_options_collection_item(dom_html_options_collection *coll, uint32_t index, dom_node **node) { (void)coll; (void)index; *node = NULL; return DOM_NOT_FOUND_ERR; }
dom_exception dom_html_options_collection_unref(dom_html_options_collection *coll) { (void)coll; return DOM_NO_ERR; }
dom_exception dom_html_script_element_get_flags(dom_html_script_element *elem, uint32_t *flags) { (void)elem; *flags = 0; return DOM_NO_ERR; }
dom_exception dom_html_select_element_get_disabled(dom_html_select_element *elem, bool *disabled) { (void)elem; *disabled = false; return DOM_NO_ERR; }
dom_exception dom_html_select_element_get_form(dom_html_select_element *elem, dom_html_form_element **form) { (void)elem; *form = NULL; return DOM_NO_ERR; }
dom_exception dom_html_select_element_get_multiple(dom_html_select_element *elem, bool *multiple) { (void)elem; *multiple = false; return DOM_NO_ERR; }
dom_exception dom_html_select_element_get_name(dom_html_select_element *elem, dom_string **name) { (void)elem; *name = NULL; return DOM_NO_ERR; }
dom_exception dom_html_text_area_element_get_disabled(dom_html_text_area_element *elem, bool *disabled) { (void)elem; *disabled = false; return DOM_NO_ERR; }
dom_exception dom_html_text_area_element_get_form(dom_html_text_area_element *elem, dom_html_form_element **form) { (void)elem; *form = NULL; return DOM_NO_ERR; }
dom_exception dom_html_text_area_element_get_name(dom_html_text_area_element *elem, dom_string **name) { (void)elem; *name = NULL; return DOM_NO_ERR; }
dom_exception dom_html_text_area_element_get_read_only(dom_html_text_area_element *elem, bool *readonly) { (void)elem; *readonly = false; return DOM_NO_ERR; }
dom_exception dom_html_text_area_element_get_value(dom_html_text_area_element *elem, dom_string **value) { (void)elem; *value = NULL; return DOM_NO_ERR; }
dom_exception dom_html_text_area_element_set_value(dom_html_text_area_element *elem, dom_string *value) { (void)elem; (void)value; return DOM_NO_ERR; }
dom_hubbub_error dom_hubbub_parser_completed(dom_hubbub_parser *parser) { (void)parser; return DOM_HUBBUB_OK; }
dom_hubbub_error dom_hubbub_parser_create(const dom_hubbub_parser_params *params, dom_hubbub_parser **parser, dom_document **doc) { (void)params; *parser = NULL; *doc = NULL; return DOM_HUBBUB_NOMEM; }
dom_hubbub_error dom_hubbub_parser_destroy(dom_hubbub_parser *parser) { (void)parser; return DOM_HUBBUB_OK; }
const char *dom_hubbub_parser_get_encoding(dom_hubbub_parser *parser, uint64_t *param) { (void)parser; (void)param; return NULL; }
dom_hubbub_error dom_hubbub_parser_parse_chunk(dom_hubbub_parser *parser, const uint8_t *data, size_t len) { (void)parser; (void)data; (void)len; return DOM_HUBBUB_OK; }
dom_hubbub_error dom_hubbub_parser_pause(dom_hubbub_parser *parser, bool pause) { (void)parser; (void)pause; return DOM_HUBBUB_OK; }
dom_exception dom_namednodemap_get_length(dom_namednodemap *map, uint32_t *len) { (void)map; *len = 0; return DOM_NO_ERR; }
dom_exception dom_namednodemap_unref(dom_namednodemap *map) { (void)map; return DOM_NO_ERR; }
dom_exception dom_namespace_finalise(void) { return DOM_NO_ERR; }
dom_exception dom_nodelist_get_length(dom_nodelist *list, uint32_t *len) { (void)list; *len = 0; return DOM_NO_ERR; }
dom_exception dom_nodelist_unref(dom_nodelist *list) { (void)list; return DOM_NO_ERR; }
size_t dom_string_byte_length(const dom_string *str) { (void)str; return 0; }
bool dom_string_caseless_isequal(const dom_string *a, const dom_string *b) { (void)a; (void)b; return false; }
bool dom_string_caseless_lwc_isequal(const dom_string *a, lwc_string *b) { (void)a; (void)b; return false; }
dom_exception dom_string_create(const uint8_t *ptr, size_t len, dom_string **str) { (void)ptr; (void)len; *str = NULL; return DOM_NOT_SUPPORTED_ERR; }
static dom_string ntux_dom_dummy = { 9999 };
dom_exception dom_string_create_interned(const uint8_t *ptr, size_t len, dom_string **str) { (void)ptr; (void)len; *str = &ntux_dom_dummy; return DOM_NO_ERR; }
const char *dom_string_data(const dom_string *str) { (void)str; return NULL; }
void dom_string_destroy(dom_string *str) { (void)str; }
dom_exception dom_string_intern(dom_string *str, struct lwc_string_s **lwcstr) { (void)str; *lwcstr = NULL; return DOM_NOT_SUPPORTED_ERR; }
bool dom_string_isequal(const dom_string *a, const dom_string *b) { (void)a; (void)b; return false; }
uint32_t dom_string_length(dom_string *str) { (void)str; return 0; }
bool dom_string_lwc_isequal(const dom_string *a, lwc_string *b) { (void)a; (void)b; return false; }

/* ===== libwapcaplet stubs ===== */
#include <string.h>

static lwc_string ntux_lwc_http_str = { .len = 4, .hash = 0x68747470, .refcnt = 0, .insensitive = NULL };
static lwc_string ntux_lwc_https_str = { .len = 5, .hash = 0x68747470, .refcnt = 0, .insensitive = NULL };
static lwc_string ntux_lwc_about_str = { .len = 5, .hash = 0x61626f75, .refcnt = 0, .insensitive = NULL };
static lwc_string ntux_lwc_file_str = { .len = 4, .hash = 0x66696c65, .refcnt = 0, .insensitive = NULL };
static lwc_string ntux_lwc_resource_str = { .len = 8, .hash = 0x7265736f, .refcnt = 0, .insensitive = NULL };
static lwc_string ntux_lwc_dummy = { .len = 0, .hash = 0, .refcnt = 0, .insensitive = NULL };

static struct {
	const char *str;
	lwc_string *lwc;
} ntux_lwc_string_map[] = {
	{ "http", &ntux_lwc_http_str },
	{ "https", &ntux_lwc_https_str },
	{ "about", &ntux_lwc_about_str },
	{ "file", &ntux_lwc_file_str },
	{ "resource", &ntux_lwc_resource_str },
};

lwc_error lwc_intern_string(const char *str, size_t len, lwc_string **out) {
	for (size_t i = 0; i < sizeof(ntux_lwc_string_map)/sizeof(ntux_lwc_string_map[0]); i++) {
		if (len == strlen(ntux_lwc_string_map[i].str) && 
		    strncmp(str, ntux_lwc_string_map[i].str, len) == 0) {
			*out = ntux_lwc_string_map[i].lwc;
			return lwc_error_ok;
		}
	}
	*out = &ntux_lwc_dummy;
	return lwc_error_ok;
}
void lwc_string_destroy(lwc_string *str) { (void)str; }
void lwc_iterate_strings(lwc_iteration_callback_fn cb, void *pw) { (void)cb; (void)pw; }

/* ===== parserutils stubs ===== */
parserutils_error parserutils_charset_utf8_char_byte_length(const uint8_t *c, size_t *len) { (void)c; *len = 1; return PARSERUTILS_OK; }
parserutils_error parserutils_charset_utf8_from_ucs4(uint32_t ucs4, uint8_t **utf8, size_t *len) { (void)ucs4; (void)utf8; *len = 0; return PARSERUTILS_OK; }
parserutils_error parserutils_charset_utf8_length(const uint8_t *data, size_t datalen, size_t *len) { (void)data; (void)datalen; *len = 0; return PARSERUTILS_OK; }
parserutils_error parserutils_charset_utf8_next(const uint8_t *data, size_t len, size_t off, uint32_t *next) { (void)data; (void)len; (void)off; (void)next; return PARSERUTILS_OK; }
parserutils_error parserutils_charset_utf8_prev(const uint8_t *data, size_t off, uint32_t *prev) { (void)data; (void)off; (void)prev; return PARSERUTILS_OK; }
parserutils_error parserutils_charset_utf8_to_ucs4(const uint8_t *c, size_t len, uint32_t *ucs4) { (void)c; (void)len; *ucs4 = 0; return PARSERUTILS_OK; }
parserutils_error parserutils_inputstream_advance(void *stream, size_t count) { (void)stream; (void)count; return PARSERUTILS_OK; }
parserutils_error parserutils_inputstream_append(void *stream, const uint8_t *data, size_t len) { (void)stream; (void)data; (void)len; return PARSERUTILS_OK; }
parserutils_error parserutils_inputstream_create(const uint8_t *data, size_t len, void *hack, void **stream) { (void)data; (void)len; (void)hack; *stream = (void*)0x1; return PARSERUTILS_OK; }
parserutils_error parserutils_inputstream_destroy(void *stream) { (void)stream; return PARSERUTILS_OK; }
parserutils_error parserutils_inputstream_peek(void *stream, size_t count, const uint8_t **data, size_t *datalen) { (void)stream; (void)count; *data = NULL; *datalen = 0; return PARSERUTILS_OK; }

/* ===== libc stubs ===== */
int atexit(void (*func)(void)) { (void)func; return 0; }
float ceilf(float x) { return x; }
int dirfd(void *dirp) { (void)dirp; return -1; }
int fstatat(int dirfd, const char *pathname, struct stat *buf, int flags) { (void)dirfd; (void)pathname; (void)buf; (void)flags; return -1; }
int ftruncate(int fd, off_t length) { (void)fd; (void)length; return -1; }
int gzclose(gzFile file) { (void)file; return 0; }
const char *gzgets(gzFile file, char *buf, int len) { (void)file; (void)buf; (void)len; return NULL; }
gzFile gzopen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
size_t iconv(intptr_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft) { (void)cd; (void)inbuf; (void)inbytesleft; (void)outbuf; (void)outbytesleft; return (size_t)-1; }
int iconv_close(intptr_t cd) { (void)cd; return 0; }
intptr_t iconv_open(const char *tocode, const char *fromcode) { (void)tocode; (void)fromcode; return (intptr_t)-1; }
int inet_aton(const char *cp, void *inp) { (void)cp; (void)inp; return 0; }
int inet_pton(int af, const char *src, void *dst) { (void)af; (void)src; (void)dst; return -1; }
int isascii(int c) { return (c >= 0 && c <= 127); }
time_t mktime(struct tm *tm) { (void)tm; return 0; }
void perror(const char *s) { (void)s; }
int rand(void) { return 0; }
int scandir(const char *dirp, struct dirent ***namelist, int (*filter)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **)) { (void)dirp; (void)namelist; (void)filter; (void)compar; return -1; }
int socket(int domain, int type, int protocol) { (void)domain; (void)type; (void)protocol; return -1; }
char *strcasestr(const char *haystack, const char *needle) { (void)haystack; (void)needle; return NULL; }
size_t strcspn(const char *s, const char *reject) { (void)s; (void)reject; return 0; }
char *strndup(const char *s, size_t n) { (void)s; (void)n; return NULL; }
char *strtok(char *str, const char *delim) { (void)str; (void)delim; return NULL; }
long long strtoll(const char *nptr, char **endptr, int base) { (void)nptr; (void)endptr; (void)base; return 0; }
int uname(void *buf) { (void)buf; return -1; }
int unlinkat(int dirfd, const char *pathname, int flags) { (void)dirfd; (void)pathname; (void)flags; return -1; }

/* ===== nsu stubs ===== */
int nsu_base64_decode_alloc(const char *in, size_t inlen, uint8_t **out, size_t *outlen) { (void)in; (void)inlen; *out = NULL; *outlen = 0; return -1; }
int nsu_base64_decode_alloc_url(const char *in, size_t inlen, uint8_t **out, size_t *outlen) { (void)in; (void)inlen; *out = NULL; *outlen = 0; return -1; }
int nsu_base64_encode(const uint8_t *in, size_t inlen, char **out, size_t *outlen) { (void)in; (void)inlen; *out = NULL; *outlen = 0; return -1; }
int nsu_base64_encode_url(const uint8_t *in, size_t inlen, char **out, size_t *outlen) { (void)in; (void)inlen; *out = NULL; *outlen = 0; return -1; }
uint64_t nsu_getmonotonic_ms(void) { return 0; }
int64_t nsu_pread(int fd, void *buf, size_t count, int64_t offset) { (void)fd; (void)buf; (void)count; (void)offset; return -1; }
int64_t nsu_pwrite(int fd, const void *buf, size_t count, int64_t offset) { (void)fd; (void)buf; (void)count; (void)offset; return -1; }

/* ===== ntux stubs ===== */

/* ===== WebP stubs ===== */
int WebPDecodeARGBInto(void) { return -1; }
int WebPDecodeBGRAInto(void) { return -1; }
int WebPDecodeRGBAInto(void) { return -1; }
int WebPGetFeatures(void) { return -1; }
int WebPGetInfo(void) { return -1; }

/* ===== zlib stubs ===== */
int inflateInit2(z_streamp strm, int windowBits) { (void)strm; (void)windowBits; return 0; }
int inflate(z_streamp strm, int flush) { (void)strm; (void)flush; return 0; }
int inflateEnd(z_streamp strm) { (void)strm; return 0; }
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len) { (void)crc; (void)buf; (void)len; return 0; }

/* ===== Internal/macro-backed stubs (called via macros from headers) ===== */
void _dom_event_unref(void *evt) { (void)evt; }
int _dom_event_create(void **evt) { (void)evt; return 0; }
int _dom_event_get_target(void *event, void **target) { (void)event; *target = NULL; return 0; }
int _dom_event_init(void *event, void *type, int bubbles, int cancelable) { (void)event; (void)type; (void)bubbles; (void)cancelable; return 0; }
int _dom_keyboard_event_create(void **event) { (void)event; return 0; }
int _dom_keyboard_event_init(void *event, void *type, int bubbles, int cancelable, void *view, int ctrl, int alt, int shift, int meta, int32_t key, int32_t char_code, int32_t location) { (void)event; (void)type; (void)bubbles; (void)cancelable; (void)view; (void)ctrl; (void)alt; (void)shift; (void)meta; (void)key; (void)char_code; (void)location; return 0; }
int _dom_namednodemap_item(void *map, uint32_t index, void **node) { (void)map; (void)index; *node = NULL; return 2; }
int _dom_node_contains(void *node, void *other, int *ret) { (void)node; (void)other; *ret = 0; return 0; }
int _dom_nodelist_item(void *list, uint32_t index, void **node) { (void)list; (void)index; *node = NULL; return 2; }
int dom__html_select_element_get_options(void *elem, void **opts) { (void)elem; *opts = NULL; return 0; }
lwc_error lwc__intern_caseless_string(lwc_string *str) { (void)str; return lwc_error_ok; }
int nsbmp_init(void) { return 0; }
int nsgif_init(void) { return 0; }
int nsico_init(void) { return 0; }
int nsjpeg_init(void) { return 0; }
int nsjpegxl_init(void) { return 0; }
int nsrsvg_init(void) { return 0; }
int nswebp_init(void) { return 0; }
