#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "รวม": การ์ดช่องวัด
//    Single  4 การ์ด 2x2  A0-A3 เทียบ GND (single-ended)
//    Diff    2 การ์ดเต็มกว้าง (differential) A0-A1 / A2-A3 (ค่าบวก/ลบ แถบเริ่มจากกลาง)
//  แตะการ์ด = เปิดแท็บ "ช่อง" ของช่องนั้น
// =====================================================================

namespace {

// สร้างเมื่อเปิดดู / ลบเมื่อออก (แท็บ ADC): gTick = nullptr แปลว่าตอนนี้ไม่มีหน้านี้ ห้ามแตะ widget
lv_timer_t *gTick = nullptr;

constexpr int CARD_W_HALF = 153, CARD_W_FULL = 312, CARD_H = 60;  // สูง 60: ใต้แถวปุ่มย่อยของแท็บ ADC

struct Card {
    lv_obj_t *box, *name, *volts, *value, *raw, *bar, *barFill, *zero;
    int slot;
};
Card gCard[4];
lv_obj_t *gSegSingle, *gSegDiff, *gDot, *gInfo;

void styleSeg(lv_obj_t *b, bool on)
{
    lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(on ? UI_BG : UI_MUTED), 0);
}

void setMode(bool diff)
{
    hal::AdsCfg c = hal::adsCfg();
    if (c.diff == diff) return;
    c.diff = diff;
    hal::adsSetCfg(c);
    ui_ads_changed();
}

void onSingle(lv_event_t *)
{
    setMode(false);
}

void onDiff(lv_event_t *)
{
    setMode(true);
}

void onCard(lv_event_t *e)
{
    const Card *c = static_cast<const Card *>(lv_event_get_user_data(e));
    ui_detail_select(c->slot);
}

