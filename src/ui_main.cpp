#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  ui_main.cpp -- ธีม + tabview 6 แท็บ (รวม | ADC | I/O | กฎ | บัส | ระบบ) + ฟังก์ชันช่วยสร้าง widget + ภาพเคลื่อนไหว
// =====================================================================

namespace {

lv_obj_t *gTabview = nullptr;

void splashDone(lv_timer_t *t)
{
    lv_obj_t *splash = static_cast<lv_obj_t *>(t->user_data);
    lv_obj_del(splash);
    lv_timer_del(t);
    if (!hal::touchCal().valid) ui_calibrate_start(true);  // ยังไม่เคย calibrate -> เข้าหน้า calibrate เอง
}

void showSplash()
{
    lv_obj_t *s = ui_overlay();
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_BG), 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(s, "ESP32 CYD Dev Level 2", nullptr, lv_color_hex(UI_MUTED));
    ui_label(s, app::APP_NAME, &font_th_24, lv_color_hex(UI_ACCENT));
    ui_label(s, "ADC 4 ช่อง + I/O 8 ขา บนบัส I2C เดียว", nullptr, lv_color_hex(UI_TEXT));
    ui_label(s, "ADS1115/ADS1015 + PCF8574 | กฎข้ามชิป", nullptr, lv_color_hex(UI_MUTED));
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  |  จอ %s  |  SDA 27 SCL 22", app::FW_VERSION, hal::displayDriverName());
    ui_label(s, buf, nullptr, lv_color_hex(UI_MUTED));
    lv_timer_create(splashDone, 1500, s);
}

}  // namespace

int ui_active_tab()
{
    return gTabview == nullptr ? -1 : static_cast<int>(lv_tabview_get_tab_act(gTabview));
}

void ui_goto_tab(int index)
{
    if (gTabview == nullptr || index < 0 || index >= UI_TAB_COUNT) return;
    lv_tabview_set_act(gTabview, static_cast<uint32_t>(index), LV_ANIM_OFF);
}

void ui_scroll_tab(int y)
{
    if (gTabview == nullptr) return;
    lv_obj_t *page = lv_obj_get_child(lv_tabview_get_content(gTabview), ui_active_tab());
    if (page != nullptr) lv_obj_scroll_to_y(page, y, LV_ANIM_OFF);
}

void ui_ads_changed()
{
    ui_overview_refresh();
    ui_detail_refresh();
    ui_chart_refresh();
    ui_settings_refresh();
    ui_busmap_refresh();
}

void ui_init()
{
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, lv_color_hex(UI_ACCENT), lv_color_hex(UI_OUT), true, &font_th_16);
    lv_disp_set_theme(disp, th);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(UI_BG), 0);

    gTabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, UI_TAB_H);
    lv_obj_set_style_bg_color(gTabview, lv_color_hex(UI_BG), 0);
    // ปิดการปัดซ้าย-ขวาเพื่อเปลี่ยนแท็บ: ลากแถบเลื่อน/วาดเส้นจะได้ไม่เปลี่ยนแท็บโดยไม่ตั้งใจ (กดที่ชื่อแท็บแทน)
    lv_obj_clear_flag(lv_tabview_get_content(gTabview), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tabBtns = lv_tabview_get_tab_btns(gTabview);
    lv_obj_set_style_text_font(tabBtns, &font_th_16, 0);
    lv_obj_set_style_pad_all(tabBtns, 0, 0);
    lv_obj_set_style_bg_color(tabBtns, lv_color_hex(UI_TABBAR), 0);
    lv_obj_set_style_text_color(tabBtns, lv_color_hex(UI_MUTED), LV_PART_ITEMS);
    lv_obj_set_style_text_color(tabBtns, lv_color_hex(UI_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tabBtns, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tabBtns, lv_color_hex(UI_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tabBtns, 3, LV_PART_ITEMS | LV_STATE_CHECKED);

    struct TabDef {
        const char *name;
        void (*build)(lv_obj_t *);
    };
    const TabDef tabs[UI_TAB_COUNT] = {{"รวม", ui_dash_tab}, {"ADC", ui_adc_tab},     {"I/O", ui_io_tab},
                                       {"กฎ", ui_rules_tab}, {"บัส", ui_busmap_tab}, {"ระบบ", ui_sys_tab}};
    for (const TabDef &t : tabs) {
        lv_obj_t *page = lv_tabview_add_tab(gTabview, t.name);
        lv_obj_set_style_pad_all(page, 6, 0);
        lv_obj_set_style_pad_row(page, 5, 0);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_ACTIVE);
        const uint32_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        t.build(page);
        const uint32_t after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        Serial.printf("[UI] tab %s built: %u bytes, heap8 %u KB (block %u KB)\n", t.name, static_cast<unsigned>(before - after),
                      static_cast<unsigned>(after / 1024), static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    }

    showSplash();
}

// ---------------------------------------------------------------------
//  ฟังก์ชันช่วย
// ---------------------------------------------------------------------
lv_obj_t *ui_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (title != nullptr) ui_label(card, title, nullptr, lv_color_hex(UI_ACCENT));
    return card;
}

lv_obj_t *ui_box(lv_obj_t *parent, uint32_t bg, int radius)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

void ui_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

lv_obj_t *ui_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_pad_row(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    if (font != nullptr) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *ui_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user, lv_event_code_t code)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 6, 0);
    lv_obj_set_style_min_height(b, 36, 0);  // เป้าแตะไม่เล็กกว่า ~2 เท่าความคลาดเคลื่อนของจอสัมผัส
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_CARD2), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, lv_color_hex(UI_BG), LV_STATE_CHECKED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (cb != nullptr) lv_obj_add_event_cb(b, cb, code, user);
    return b;
}

