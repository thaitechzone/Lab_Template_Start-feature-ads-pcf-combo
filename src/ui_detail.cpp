#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "ช่อง": ดูทีละช่องแบบละเอียด
//    ซ้าย  เกจโค้ง (lv_arc) ช่วงเกจ = ค่าที่ 0 V ถึงค่าที่ FSR (หรือ 3.3 V) ของช่องนั้น
//    ขวา   เลือกช่อง + ต่ำสุด/สูงสุด/เฉลี่ย/ค่าดิบ/จำนวนครั้ง + รีเซ็ต / ค้างค่า
// =====================================================================

namespace {

// สร้างเมื่อเปิดดู / ลบเมื่อออก (แท็บ ADC): gTick = nullptr แปลว่าตอนนี้ไม่มีหน้านี้ ห้ามแตะ widget
lv_timer_t *gTick = nullptr;

lv_obj_t *gArc, *gName, *gValue, *gUnit, *gVolts, *gLo, *gHi, *gAlarm;
lv_obj_t *gChBtn[4], *gHoldBtn;
lv_obj_t *gStat[5];
int gSlot = 0;
bool gHold = false;

void selectIndex(int i)
{
    gSlot = hal::adsFirstSlot() + i;
    ui_detail_refresh();
}

void onCh(lv_event_t *e)
{
    selectIndex(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

void onReset(lv_event_t *)
{
    hal::adsResetStats(gSlot);
    hal::soundTone(1568, 40);
}

void onHold(lv_event_t *)
{
    gHold = !gHold;
    if (gHold) lv_obj_add_state(gHoldBtn, LV_STATE_CHECKED);
    else lv_obj_clear_state(gHoldBtn, LV_STATE_CHECKED);
    Serial.printf("[UI] detail hold %s\n", gHold ? "ON" : "OFF");
}

lv_obj_t *statRow(lv_obj_t *parent, const char *title)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 19);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = ui_label(r, title, nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *v = ui_label(r, "--", nullptr, lv_color_hex(UI_TEXT));
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    return v;
}

void fmtWithUnit(char *buf, size_t n, float value)
{
    char num[16];
    ui_fmt_value(num, sizeof(num), gSlot, value);
    snprintf(buf, n, "%s %s", num, ui_slot_unit(gSlot));
}

void tick(lv_timer_t *)
{
    if (gHold) return;
    const hal::AdsState &st = hal::ads();
    const hal::AdsSlot &s = hal::adsSlot(static_cast<uint8_t>(gSlot));
    char buf[48];
    if (!(st.found || st.demo) || s.n == 0) {
        ui_set_text(gValue, "--");
        ui_set_text(gVolts, "");
        lv_arc_set_value(gArc, 0);
        return;
    }
    ui_fmt_value(buf, sizeof(buf), gSlot, s.value);
    ui_set_text(gValue, buf);
    lv_obj_set_style_text_color(gValue, lv_color_hex(s.alarm ? UI_BAD : UI_TEXT), 0);
    ui_fmt_volts(buf, sizeof(buf), s.volts);
    ui_set_text(gVolts, buf);

    float lo, hi;
    hal::adsRange(static_cast<uint8_t>(gSlot), lo, hi);
    const float f = hi > lo ? (s.value - lo) / (hi - lo) : 0;
    const int v = static_cast<int>(f * 1000);
    lv_arc_set_value(gArc, static_cast<int16_t>(v < 0 ? 0 : v > 1000 ? 1000 : v));

    // min/max เก็บเป็นแรงดัน แปลงหน่วยตอนแสดง (ถ้าความชันติดลบ ต่ำ/สูงจะสลับกัน)
    float a = hal::adsScale(static_cast<uint8_t>(gSlot), s.vMin), b = hal::adsScale(static_cast<uint8_t>(gSlot), s.vMax);
    if (a > b) {
        const float t = a;
        a = b;
        b = t;
    }
    fmtWithUnit(buf, sizeof(buf), a);
    ui_set_text(gStat[0], buf);
    fmtWithUnit(buf, sizeof(buf), b);
    ui_set_text(gStat[1], buf);
    fmtWithUnit(buf, sizeof(buf), hal::adsScale(static_cast<uint8_t>(gSlot), static_cast<float>(s.vSum / s.n)));
    ui_set_text(gStat[2], buf);
    snprintf(buf, sizeof(buf), "%d%s", s.counts, s.clip ? " เกิน!" : "");
    ui_set_text(gStat[3], buf);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.n));
    ui_set_text(gStat[4], buf);
}

}  // namespace

void ui_detail_select(int slot)
{
    const int first = hal::adsFirstSlot();
    if (slot < first || slot >= first + hal::adsSlotCount()) slot = first;
    gSlot = slot;
    ui_detail_refresh();
    ui_goto_tab(UI_TAB_ADC);
    ui_adc_show(1);
    Serial.printf("[UI] detail %s\n", hal::ADS_SLOT_NAMES[slot]);
}

void ui_detail_close()
{
    if (gTick != nullptr) lv_timer_del(gTick);
    gTick = nullptr;
}