lv_obj_t *segBtn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 62, 30);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACCENT), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = ui_label(b, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void buildCard(lv_obj_t *parent, int i)
{
    Card &c = gCard[i];
    c.box = ui_box(parent, UI_CARD, 8);
    lv_obj_set_size(c.box, CARD_W_HALF, CARD_H);
    lv_obj_set_style_pad_all(c.box, 6, 0);
    lv_obj_add_flag(c.box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c.box, onCard, LV_EVENT_CLICKED, &c);

    c.name = ui_label(c.box, "", nullptr, lv_color_hex(UI_MUTED));
    lv_label_set_recolor(c.name, true);  // "#38bdf8 A0# ปุ่มหมุน"
    lv_obj_align(c.name, LV_ALIGN_TOP_LEFT, 0, -3);
    c.volts = ui_label(c.box, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(c.volts, LV_ALIGN_TOP_RIGHT, 0, -3);
    c.value = ui_label(c.box, "--", &font_th_24, lv_color_hex(UI_TEXT));
    lv_obj_align(c.value, LV_ALIGN_LEFT_MID, 0, 2);
    c.raw = ui_label(c.box, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(c.raw, LV_ALIGN_RIGHT_MID, 0, 6);

    c.bar = ui_box(c.box, UI_LINE, 3);
    lv_obj_set_size(c.bar, LV_PCT(100), 6);
    lv_obj_align(c.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    c.barFill = ui_box(c.bar, UI_ACCENT, 3);
    lv_obj_set_size(c.barFill, 0, 6);
    c.zero = ui_box(c.box, UI_TEXT, 0);  // เส้นศูนย์ของโหมดต่าง
    lv_obj_set_size(c.zero, 2, 12);
    lv_obj_align(c.zero, LV_ALIGN_BOTTOM_MID, 0, 3);
    for (lv_obj_t *o : {c.name, c.volts, c.value, c.raw, c.bar, c.barFill, c.zero}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

void setBar(Card &c, float value, bool diff)
{
    float lo, hi;
    hal::adsRange(static_cast<uint8_t>(c.slot), lo, hi);
    const int w = lv_obj_get_content_width(c.box);
    float f = (hi > lo) ? (value - lo) / (hi - lo) : 0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    if (!diff) {
        lv_obj_set_x(c.barFill, 0);
        lv_obj_set_width(c.barFill, static_cast<lv_coord_t>(f * w));
        return;
    }
    // ต่าง: แถบเริ่มจากกลาง ไปขวา = บวก ไปซ้าย = ลบ
    const int mid = w / 2, pos = static_cast<int>(f * w);
    lv_obj_set_x(c.barFill, pos < mid ? pos : mid);
    lv_obj_set_width(c.barFill, pos < mid ? mid - pos : pos - mid);
    lv_obj_set_style_bg_color(c.barFill, lv_color_hex(pos < mid ? UI_WARN : ui_slot_color(c.slot)), 0);
}

void tick(lv_timer_t *)
{
    const hal::AdsState &st = hal::ads();
    const bool live = st.found || st.demo;
    char buf[64];
    if (live) {
        snprintf(buf, sizeof(buf), "%s 0x%02X%s\n±%.3f V  %u SPS", hal::adsChipName(st.chip), st.addr, st.demo ? " จำลอง" : "",
                 hal::ADS_FSR[hal::adsCfg().gain], hal::adsSps());
    } else {
        snprintf(buf, sizeof(buf), "ไม่พบ ADS1x15\nดูแท็บ I2C");
    }
    ui_set_text(gInfo, buf);
    lv_obj_set_style_bg_color(gDot, lv_color_hex(st.demo ? UI_OUT : live ? UI_OK : UI_BAD), 0);

    const bool diff = hal::adsCfg().diff;
    for (int i = 0; i < hal::adsSlotCount(); i++) {
        Card &c = gCard[i];
        const hal::AdsSlot &s = hal::adsSlot(static_cast<uint8_t>(c.slot));
        if (!live || s.n == 0) {
            ui_set_text(c.value, "--");
            ui_set_text(c.volts, "");
            ui_set_text(c.raw, "");
            continue;
        }
        char num[16];
        ui_fmt_value(num, sizeof(num), c.slot, s.value);
        snprintf(buf, sizeof(buf), "%s %s", num, ui_slot_unit(c.slot));
        ui_set_text(c.value, buf);
        ui_fmt_volts(buf, sizeof(buf), s.volts);
        ui_set_text(c.volts, buf);
        snprintf(buf, sizeof(buf), s.clip ? "เกินช่วง!" : "%d", s.counts);
        ui_set_text(c.raw, buf);
        lv_obj_set_style_text_color(c.raw, lv_color_hex(s.clip ? UI_BAD : UI_MUTED), 0);
        lv_obj_set_style_bg_color(c.box, lv_color_hex(s.alarm ? UI_ALARM_BG : UI_CARD), 0);
        lv_obj_set_style_text_color(c.value, lv_color_hex(s.alarm ? UI_BAD : UI_TEXT), 0);
        setBar(c, s.value, diff);
    }
}

}  // namespace

void ui_overview_close()
{
    if (gTick != nullptr) lv_timer_del(gTick);
    gTick = nullptr;
}

void ui_overview_refresh()
{
    if (gTick == nullptr) return;
    const bool diff = hal::adsCfg().diff;
    styleSeg(gSegSingle, !diff);
    styleSeg(gSegDiff, diff);
    for (int i = 0; i < 4; i++) {
        Card &c = gCard[i];
        if (i >= hal::adsSlotCount()) {
            lv_obj_add_flag(c.box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(c.box, LV_OBJ_FLAG_HIDDEN);
        c.slot = hal::adsFirstSlot() + i;
        lv_obj_set_width(c.box, diff ? CARD_W_FULL : CARD_W_HALF);
        lv_obj_update_layout(c.box);
        char buf[80];
        char hex[8];
        snprintf(hex, sizeof(hex), "%06lx", static_cast<unsigned long>(ui_slot_color(c.slot)));
        snprintf(buf, sizeof(buf), "#%s %s# %s", hex, hal::ADS_SLOT_NAMES[c.slot], ui_slot_name(c.slot));
        lv_label_set_text(c.name, buf);
        lv_obj_set_style_bg_color(c.barFill, lv_color_hex(ui_slot_color(c.slot)), 0);
        if (diff) lv_obj_clear_flag(c.zero, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(c.zero, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(c.box, lv_color_hex(UI_CARD), 0);
    }
    tick(nullptr);
}

void ui_overview_tab(lv_obj_t *tab)
{
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    // ---- แถวบน: โหมด Single / Diff + สถานะชิป ----
    lv_obj_t *head = ui_row(tab);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_t *seg = ui_box(head, UI_CARD, 8);
    lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(seg, 2, 0);
    lv_obj_set_style_border_color(seg, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_clip_corner(seg, true, 0);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    gSegSingle = segBtn(seg, "Single", onSingle);  // single-ended: A0-A3 เทียบ GND
    gSegDiff = segBtn(seg, "Diff", onDiff);        // differential: A0-A1 / A2-A3
    gDot = ui_box(head, UI_BAD, LV_RADIUS_CIRCLE);
    lv_obj_set_size(gDot, 10, 10);
    gInfo = ui_label(head, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_style_text_line_space(gInfo, -4, 0);
    lv_obj_set_flex_grow(gInfo, 1);

    // ---- การ์ด ----
    lv_obj_t *grid = ui_row(tab);
    lv_obj_set_style_pad_column(grid, 5, 0);
    lv_obj_set_style_pad_row(grid, 5, 0);
    for (int i = 0; i < 4; i++) buildCard(grid, i);

    gTick = lv_timer_create(tick, app::UI_REFRESH_MS, nullptr);
    ui_overview_refresh();
}