lv_obj_t *ui_switch_row(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    ui_label(row, text);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 46, 24);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

lv_obj_t *ui_overlay()
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, UI_W, UI_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_crosshair(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 41, 41);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_obj_create(c);  // เส้นนอน
    lv_obj_remove_style_all(h);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(h, color, 0);
    lv_obj_set_size(h, 41, 1);
    lv_obj_set_pos(h, 0, 20);

    lv_obj_t *v = lv_obj_create(c);  // เส้นตั้ง
    lv_obj_remove_style_all(v);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(v, color, 0);
    lv_obj_set_size(v, 1, 41);
    lv_obj_set_pos(v, 20, 0);

    lv_obj_t *ring = lv_obj_create(c);  // วงกลมรอบเป้า
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 21, 21);
    lv_obj_set_pos(ring, 10, 10);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, color, 0);
    for (lv_obj_t *part : {h, v, ring}) lv_obj_clear_flag(part, LV_OBJ_FLAG_CLICKABLE);  // แตะทะลุไปถึงพื้นหลัง
    return c;
}

void ui_crosshair_move(lv_obj_t *cross, int x, int y)
{
    lv_obj_set_pos(cross, x - 20, y - 20);
}

void ui_no_scroll(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

lv_obj_t *ui_subnav(lv_obj_t *parent, const char *const *names, int count, lv_obj_t **btns, lv_event_cb_t cb)
{
    lv_obj_t *nav = lv_obj_create(parent);
    lv_obj_remove_style_all(nav);
    lv_obj_set_size(nav, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, 4, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < count; i++) {
        lv_obj_t *b = ui_btn(nav, names[i], cb, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 2, 0);
        lv_obj_set_style_pad_ver(b, 2, 0);
        lv_obj_set_style_min_height(b, 30, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(UI_LINE), 0);
        lv_obj_set_style_border_width(b, 0, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_CARD), 0);
        btns[i] = b;
    }
    return nav;
}

