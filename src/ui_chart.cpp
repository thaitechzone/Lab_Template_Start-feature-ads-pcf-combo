#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "กราฟ": กราฟเส้นสดของทุกช่อง (หน่วยโวลต์ แกนเดียวกัน)
//    เก็บจุดเป็นสัดส่วนของ FSR x 1000 (-1000..1000) เพราะ lv_coord_t เป็น int16
//    ปรับแกน Y เองจากจุดที่เห็นอยู่ / ช่วงเวลา 5 วินาที - 5 นาที (100 จุดเต็มกราฟ) / หยุดดูได้
// =====================================================================

namespace {

constexpr int POINTS = 100;
constexpr int SPAN_COUNT = 5;
constexpr uint32_t SPAN_MS[SPAN_COUNT] = {5000, 10000, 30000, 60000, 300000};
constexpr const char *SPAN_TEXT[SPAN_COUNT] = {"5 วิ", "10 วิ", "30 วิ", "1 นาที", "5 นาที"};

lv_obj_t *gChart, *gChip[4], *gSpanBtn, *gPauseBtn, *gYLab[3], *gLegend[4];
lv_chart_series_t *gSer[4];
lv_timer_t *gTimer = nullptr;  // nullptr = หน้ากราฟไม่ได้สร้างอยู่ (แท็บ ADC สร้างเมื่อเปิดดู / ลบเมื่อออก)
bool gShow[4] = {true, true, true, true};
bool gPaused = false;
int gSpan = 1;

int slotOf(int i)
{
    return hal::adsFirstSlot() + i;
}

void styleChip(int i)
{
    lv_obj_t *c = gChip[i];
    const uint32_t col = ui_slot_color(slotOf(i));
    lv_obj_set_style_bg_color(c, lv_color_hex(gShow[i] ? col : UI_BG), 0);
    lv_obj_set_style_text_color(c, lv_color_hex(gShow[i] ? UI_BG : UI_MUTED), 0);
    lv_obj_set_style_border_color(c, lv_color_hex(gShow[i] ? col : UI_LINE), 0);
    lv_chart_hide_series(gChart, gSer[i], !gShow[i]);
}

void clearPoints()
{
    for (int i = 0; i < 4; i++) lv_chart_set_all_value(gChart, gSer[i], LV_CHART_POINT_NONE);
}

// แกน Y: หาค่าต่ำ/สูงของจุดที่เห็น เผื่อขอบ 10% ช่วงแคบสุด 2% ของ FSR
void autoscale()
{
    int lo = 32767, hi = -32768;
    for (int i = 0; i < hal::adsSlotCount(); i++) {
        if (!gShow[i]) continue;
        const lv_coord_t *ys = lv_chart_get_y_array(gChart, gSer[i]);
        for (int k = 0; k < POINTS; k++) {
            if (ys[k] == LV_CHART_POINT_NONE) continue;
            if (ys[k] < lo) lo = ys[k];
            if (ys[k] > hi) hi = ys[k];
        }
    }
    if (lo > hi) {
        lo = 0;
        hi = 1000;
    }
    int pad = (hi - lo) / 10;
    if (hi - lo < 20) pad = (20 - (hi - lo)) / 2 + 2;
    lo -= pad;
    hi += pad;
    lv_chart_set_range(gChart, LV_CHART_AXIS_PRIMARY_Y, static_cast<lv_coord_t>(lo), static_cast<lv_coord_t>(hi));

    const float fsr = hal::ADS_FSR[hal::adsCfg().gain];
    const int marks[3] = {hi, (lo + hi) / 2, lo};
    char buf[16];
    for (int i = 0; i < 3; i++) {
        const float v = marks[i] * fsr / 1000.0f;
        if (fsr < 1.0f) snprintf(buf, sizeof(buf), "%.0fm", v * 1000.0f);
        else snprintf(buf, sizeof(buf), "%.2f", v);
        ui_set_text(gYLab[i], buf);
    }
}

void sample(lv_timer_t *)
{
    const hal::AdsState &st = hal::ads();
    const bool live = st.found || st.demo;
    const float fsr = hal::ADS_FSR[hal::adsCfg().gain];
    char buf[24];
    for (int i = 0; i < hal::adsSlotCount(); i++) {
        const hal::AdsSlot &s = hal::adsSlot(static_cast<uint8_t>(slotOf(i)));
        const bool ok = live && s.n > 0;
        if (!gPaused) lv_chart_set_next_value(gChart, gSer[i], ok ? static_cast<lv_coord_t>(s.volts / fsr * 1000.0f) : LV_CHART_POINT_NONE);
        if (ok) {
            char v[16];
            ui_fmt_volts(v, sizeof(v), s.volts);
            snprintf(buf, sizeof(buf), "%s", v);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        ui_set_text(gLegend[i], buf);
    }
    if (!gPaused) autoscale();
}

void onChip(lv_event_t *e)
{
    const int i = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    gShow[i] = !gShow[i];
    styleChip(i);
    autoscale();
}

void onSpan(lv_event_t *)
{
    gSpan = (gSpan + 1) % SPAN_COUNT;
    lv_label_set_text(lv_obj_get_child(gSpanBtn, 0), SPAN_TEXT[gSpan]);
    lv_timer_set_period(gTimer, SPAN_MS[gSpan] / POINTS);
    clearPoints();
    Serial.printf("[UI] chart span %lu ms\n", static_cast<unsigned long>(SPAN_MS[gSpan]));
}

void onPause(lv_event_t *)
{
    gPaused = !gPaused;
    lv_label_set_text(lv_obj_get_child(gPauseBtn, 0), gPaused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    if (gPaused) lv_obj_add_state(gPauseBtn, LV_STATE_CHECKED);
    else lv_obj_clear_state(gPauseBtn, LV_STATE_CHECKED);
}

lv_obj_t *chip(lv_obj_t *parent, int i)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 50, 30);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = ui_label(b, "A0");
    lv_obj_center(l);
    lv_obj_add_event_cb(b, onChip, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    return b;
}

}  // namespace

void ui_chart_close()
{
    if (gTimer != nullptr) lv_timer_del(gTimer);
    gTimer = nullptr;
}

void ui_chart_refresh()
{
    if (gTimer == nullptr) return;
    const int count = hal::adsSlotCount();
    for (int i = 0; i < 4; i++) {
        const bool used = i < count;
        if (used) {
            lv_obj_clear_flag(gChip[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(gLegend[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lv_obj_get_child(gChip[i], 0), hal::ADS_SLOT_NAMES[slotOf(i)]);
            lv_obj_set_width(gChip[i], count == 2 ? 70 : 50);
            lv_chart_set_series_color(gChart, gSer[i], lv_color_hex(ui_slot_color(slotOf(i))));
            lv_obj_set_style_text_color(gLegend[i], lv_color_hex(ui_slot_color(slotOf(i))), 0);
            styleChip(i);
        } else {
            lv_obj_add_flag(gChip[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(gLegend[i], LV_OBJ_FLAG_HIDDEN);
            lv_chart_hide_series(gChart, gSer[i], true);
        }
    }
    clearPoints();  // โหมด/gain เปลี่ยน จุดเก่าเทียบกันไม่ได้แล้ว
    autoscale();
}

void ui_chart_tab(lv_obj_t *tab)
{
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = ui_row(tab);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 4, 0);
    for (int i = 0; i < 4; i++) gChip[i] = chip(top, i);
    lv_obj_t *gap = lv_obj_create(top);
    lv_obj_remove_style_all(gap);
    lv_obj_set_size(gap, 1, 1);
    lv_obj_set_flex_grow(gap, 1);
    gSpanBtn = ui_btn(top, SPAN_TEXT[gSpan], onSpan);
    lv_obj_set_style_min_height(gSpanBtn, 30, 0);
    lv_obj_set_style_pad_ver(gSpanBtn, 2, 0);
    lv_obj_set_style_pad_hor(gSpanBtn, 6, 0);
    gPauseBtn = ui_btn(top, LV_SYMBOL_PAUSE, onPause);
    lv_obj_set_style_min_height(gPauseBtn, 30, 0);
    lv_obj_set_style_pad_ver(gPauseBtn, 2, 0);

    // ---- กราฟ + ป้ายแกน Y ----
    lv_obj_t *box = ui_box(tab, UI_CARD, 8);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_flex_grow(box, 1);
    for (int i = 0; i < 3; i++) {
        gYLab[i] = ui_label(box, "", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
        lv_obj_align(gYLab[i], i == 0 ? LV_ALIGN_TOP_LEFT : i == 1 ? LV_ALIGN_LEFT_MID : LV_ALIGN_BOTTOM_LEFT, 3, i == 0 ? 2 : i == 2 ? -2 : 0);
    }
    gChart = lv_chart_create(box);
    lv_obj_set_size(gChart, 262, LV_PCT(100));
    lv_obj_align(gChart, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(gChart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gChart, 0, 0);
    lv_obj_set_style_pad_all(gChart, 6, 0);
    lv_obj_set_style_line_color(gChart, lv_color_hex(UI_LINE), LV_PART_MAIN);
    lv_obj_set_style_size(gChart, 0, LV_PART_INDICATOR);  // ไม่วาดจุด วาดแต่เส้น
    lv_obj_set_style_line_width(gChart, 2, LV_PART_ITEMS);
    lv_obj_clear_flag(gChart, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_type(gChart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(gChart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(gChart, POINTS);
    lv_chart_set_div_line_count(gChart, 5, 0);
    for (int i = 0; i < 4; i++) gSer[i] = lv_chart_add_series(gChart, lv_color_hex(ui_slot_color(i)), LV_CHART_AXIS_PRIMARY_Y);

    // ---- ค่าล่าสุด ----
    lv_obj_t *legend = ui_row(tab);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < 4; i++) gLegend[i] = ui_label(legend, "--");

    gTimer = lv_timer_create(sample, SPAN_MS[gSpan] / POINTS, nullptr);
    ui_chart_refresh();
}