void ui_detail_refresh()
{
    if (gTick == nullptr) return;
    const int first = hal::adsFirstSlot(), count = hal::adsSlotCount();
    if (gSlot < first || gSlot >= first + count) gSlot = first;
    for (int i = 0; i < 4; i++) {
        if (i >= count) {
            lv_obj_add_flag(gChBtn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(gChBtn[i], LV_OBJ_FLAG_HIDDEN);
        const int slot = first + i;
        lv_label_set_text(lv_obj_get_child(gChBtn[i], 0), hal::ADS_SLOT_NAMES[slot]);
        lv_obj_set_style_bg_color(gChBtn[i], lv_color_hex(ui_slot_color(slot)), LV_STATE_CHECKED);
        if (slot == gSlot) lv_obj_add_state(gChBtn[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(gChBtn[i], LV_STATE_CHECKED);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s", hal::ADS_SLOT_NAMES[gSlot], ui_slot_name(gSlot));
    ui_set_text(gName, buf);
    ui_set_text(gUnit, ui_slot_unit(gSlot));
    lv_obj_set_style_arc_color(gArc, lv_color_hex(ui_slot_color(gSlot)), LV_PART_INDICATOR);

    float lo, hi;
    hal::adsRange(static_cast<uint8_t>(gSlot), lo, hi);
    char num[16];
    ui_fmt_value(num, sizeof(num), gSlot, lo);
    ui_set_text(gLo, num);
    ui_fmt_value(num, sizeof(num), gSlot, hi);
    ui_set_text(gHi, num);

    const hal::AdsChCfg &c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    char a[16] = "", b[16] = "";
    if (c.loOn) ui_fmt_value(a, sizeof(a), gSlot, c.lo);
    if (c.hiOn) ui_fmt_value(b, sizeof(b), gSlot, c.hi);
    if (c.loOn && c.hiOn) snprintf(buf, sizeof(buf), "เตือน <%s >%s", a, b);
    else if (c.hiOn) snprintf(buf, sizeof(buf), "เตือนเมื่อ > %s", b);
    else if (c.loOn) snprintf(buf, sizeof(buf), "เตือนเมื่อ < %s", a);
    else buf[0] = '\0';  // ไม่ได้ตั้งขีดเตือน -> ไม่แสดงอะไร
    ui_set_text(gAlarm, buf);
    tick(nullptr);
}

void ui_detail_tab(lv_obj_t *tab)
{
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tab, 6, 0);

    // ---- ซ้าย: เกจ ----
    lv_obj_t *left = ui_box(tab, UI_CARD, 8);
    lv_obj_set_size(left, 160, LV_PCT(100));
    gArc = lv_arc_create(left);
    lv_obj_set_size(gArc, 150, 150);
    lv_obj_align(gArc, LV_ALIGN_TOP_MID, 0, 5);
    lv_arc_set_bg_angles(gArc, 135, 45);
    lv_arc_set_range(gArc, 0, 1000);
    lv_arc_set_value(gArc, 0);
    lv_obj_remove_style(gArc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(gArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(gArc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gArc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gArc, lv_color_hex(UI_LINE), LV_PART_MAIN);

    gName = ui_label(gArc, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(gName, LV_ALIGN_CENTER, 0, -34);
    gValue = ui_label(gArc, "--", &font_th_24, lv_color_hex(UI_TEXT));
    lv_obj_align(gValue, LV_ALIGN_CENTER, 0, -6);
    gUnit = ui_label(gArc, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(gUnit, LV_ALIGN_CENTER, 0, 18);
    gVolts = ui_label(gArc, "", nullptr, lv_color_hex(UI_TEXT));
    lv_obj_align(gVolts, LV_ALIGN_CENTER, 0, 40);

    gLo = ui_label(left, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(gLo, LV_ALIGN_TOP_LEFT, 14, 140);
    gHi = ui_label(left, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(gHi, LV_ALIGN_TOP_RIGHT, -14, 140);
    gAlarm = ui_label(left, "", nullptr, lv_color_hex(UI_BAD));
    lv_obj_align(gAlarm, LV_ALIGN_BOTTOM_MID, 0, -4);

    // ---- ขวา: เลือกช่อง + สถิติ + ปุ่ม ----
    lv_obj_t *right = lv_obj_create(tab);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 5, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = ui_row(right);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = ui_btn(row, "A0", onCh, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 2, 0);
        lv_obj_set_style_min_height(b, 30, 0);
        gChBtn[i] = b;
    }

    lv_obj_t *card = ui_card(right, nullptr);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_style_pad_ver(card, 4, 0);
    const char *titles[5] = {"ต่ำสุด", "สูงสุด", "เฉลี่ย", "ค่าดิบ", "จำนวนครั้ง"};
    for (int i = 0; i < 5; i++) gStat[i] = statRow(card, titles[i]);

    row = ui_row(right);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_t *b = ui_btn(row, LV_SYMBOL_REFRESH " รีเซ็ต", onReset);
    lv_obj_set_flex_grow(b, 1);
    gHoldBtn = ui_btn(row, LV_SYMBOL_PAUSE " ค้างค่า", onHold);
    lv_obj_set_flex_grow(gHoldBtn, 1);
    for (lv_obj_t *o : {b, gHoldBtn}) lv_obj_set_style_min_height(o, 30, 0);
    lv_obj_add_flag(lv_obj_get_parent(gStat[4]), LV_OBJ_FLAG_HIDDEN);  // ใต้แถวปุ่มย่อยที่ไม่พอ: ซ่อน "จำนวนครั้ง" (ดูได้ทาง Serial v)

    gTick = lv_timer_create(tick, app::UI_REFRESH_MS, nullptr);
    ui_detail_refresh();
}