void ui_show_sub(lv_obj_t **boxes, lv_obj_t **btns, int count, int index)
{
    if (index < 0 || index >= count) return;
    for (int i = 0; i < count; i++) {
        if (i == index) {
            const bool wasHidden = lv_obj_has_flag(boxes[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(boxes[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(btns[i], LV_STATE_CHECKED);
            if (wasHidden) ui_slide_in(boxes[i], 12, 180);
        } else {
            lv_obj_add_flag(boxes[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(btns[i], LV_STATE_CHECKED);
        }
    }
}

// ---------------------------------------------------------------------
//  ภาพเคลื่อนไหว: lv_anim เปลี่ยนค่า style ทีละเฟรม (ไม่จองบัฟเฟอร์ภาพเพิ่ม)
// ---------------------------------------------------------------------
namespace {

void pulseExec(void *obj, int32_t v)  // v 0..255: วงขยายออก + จางลง
{
    lv_obj_t *o = static_cast<lv_obj_t *>(obj);
    lv_obj_set_style_outline_width(o, static_cast<lv_coord_t>(1 + v * 4 / 255), 0);
    lv_obj_set_style_outline_opa(o, static_cast<lv_opa_t>(220 - v * 220 / 255), 0);
}

void popExec(void *obj, int32_t v)  // v = px ที่หดเข้า (บวก) / ขยายออก (ลบ)
{
    lv_obj_t *o = static_cast<lv_obj_t *>(obj);
    lv_obj_set_style_transform_width(o, static_cast<lv_coord_t>(-v), 0);
    lv_obj_set_style_transform_height(o, static_cast<lv_coord_t>(-v), 0);
}

int32_t popPath(const lv_anim_t *a)  // ยุบ 3 px -> เด้งออก 2 px -> กลับที่เดิม
{
    const int32_t t = static_cast<int32_t>(a->act_time * 100 / a->time);
    if (t < 30) return t * 3 / 30;
    if (t < 70) return 3 - (t - 30) * 5 / 40;
    return -2 + (t - 70) * 2 / 30;
}

struct Flash {
    uint32_t from, to;
};
Flash gFlash[24];
uint8_t gFlashNext = 0;

void flashExec(void *obj, int32_t v)
{
    lv_obj_t *o = static_cast<lv_obj_t *>(obj);
    const Flash *f = static_cast<const Flash *>(lv_obj_get_user_data(o));
    if (f == nullptr) return;
    lv_obj_set_style_bg_color(o, lv_color_hex(ui_mix(f->from, f->to, static_cast<uint8_t>(v))), 0);
}

void barExec(void *obj, int32_t v)
{
    lv_bar_set_value(static_cast<lv_obj_t *>(obj), v, LV_ANIM_OFF);
}

void slideExec(void *obj, int32_t v)  // v = px ที่ยังห่างจากตำแหน่งจริง
{
    lv_obj_set_style_translate_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v), 0);
}

}  // namespace

uint32_t ui_mix(uint32_t a, uint32_t b, uint8_t t)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= static_cast<uint32_t>(ca + (cb - ca) * t / 255) << sh;
    }
    return out;
}

void ui_pulse(lv_obj_t *obj, bool on, uint32_t color)
{
    const bool running = lv_anim_get(obj, pulseExec) != nullptr;
    if (on == running) return;
    if (!on) {
        lv_anim_del(obj, pulseExec);
        lv_obj_set_style_outline_width(obj, 0, 0);
        return;
    }
    lv_obj_set_style_outline_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_outline_pad(obj, 0, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, pulseExec);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 900);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 150);
    lv_anim_start(&a);
}

void ui_pop(lv_obj_t *obj)
{
    lv_anim_del(obj, popExec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, popExec);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 260);
    lv_anim_set_path_cb(&a, popPath);
    lv_anim_start(&a);
}

void ui_flash(lv_obj_t *obj, uint32_t from, uint32_t to, uint32_t ms)
{
    Flash *f = static_cast<Flash *>(lv_obj_get_user_data(obj));
    if (f == nullptr) {  // ผูกช่องเก็บสีไว้กับ obj ครั้งแรก
        f = &gFlash[gFlashNext++ % 24];
        lv_obj_set_user_data(obj, f);
    }
    f->from = from;
    f->to = to;
    lv_anim_del(obj, flashExec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, flashExec);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);
}

void ui_bar_anim(lv_obj_t *bar, int value)
{
    const int cur = lv_bar_get_value(bar);
    if (cur == value) return;
    lv_anim_del(bar, barExec);
    if (abs(cur - value) < 6) {  // ขยับนิดเดียว: ตั้งตรงๆ ไม่ต้องทำ anim
        lv_bar_set_value(bar, value, LV_ANIM_OFF);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, barExec);
    lv_anim_set_values(&a, cur, value);
    lv_anim_set_time(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void ui_slide_in(lv_obj_t *obj, int dy, uint32_t ms)
{
    lv_anim_del(obj, slideExec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, slideExec);
    lv_anim_set_values(&a, dy, 0);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}
