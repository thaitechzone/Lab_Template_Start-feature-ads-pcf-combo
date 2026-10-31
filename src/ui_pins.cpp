#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  ส่วน "ตั้งขา" (ใต้แท็บ I/O): เลือกทิศทาง OUT/IN และ "ทำงานเมื่อ LOW" ทีละขา (จำค่าถาวร)
//  + ทดสอบเอาต์พุต: ติด/ดับทั้งหมด (ไฟวิ่ง/นับเลขของแล็บ PCF ถูกแทนด้วย "แถบไฟ" ในแท็บกฎ)
// =====================================================================

namespace {

struct Row {
    lv_obj_t *out, *in, *role, *sw;
};

Row gRow[8];

void styleSeg(lv_obj_t *b, bool on, uint32_t col)
{
    lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(on ? UI_BG : UI_MUTED), 0);
}

void refreshRow(int pin)
{
    const Row &r = gRow[pin];
    const bool in = hal::pcfIsInput(pin);
    styleSeg(r.out, !in, UI_OUT);
    styleSeg(r.in, in, UI_ACCENT);
    ui_set_text(r.role, in ? "ปุ่ม/สวิตช์" : "LED/รีเลย์");
    const bool low = (hal::pcf().lowMask >> pin) & 1;
    if (low) lv_obj_add_state(r.sw, LV_STATE_CHECKED);
    else lv_obj_clear_state(r.sw, LV_STATE_CHECKED);
}

void onOut(lv_event_t *e)
{
    const int pin = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    hal::pcfSetDir(pin, false);
    refreshRow(pin);
}

void onIn(lv_event_t *e)
{
    const int pin = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    hal::pcfSetDir(pin, true);
    refreshRow(pin);
}

void onLow(lv_event_t *e)
{
    const int pin = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    hal::pcfSetActiveLow(pin, lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

void takeAll()
{
    for (uint8_t p = 0; p < 8; p++) hal::rulesManual(p);  // ขาที่กฎคุม: ยึดคืนชั่วคราว
}

void onAllOn(lv_event_t *)
{
    takeAll();
    hal::pcfSetAllOutputs(true);
}

void onAllOff(lv_event_t *)
{
    takeAll();
    hal::pcfSetAllOutputs(false);
}

void onDefaults(lv_event_t *)
{
    hal::pcfResetConfig();
    ui_pins_refresh();
}

lv_obj_t *segBtn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, int pin)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 44, 28);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = ui_label(b, text, &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(pin)));
    return b;
}

lv_obj_t *plainRow(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

}  // namespace

namespace {

lv_obj_t *gOv = nullptr;  // หน้าตั้งขาเต็มจอ (สร้างเมื่อเปิด / ลบเมื่อปิด ประหยัด RAM ~20 KB)

void onClose(lv_event_t *)
{
    if (gOv == nullptr) return;
    lv_obj_del_async(gOv);  // อยู่ใน event ของปุ่มบนหน้านี้: ห้ามลบทันที
    gOv = nullptr;
}

}  // namespace

void ui_pins_refresh()
{
    if (gOv == nullptr) return;
    for (int p = 0; p < 8; p++) refreshRow(p);
}

void ui_pins_open()
{
    if (gOv != nullptr) return;
    gOv = ui_overlay();
    lv_obj_set_style_bg_color(gOv, lv_color_hex(UI_BG), 0);
    lv_obj_t *head = ui_box(gOv, UI_TABBAR, 0);
    lv_obj_set_size(head, UI_W, 32);
    lv_obj_set_style_pad_hor(head, 8, 0);
    lv_obj_t *t = ui_label(head, "ตั้งขา PCF8574 (จำค่าถาวร)", nullptr, lv_color_hex(UI_ACCENT));
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *x = ui_btn(head, LV_SYMBOL_CLOSE " ปิด", onClose);
    lv_obj_set_style_min_height(x, 28, 0);
    lv_obj_set_style_pad_ver(x, 2, 0);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *body = lv_obj_create(gOv);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, UI_W, UI_H - 32);
    lv_obj_set_pos(body, 0, 32);
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_ACTIVE);
    ui_pins_tab(body);
    ui_slide_in(gOv, 40, 200);
    lv_indev_t *indev = lv_indev_get_act();
    if (indev != nullptr) lv_indev_wait_release(indev);
}

void ui_pins_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_row(tab, 4, 0);
    lv_obj_t *head = plainRow(tab);
    lv_obj_set_style_pad_hor(head, 4, 0);
    lv_obj_t *l = ui_label(head, "ขา", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(l, 36);
    l = ui_label(head, "ทิศทาง", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_flex_grow(l, 1);
    ui_label(head, "ติดเมื่อ LOW", nullptr, lv_color_hex(UI_MUTED));

    for (int pin = 0; pin < 8; pin++) {
        lv_obj_t *row = ui_box(tab, UI_CARD, 8);
        lv_obj_set_size(row, LV_PCT(100), 38);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_set_style_pad_column(row, 6, 0);

        char name[4];
        snprintf(name, sizeof(name), "P%d", pin);
        l = ui_label(row, name);
        lv_obj_set_width(l, 28);

        lv_obj_t *seg = ui_box(row, UI_CARD, 8);
        lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(seg, 2, 0);
        lv_obj_set_style_border_color(seg, lv_color_hex(UI_LINE), 0);
        lv_obj_set_style_clip_corner(seg, true, 0);
        lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
        Row &r = gRow[pin];
        r.out = segBtn(seg, "OUT", onOut, pin);
        r.in = segBtn(seg, "IN", onIn, pin);

        r.role = ui_label(row, "", nullptr, lv_color_hex(UI_MUTED));
        lv_obj_set_flex_grow(r.role, 1);

        r.sw = lv_switch_create(row);
        lv_obj_set_size(r.sw, 44, 24);
        lv_obj_add_event_cb(r.sw, onLow, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<uintptr_t>(pin)));
        refreshRow(pin);
    }

    lv_obj_t *card = ui_card(tab, "ทดสอบเอาต์พุต");
    lv_obj_t *row = ui_row(card);
    ui_btn(row, "ติดทั้งหมด", onAllOn);
    ui_btn(row, "ดับทั้งหมด", onAllOff);
    ui_btn(row, LV_SYMBOL_REFRESH " ค่าเริ่มต้น", onDefaults);

    card = ui_card(tab, "ความรู้: ขาแบบ quasi-bidirectional");
    lv_obj_t *note = ui_label(card,
                              "เขียน 0 = ดึงลง GND แรง (รับกระแสได้ ~25 mA)\n"
                              "เขียน 1 = ปล่อยลอย มี pull-up อ่อนๆ (~0.1 mA)\n"
                              "จึงต่อ LED แบบ sink: 3.3V - R 330 - LED - ขา\n"
                              "ขาอินพุตต้องเขียน 1 ไว้ แล้วให้ปุ่มดึงลง GND",
                              nullptr, lv_color_hex(UI_TEXT));
    lv_obj_set_width(note, LV_PCT(100));
}
