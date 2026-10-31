#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "กฎ": ให้สองชิปคุยกัน  ปุ่มย่อย  กฎ | แถบไฟ | ปุ่มเลือก
//    กฎ       การ์ด 8 กฎ: เงื่อนไข + แถบค่าสดพร้อมขีด + ขาปลายทาง + สวิตช์   แตะการ์ด = หน้าแก้กฎเต็มจอ
//             กฎเริ่มทำงาน -> การ์ดกะพริบส้ม + ตัวเลขเด้ง
//    แถบไฟ    ค่าช่องที่เลือก (เกจโค้ง) -> ไฟบนขาเอาต์พุตทุกขา แบบ แถบ / จุด / VU
//    ปุ่มเลือก  ปุ่ม P4-P7 ของ PCF สั่ง ADS: ช่องถัดไป / gain + / gain - / ค้างค่า / เดี่ยว-ต่าง
//  ตัวประมวลผลอยู่ใน hal_rules.cpp (task "i2c") หน้านี้แค่ตั้งค่า + แสดงผล
// =====================================================================

namespace {

constexpr int SUB_COUNT = 3;
lv_obj_t *gSubBtn[SUB_COUNT], *gSub[SUB_COUNT];

// ---- รายการกฎ ----
struct RCard {
    lv_obj_t *box, *badge, *num, *cond, *bar, *tick, *led, *target, *sw;
    bool wasActive;
    uint32_t fired;
};
RCard gR[hal::RULES_MAX];

// ---- แถบไฟ ----
lv_obj_t *gArc, *gArcPct, *gArcSub, *gArcLit, *gSrcBtn[4], *gModeBtn[3], *gBarSw, *gSeg[8], *gSegLbl[8];
int gArcTarget = -1;

// ---- ปุ่มเลือก ----
lv_obj_t *gInRow[4], *gInRing[4], *gInDot[4], *gInBtn[4], *gInName[4], *gSelBig, *gSelVal, *gSelInfo, *gHoldPill, *gInSw;
uint32_t gInSeq = 0;
int gSelShown = -1;

// ---- หน้าแก้กฎ ----
lv_obj_t *gEdOv = nullptr, *gEdTitle, *gEdSlot, *gEdAbove, *gEdBelow, *gEdThr, *gEdHyst, *gEdHold, *gEdPin, *gEdAct[4], *gEdSlider,
         *gEdBand, *gEdMark, *gEdLive, *gEdLo, *gEdHi;
lv_timer_t *gEdTimer = nullptr;
int gEdIdx = 0;
hal::Rule gEd;

void fmtUnit(char *buf, size_t n, int slot, float v)
{
    char num[16];
    ui_fmt_value(num, sizeof(num), slot, v);
    snprintf(buf, n, "%s %s", num, ui_slot_unit(slot));
}

float fracOf(int slot, float v)
{
    float lo, hi;
    hal::adsRange(static_cast<uint8_t>(slot), lo, hi);
    float f = hi > lo ? (v - lo) / (hi - lo) : 0;
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

bool slotLive(int slot)
{
    const int first = hal::adsFirstSlot();
    return slot >= first && slot < first + hal::adsSlotCount() && (hal::ads().found || hal::ads().demo);
}

void onSub(lv_event_t *e)
{
    ui_rules_show(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

lv_obj_t *plainRow(lv_obj_t *parent, int gap)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

lv_obj_t *smallBtn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, int user, int minW = 0)
{
    lv_obj_t *b = ui_btn(parent, text, cb, reinterpret_cast<void *>(static_cast<uintptr_t>(user)));
    lv_obj_set_style_min_height(b, 30, 0);
    lv_obj_set_style_pad_ver(b, 2, 0);
    lv_obj_set_style_pad_hor(b, 4, 0);
    if (minW) lv_obj_set_style_min_width(b, minW, 0);
    return b;
}

void setChecked(lv_obj_t *o, bool on)
{
    if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
    else lv_obj_clear_state(o, LV_STATE_CHECKED);
}

// =====================================================================
//  รายการกฎ
// =====================================================================
void onCard(lv_event_t *e)
{
    ui_rule_edit(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

void onRuleSw(lv_event_t *e)
{
    const int i = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    hal::Rule r = hal::rulesCfg().rule[i];
    r.en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    hal::rulesSetRule(static_cast<uint8_t>(i), r);
}

void onDefaults(lv_event_t *)
{
    hal::rulesResetDefaults();
    hal::soundPlay(hal::Melody::Success);
    for (RCard &c : gR) c.fired = 0;
}

void refreshCard(int i)
{
    RCard &c = gR[i];
    const hal::Rule &r = hal::rulesCfg().rule[i];
    const hal::RulesState &st = hal::rules();
    char buf[64];
    ui_rule_text(buf, sizeof(buf), i, false);
    ui_set_text(c.cond, buf);
    snprintf(buf, sizeof(buf), "P%u %s", r.pin, hal::RULE_ACT_NAMES[r.act < 4 ? r.act : 0]);
    ui_set_text(c.target, buf);
    setChecked(c.sw, r.en);
    lv_obj_set_style_opa(c.box, r.en ? LV_OPA_COVER : LV_OPA_50, 0);

    const bool live = r.en && slotLive(r.slot);
    lv_obj_set_style_bg_color(c.bar, lv_color_hex(ui_slot_color(r.slot)), LV_PART_INDICATOR);
    ui_bar_anim(c.bar, live ? static_cast<int>(fracOf(r.slot, hal::adsSlot(r.slot).value) * 1000) : 0);
    lv_obj_set_x(c.tick, static_cast<lv_coord_t>(fracOf(r.slot, r.thr) * (lv_obj_get_width(c.bar) - 2)));

    const bool act = st.active[i];
    lv_obj_set_style_border_color(c.box, lv_color_hex(act ? UI_OUT : UI_CARD), 0);
    lv_obj_set_style_bg_color(c.badge, lv_color_hex(act ? UI_OUT : UI_CARD2), 0);
    lv_obj_set_style_text_color(c.num, lv_color_hex(act ? UI_BG : UI_TEXT), 0);
    const bool pinOn = hal::pcf().found && !hal::pcfIsInput(r.pin) && hal::pcfActive(r.pin);
    lv_obj_set_style_bg_color(c.led, lv_color_hex(pinOn ? UI_OUT : UI_LINE), 0);
    if (act != c.wasActive) {
        if (act) ui_flash(c.box, 0x7A5410, UI_OUT_BG, 600);
        else ui_flash(c.box, UI_OUT_BG, UI_CARD, 250);
        c.wasActive = act;
    }
    if (st.fired[i] != c.fired) {
        if (c.fired != 0 || st.fired[i] == 1) ui_pop(c.badge);
        c.fired = st.fired[i];
    }
}

void buildList(lv_obj_t *box)
{
    for (int i = 0; i < hal::RULES_MAX; i++) {
        RCard &c = gR[i];
        c.box = ui_box(box, UI_CARD, 7);
        lv_obj_set_size(c.box, LV_PCT(100), 40);
        lv_obj_set_style_border_width(c.box, 2, 0);
        lv_obj_set_style_border_color(c.box, lv_color_hex(UI_CARD), 0);
        lv_obj_set_style_pad_hor(c.box, 5, 0);
        lv_obj_set_flex_flow(c.box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(c.box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(c.box, 5, 0);
        lv_obj_add_flag(c.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c.box, onCard, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

        c.badge = ui_box(c.box, UI_CARD2, LV_RADIUS_CIRCLE);
        lv_obj_set_size(c.badge, 20, 20);
        char n[4];
        snprintf(n, sizeof(n), "%d", i + 1);
        c.num = ui_label(c.badge, n, &lv_font_montserrat_14, lv_color_hex(UI_TEXT));
        lv_obj_center(c.num);

        lv_obj_t *mid = lv_obj_create(c.box);
        lv_obj_remove_style_all(mid);
        lv_obj_set_size(mid, 116, 34);
        lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);
        c.cond = ui_label(mid, "", nullptr, lv_color_hex(UI_TEXT));
        lv_label_set_long_mode(c.cond, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(c.cond, 116);
        lv_obj_align(c.cond, LV_ALIGN_TOP_LEFT, 0, -3);
        c.bar = lv_bar_create(mid);
        lv_obj_set_size(c.bar, 110, 5);
        lv_obj_align(c.bar, LV_ALIGN_BOTTOM_LEFT, 0, -2);
        lv_bar_set_range(c.bar, 0, 1000);
        lv_obj_set_style_bg_color(c.bar, lv_color_hex(UI_LINE), LV_PART_MAIN);
        c.tick = ui_box(mid, UI_OUT, 1);
        lv_obj_set_size(c.tick, 2, 11);
        lv_obj_align(c.tick, LV_ALIGN_BOTTOM_LEFT, 0, 1);

        lv_obj_t *arrow = ui_label(c.box, "→", nullptr, lv_color_hex(UI_MUTED));
        c.led = ui_box(c.box, UI_LINE, LV_RADIUS_CIRCLE);
        lv_obj_set_size(c.led, 14, 14);
        c.target = ui_label(c.box, "", nullptr, lv_color_hex(UI_TEXT));
        lv_label_set_long_mode(c.target, LV_LABEL_LONG_CLIP);
        lv_obj_set_flex_grow(c.target, 1);
        c.sw = lv_switch_create(c.box);
        lv_obj_set_size(c.sw, 40, 22);
        lv_obj_add_event_cb(c.sw, onRuleSw, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        for (lv_obj_t *o : {c.badge, c.num, mid, c.cond, c.bar, c.tick, arrow, c.led, c.target}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        // เริ่มจากสถานะปัจจุบัน: เปิดหน้านี้ใหม่แล้วไม่กะพริบย้อนหลัง
        c.wasActive = hal::rules().active[i];
        c.fired = hal::rules().fired[i];
        if (c.wasActive) lv_obj_set_style_bg_color(c.box, lv_color_hex(UI_OUT_BG), 0);
    }
    lv_obj_t *row = plainRow(box, 6);
    ui_label(row, "แตะการ์ด = แก้กฎ", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_t *sp = lv_obj_create(row);
    lv_obj_remove_style_all(sp);
    lv_obj_set_height(sp, 1);  // ตัวดันว่าง: ค่าเริ่มต้นของ lv_obj สูง ~130 px
    lv_obj_set_flex_grow(sp, 1);
    ui_btn(row, LV_SYMBOL_REFRESH " ค่าเริ่มต้น (กดค้าง)", onDefaults, nullptr, LV_EVENT_LONG_PRESSED);
}

// =====================================================================
//  แถบไฟ
// =====================================================================
void arcExec(void *obj, int32_t v)
{
    lv_arc_set_value(static_cast<lv_obj_t *>(obj), static_cast<int16_t>(v));
}

void onSrc(lv_event_t *e)
{
    const int i = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    hal::rulesSelect(static_cast<uint8_t>(hal::adsFirstSlot() + i));
}

void onMode(lv_event_t *e)
{
    const int m = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    hal::rulesSetBar(hal::rulesCfg().barOn, static_cast<uint8_t>(m));
}

void onBarSw(lv_event_t *e)
{
    hal::rulesSetBar(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED), hal::rulesCfg().barMode);
}

void refreshBar()
{
    const hal::RulesCfg &cfg = hal::rulesCfg();
    const hal::RulesState &st = hal::rules();
    const int sel = cfg.sel, first = hal::adsFirstSlot(), count = hal::adsSlotCount();
    for (int i = 0; i < 4; i++) {
        if (i >= count) {
            lv_obj_add_flag(gSrcBtn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(gSrcBtn[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(gSrcBtn[i], 0), hal::ADS_SLOT_NAMES[first + i]);
        setChecked(gSrcBtn[i], sel == first + i);
    }
    for (int m = 0; m < 3; m++) setChecked(gModeBtn[m], cfg.barMode == m);
    setChecked(gBarSw, cfg.barOn);

    // เกจ: เลื่อนนุ่มด้วย lv_anim
    const int target = static_cast<int>(st.barFrac * 1000);
    if (abs(target - gArcTarget) > 4) {
        gArcTarget = target;
        lv_anim_del(gArc, arcExec);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, gArc);
        lv_anim_set_exec_cb(&a, arcExec);
        lv_anim_set_values(&a, lv_arc_get_value(gArc), target);
        lv_anim_set_time(&a, 200);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    lv_obj_set_style_arc_color(gArc, lv_color_hex(ui_slot_color(sel)), LV_PART_INDICATOR);
    char buf[40];
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(st.barFrac * 100 + 0.5f));
    ui_set_text(gArcPct, buf);
    char v[24];
    fmtUnit(v, sizeof(v), sel, st.hold ? st.heldValue : hal::adsSlot(static_cast<uint8_t>(sel)).value);
    snprintf(buf, sizeof(buf), "%s %s", hal::ADS_SLOT_NAMES[sel], v);
    ui_set_text(gArcSub, buf);
    snprintf(buf, sizeof(buf), "%u/%u ดวง", st.barLit, st.barTotal);
    lv_obj_set_style_text_color(gArcLit, lv_color_hex(cfg.barOn ? UI_OUT : UI_MUTED), 0);
    ui_set_text(gArcLit, buf);

    // ไฟ 8 ดวง: เปิดแถบไฟ = ขาจริง / ปิด = ตัวอย่างจางๆ
    int k = 0;
    const int n = st.barTotal;
    for (int pin = 0; pin < 8; pin++) {
        lv_obj_t *s = gSeg[pin];
        if (hal::pcfIsInput(pin)) {
            lv_obj_set_style_bg_color(s, lv_color_hex(UI_BG), 0);
            lv_obj_set_style_border_width(s, 1, 0);
            lv_obj_set_style_text_color(gSegLbl[pin], lv_color_hex(UI_LINE), 0);
            continue;
        }
        bool on;
        if (cfg.barOn) {
            on = hal::pcfActive(pin) && hal::pcf().found;
        } else if (cfg.barMode == static_cast<uint8_t>(hal::BarMode::Dot)) {
            on = n > 0 && k == static_cast<int>(st.barFrac * (n - 1) + 0.5f);
        } else {
            on = k < st.barLit || (cfg.barMode == static_cast<uint8_t>(hal::BarMode::Vu) && k == st.barPeak - 1);
        }
        const uint32_t col = ui_mix(UI_OUT, UI_BAD, static_cast<uint8_t>(n > 1 ? k * 255 / (n - 1) : 0));
        lv_obj_set_style_border_width(s, 0, 0);
        lv_obj_set_style_bg_color(s, lv_color_hex(on ? (cfg.barOn ? col : ui_mix(col, UI_CARD, 140)) : UI_LINE), 0);
        lv_obj_set_style_text_color(gSegLbl[pin], lv_color_hex(on ? UI_BG : UI_MUTED), 0);
        k++;
    }
}

void buildBar(lv_obj_t *box)
{
    lv_obj_t *top = plainRow(box, 6);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *g = ui_box(top, UI_CARD, 8);
    lv_obj_set_size(g, 114, 104);
    gArc = lv_arc_create(g);
    lv_obj_set_size(gArc, 96, 96);
    lv_obj_align(gArc, LV_ALIGN_TOP_MID, 0, 4);
    lv_arc_set_bg_angles(gArc, 135, 45);
    lv_arc_set_range(gArc, 0, 1000);
    lv_arc_set_value(gArc, 0);
    lv_obj_remove_style(gArc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(gArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(gArc, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gArc, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gArc, lv_color_hex(UI_LINE), LV_PART_MAIN);
    gArcPct = ui_label(g, "0%", &font_th_24, lv_color_hex(UI_TEXT));
    lv_obj_align(gArcPct, LV_ALIGN_TOP_MID, 0, 28);
    gArcSub = ui_label(g, "", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    lv_obj_set_style_text_font(gArcSub, &font_th_16, 0);
    lv_obj_align(gArcSub, LV_ALIGN_TOP_MID, 0, 56);
    gArcLit = ui_label(g, "", nullptr, lv_color_hex(UI_OUT));
    lv_obj_align(gArcLit, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *col = lv_obj_create(top);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *r = plainRow(col, 3);
    lv_obj_t *l = ui_label(r, "แหล่ง", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(l, 42);
    for (int i = 0; i < 4; i++) {
        gSrcBtn[i] = smallBtn(r, "A0", onSrc, i);
        lv_obj_set_flex_grow(gSrcBtn[i], 1);
    }
    r = plainRow(col, 3);
    l = ui_label(r, "แบบ", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(l, 42);
    const char *modes[3] = {"แถบ", "จุด", "VU"};
    for (int m = 0; m < 3; m++) {
        gModeBtn[m] = smallBtn(r, modes[m], onMode, m);
        lv_obj_set_flex_grow(gModeBtn[m], 1);
    }
    lv_obj_t *swCard = ui_box(col, UI_CARD, 7);
    lv_obj_set_size(swCard, LV_PCT(100), 32);
    lv_obj_set_style_pad_hor(swCard, 6, 0);
    gBarSw = ui_switch_row(swCard, "เปิดแถบไฟ", false, onBarSw);
    lv_obj_center(lv_obj_get_parent(gBarSw));

    lv_obj_t *segs = ui_box(box, UI_CARD, 8);
    lv_obj_set_size(segs, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(segs, 5, 0);
    lv_obj_set_flex_flow(segs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(segs, 4, 0);
    for (int pin = 0; pin < 8; pin++) {
        gSeg[pin] = ui_box(segs, UI_LINE, 5);
        lv_obj_set_height(gSeg[pin], 30);
        lv_obj_set_flex_grow(gSeg[pin], 1);
        lv_obj_set_style_border_color(gSeg[pin], lv_color_hex(UI_LINE), 0);
        char n[4];
        snprintf(n, sizeof(n), "P%d", pin);
        gSegLbl[pin] = ui_label(gSeg[pin], n, &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
        lv_obj_center(gSegLbl[pin]);
    }
    lv_obj_t *note = ui_label(box, "ใช้เฉพาะขาเอาต์พุต (ขาอินพุตเป็นกรอบว่าง) แถบไฟชนะกฎที่ใช้ขาเดียวกัน", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(note, LV_PCT(100));
}

// =====================================================================
//  ปุ่มเลือก
// =====================================================================
void onInAct(lv_event_t *e)
{
    const int i = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    uint8_t act[4];
    memcpy(act, hal::rulesCfg().inAct, 4);
    act[i] = static_cast<uint8_t>((act[i] + 1) % static_cast<uint8_t>(hal::InAct::Count));
    hal::rulesSetInput(hal::rulesCfg().inOn, act);
}

void onInSw(lv_event_t *e)
{
    hal::rulesSetInput(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED), hal::rulesCfg().inAct);
}

void refreshIn()
{
    const hal::RulesCfg &cfg = hal::rulesCfg();
    const hal::RulesState &st = hal::rules();
    const uint8_t pressed = hal::pcfInputActiveMask();
    for (int i = 0; i < 4; i++) {
        const int pin = 4 + i;
        const bool in = hal::pcfIsInput(pin);
        const bool on = in && (pressed >> pin) & 1;
        lv_obj_set_style_border_color(gInRing[i], lv_color_hex(on ? UI_ACCENT : 0x3B4A69), 0);
        if (on) lv_obj_clear_flag(gInDot[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(gInDot[i], LV_OBJ_FLAG_HIDDEN);
        char buf[16];
        snprintf(buf, sizeof(buf), in ? "P%d" : "P%d ขาออก", pin);
        ui_set_text(gInName[i], buf);
        lv_obj_set_style_text_color(gInName[i], lv_color_hex(in ? UI_TEXT : UI_MUTED), 0);
        ui_set_text(lv_obj_get_child(gInBtn[i], 0), hal::IN_ACT_NAMES[cfg.inAct[i] < 6 ? cfg.inAct[i] : 0]);
    }
    setChecked(gInSw, cfg.inOn);
    if (st.inSeq != gInSeq) {
        if (gInSeq != 0 || st.inSeq == 1) {
            const int idx = st.inLast >= 4 ? st.inLast - 4 : 0;
            ui_flash(gInRow[idx], 0x17506E, UI_CARD, 500);
        }
        gInSeq = st.inSeq;
    }

    const int sel = cfg.sel;
    if (sel != gSelShown) {
        lv_label_set_text(gSelBig, hal::ADS_SLOT_NAMES[sel]);
        lv_obj_set_style_text_color(gSelBig, lv_color_hex(ui_slot_color(sel)), 0);
        if (gSelShown >= 0) ui_slide_in(gSelBig, 22, 260);
        gSelShown = sel;
    }
    char v[24];
    fmtUnit(v, sizeof(v), sel, st.hold ? st.heldValue : hal::adsSlot(static_cast<uint8_t>(sel)).value);
    ui_set_text(gSelVal, slotLive(sel) || st.hold ? v : "--");
    char info[40];
    snprintf(info, sizeof(info), "±%.3f V\n%u SPS", hal::ADS_FSR[hal::adsCfg().gain], hal::adsSps());
    ui_set_text(gSelInfo, info);
    if (st.hold) lv_obj_clear_flag(gHoldPill, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(gHoldPill, LV_OBJ_FLAG_HIDDEN);
}

void buildIn(lv_obj_t *box)
{
    lv_obj_t *top = plainRow(box, 6);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *col = lv_obj_create(top);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 3, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *r = ui_box(col, UI_CARD, 6);
        lv_obj_set_size(r, LV_PCT(100), 34);
        lv_obj_set_style_pad_hor(r, 6, 0);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 6, 0);
        gInRow[i] = r;
        gInRing[i] = ui_box(r, UI_CARD, LV_RADIUS_CIRCLE);
        lv_obj_set_size(gInRing[i], 14, 14);
        lv_obj_set_style_bg_opa(gInRing[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(gInRing[i], 2, 0);
        gInDot[i] = ui_box(gInRing[i], UI_ACCENT, LV_RADIUS_CIRCLE);
        lv_obj_set_size(gInDot[i], 6, 6);
        lv_obj_center(gInDot[i]);
        gInName[i] = ui_label(r, "P4", nullptr, lv_color_hex(UI_TEXT));
        lv_obj_set_flex_grow(gInName[i], 1);
        gInBtn[i] = smallBtn(r, "", onInAct, i, 86);
        lv_obj_set_style_min_height(gInBtn[i], 26, 0);
    }

    lv_obj_t *card = ui_box(top, UI_CARD, 8);
    lv_obj_set_size(card, 108, 145);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_t *t = ui_label(card, "ช่องที่แสดง", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, -2);
    gSelBig = ui_label(card, "A0", &font_th_24, lv_color_hex(UI_ACCENT));
    lv_obj_align(gSelBig, LV_ALIGN_TOP_MID, 0, 24);
    gSelVal = ui_label(card, "--", nullptr, lv_color_hex(UI_TEXT));
    lv_obj_align(gSelVal, LV_ALIGN_TOP_MID, 0, 58);
    gSelInfo = ui_label(card, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_style_text_align(gSelInfo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(gSelInfo, -4, 0);
    lv_obj_align(gSelInfo, LV_ALIGN_TOP_MID, 0, 78);
    gHoldPill = ui_box(card, UI_OUT, 8);
    lv_obj_set_size(gHoldPill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(gHoldPill, 8, 0);
    ui_label(gHoldPill, LV_SYMBOL_PAUSE " ค้างค่า", nullptr, lv_color_hex(UI_BG));
    lv_obj_align(gHoldPill, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *swCard = ui_box(box, UI_CARD, 7);
    lv_obj_set_size(swCard, LV_PCT(100), 34);
    lv_obj_set_style_pad_hor(swCard, 8, 0);
    gInSw = ui_switch_row(swCard, "ให้ปุ่ม P4-P7 คุม ADS", false, onInSw);
    lv_obj_center(lv_obj_get_parent(gInSw));
    lv_obj_t *note = ui_label(box, "แตะชื่อคำสั่งเพื่อเปลี่ยน | ขาต้องตั้งเป็นอินพุตในแท็บ I/O", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(note, LV_PCT(100));
}

// =====================================================================
//  หน้าแก้กฎ (เต็มจอบน layer_top เลื่อนขึ้นมาจากล่าง)
// =====================================================================
void edClose()
{
    if (gEdOv == nullptr) return;
    if (gEdTimer != nullptr) lv_timer_del(gEdTimer);
    gEdTimer = nullptr;
    lv_obj_del_async(gEdOv);  // เรียกจาก event ของปุ่มบนหน้านี้: ห้ามลบทันที
    gEdOv = nullptr;
}

void edRefresh()
{
    if (gEdOv == nullptr) return;
    char buf[48], v[24];
    snprintf(buf, sizeof(buf), "แก้กฎ %d", gEdIdx + 1);
    ui_set_text(gEdTitle, buf);
    ui_set_text(lv_obj_get_child(gEdSlot, 0), hal::ADS_SLOT_NAMES[gEd.slot]);
    setChecked(gEdAbove, gEd.above);
    setChecked(gEdBelow, !gEd.above);
    fmtUnit(v, sizeof(v), gEd.slot, gEd.thr);
    ui_set_text(lv_obj_get_child(gEdThr, 0), v);
    char num[16];
    ui_fmt_value(num, sizeof(num), gEd.slot, gEd.hyst);
    snprintf(buf, sizeof(buf), "หน่วงกลับ %s", num);
    ui_set_text(lv_obj_get_child(gEdHyst, 0), buf);
    snprintf(buf, sizeof(buf), "ค้าง %u ms", gEd.holdMs);
    ui_set_text(lv_obj_get_child(gEdHold, 0), buf);
    snprintf(buf, sizeof(buf), "P%u", gEd.pin);
    ui_set_text(lv_obj_get_child(gEdPin, 0), buf);
    for (int a = 0; a < 4; a++) setChecked(gEdAct[a], gEd.act == a);

    float lo, hi;
    hal::adsRange(gEd.slot, lo, hi);
    fmtUnit(v, sizeof(v), gEd.slot, lo);
    ui_set_text(gEdLo, v);
    fmtUnit(v, sizeof(v), gEd.slot, hi);
    ui_set_text(gEdHi, v);
    if (!lv_obj_has_state(gEdSlider, LV_STATE_PRESSED)) lv_slider_set_value(gEdSlider, static_cast<int>(fracOf(gEd.slot, gEd.thr) * 1000), LV_ANIM_ON);
    // ช่วงหน่วงกลับ (ลาย) อยู่ฝั่งที่ "เลิกทำงาน" ของขีด
    const int w = lv_obj_get_width(gEdSlider);
    const float f1 = fracOf(gEd.slot, gEd.thr), f2 = fracOf(gEd.slot, gEd.above ? gEd.thr - gEd.hyst : gEd.thr + gEd.hyst);
    const int x1 = static_cast<int>((f1 < f2 ? f1 : f2) * w), x2 = static_cast<int>((f1 < f2 ? f2 : f1) * w);
    lv_obj_set_x(gEdBand, static_cast<lv_coord_t>(lv_obj_get_x(gEdSlider) + x1));
    lv_obj_set_width(gEdBand, static_cast<lv_coord_t>(x2 - x1 < 2 ? 2 : x2 - x1));
}

void edLive(lv_timer_t *)
{
    if (gEdOv == nullptr) return;
    char buf[40], v[24];
    const bool live = slotLive(gEd.slot);
    const float val = hal::adsSlot(gEd.slot).value;
    fmtUnit(v, sizeof(v), gEd.slot, val);
    snprintf(buf, sizeof(buf), "ค่าสด %s", live ? v : "--");
    ui_set_text(gEdLive, buf);
    if (live) {
        lv_obj_clear_flag(gEdMark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(gEdMark, static_cast<lv_coord_t>(lv_obj_get_x(gEdSlider) + fracOf(gEd.slot, val) * lv_obj_get_width(gEdSlider) - 5));
        const bool trig = gEd.above ? val > gEd.thr : val < gEd.thr;
        lv_obj_set_style_bg_color(gEdMark, lv_color_hex(trig ? UI_OUT : UI_ACCENT), 0);
    } else {
        lv_obj_add_flag(gEdMark, LV_OBJ_FLAG_HIDDEN);
    }
}

void edThrDone(float v)
{
    gEd.thr = v;
    edRefresh();
}

void edHystDone(float v)
{
    gEd.hyst = v < 0 ? -v : v;
    edRefresh();
}

void edHoldDone(float v)
{
    gEd.holdMs = static_cast<uint16_t>(v < 0 ? 0 : v > 60000 ? 60000 : v);
    edRefresh();
}

void onEdBtn(lv_event_t *e)
{
    const int id = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    switch (id) {
        case 0: {  // ช่อง: วนตามโหมดเดี่ยว/ต่างตอนนี้
            gEd.slot = hal::rulesNextSlot(gEd.slot);
            break;
        }
        case 1: gEd.above = true; break;
        case 2: gEd.above = false; break;
        case 3: ui_keypad_open("ขีด (หน่วยของช่อง)", gEd.thr, edThrDone); return;
        case 4: ui_keypad_open("หน่วงกลับ (ฮิสเทอรีซิส)", gEd.hyst, edHystDone); return;
        case 5: ui_keypad_open("ค้างกี่ ms ก่อนเปลี่ยน", gEd.holdMs, edHoldDone); return;
        case 6: {  // ขา: วนเฉพาะขาเอาต์พุต
            for (int k = 1; k <= 8; k++) {
                const uint8_t pin = static_cast<uint8_t>((gEd.pin + k) & 7);
                if (!hal::pcfIsInput(pin)) {
                    gEd.pin = pin;
                    break;
                }
            }
            break;
        }
        case 20: edClose(); return;  // ปิด / ยกเลิก
        case 21:                     // บันทึก (บันทึก = เปิดใช้)
            gEd.en = true;
            hal::rulesSetRule(static_cast<uint8_t>(gEdIdx), gEd);
            hal::soundPlay(hal::Melody::Success);
            edClose();
            return;
        case 22:  // ปิดกฎ
            gEd.en = false;
            hal::rulesSetRule(static_cast<uint8_t>(gEdIdx), gEd);
            edClose();
            return;
        default:
            if (id >= 10 && id < 14) gEd.act = static_cast<uint8_t>(id - 10);
            break;
    }
    edRefresh();
}

void onEdSlider(lv_event_t *e)
{
    float lo, hi;
    hal::adsRange(gEd.slot, lo, hi);
    const float f = lv_slider_get_value(lv_event_get_target(e)) / 1000.0f;
    // ปัดตามทศนิยมของช่อง ให้ตัวเลขอ่านง่าย
    const float step = hal::adsDecimals(gEd.slot) >= 3 ? 0.01f : hal::adsDecimals(gEd.slot) == 2 ? 0.1f : 1.0f;
    gEd.thr = roundf((lo + f * (hi - lo)) / step) * step;
    edRefresh();
}

lv_obj_t *edRow(lv_obj_t *parent, const char *label)
{
    lv_obj_t *r = plainRow(parent, 4);
    lv_obj_t *l = ui_label(r, label, nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(l, 40);
    return r;
}

lv_obj_t *edBtn(lv_obj_t *row, const char *text, int id, bool grow)
{
    lv_obj_t *b = smallBtn(row, text, onEdBtn, id);
    lv_obj_set_style_min_height(b, 30, 0);
    if (grow) lv_obj_set_flex_grow(b, 1);
    return b;
}

}  // namespace

// =====================================================================

void ui_rule_text(char *buf, size_t n, int index, bool withTarget)
{
    const hal::Rule &r = hal::rulesCfg().rule[index];
    char v[24];
    fmtUnit(v, sizeof(v), r.slot, r.thr);
    if (withTarget) snprintf(buf, n, "%s %s %s → P%u %s", hal::ADS_SLOT_NAMES[r.slot], r.above ? ">" : "<", v, r.pin, hal::RULE_ACT_NAMES[r.act < 4 ? r.act : 0]);
    else snprintf(buf, n, "%s %s %s", hal::ADS_SLOT_NAMES[r.slot], r.above ? ">" : "<", v);
}

void ui_rule_edit(int index)
{
    if (index < 0 || index >= hal::RULES_MAX || gEdOv != nullptr) return;
    gEdIdx = index;
    gEd = hal::rulesCfg().rule[index];
    if (!gEd.en && hal::pcfIsInput(gEd.pin)) gEd.pin = 0;

    gEdOv = ui_overlay();
    lv_obj_set_style_bg_color(gEdOv, lv_color_hex(UI_BG), 0);

    lv_obj_t *head = ui_box(gEdOv, UI_TABBAR, 0);
    lv_obj_set_size(head, UI_W, 32);
    lv_obj_set_style_pad_hor(head, 8, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 6, 0);
    gEdTitle = ui_label(head, "", nullptr, lv_color_hex(UI_OUT));
    ui_label(head, "ตัวเปรียบเทียบเสมือน", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_t *sp = lv_obj_create(head);
    lv_obj_remove_style_all(sp);
    lv_obj_set_height(sp, 1);  // ตัวดันว่าง: ค่าเริ่มต้นของ lv_obj สูง ~130 px
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_t *x = ui_btn(head, LV_SYMBOL_CLOSE, onEdBtn, reinterpret_cast<void *>(static_cast<uintptr_t>(20)));
    lv_obj_set_style_min_height(x, 28, 0);
    lv_obj_set_style_pad_ver(x, 2, 0);
    lv_obj_set_style_bg_opa(x, LV_OPA_TRANSP, 0);

    lv_obj_t *body = lv_obj_create(gEdOv);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, UI_W, UI_H - 32);
    lv_obj_set_pos(body, 0, 32);
    lv_obj_set_style_pad_hor(body, 6, 0);
    lv_obj_set_style_pad_ver(body, 4, 0);
    lv_obj_set_style_pad_row(body, 4, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *r = edRow(body, "ถ้า");
    gEdSlot = edBtn(r, "A0", 0, false);
    lv_obj_set_style_min_width(gEdSlot, 62, 0);
    gEdAbove = edBtn(r, ">", 1, false);
    lv_obj_set_style_min_width(gEdAbove, 36, 0);
    gEdBelow = edBtn(r, "<", 2, false);
    lv_obj_set_style_min_width(gEdBelow, 36, 0);
    gEdThr = edBtn(r, "", 3, true);
    lv_obj_set_style_bg_color(gEdThr, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_border_width(gEdThr, 2, 0);
    lv_obj_set_style_border_color(gEdThr, lv_color_hex(UI_OUT), 0);

    r = edRow(body, "หน่วง");
    gEdHyst = edBtn(r, "", 4, true);
    gEdHold = edBtn(r, "", 5, true);

    r = edRow(body, "แล้ว");
    gEdPin = edBtn(r, "P0", 6, false);
    lv_obj_set_style_min_width(gEdPin, 40, 0);
    for (int a = 0; a < 4; a++) {
        gEdAct[a] = edBtn(r, hal::RULE_ACT_NAMES[a], 10 + a, true);
        lv_obj_set_style_bg_color(gEdAct[a], lv_color_hex(UI_OUT), LV_STATE_CHECKED);
    }

    // ---- ตัวอย่าง: แถบค่าสด + เส้นขีด (ลากได้) + ช่วงหน่วงกลับ ----
    lv_obj_t *pv = ui_box(body, UI_CARD, 8);
    lv_obj_set_size(pv, LV_PCT(100), 46);
    lv_obj_set_style_pad_hor(pv, 10, 0);
    lv_obj_clear_flag(pv, LV_OBJ_FLAG_CLICKABLE);
    gEdLo = ui_label(pv, "", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    lv_obj_set_style_text_font(gEdLo, &font_th_16, 0);
    lv_obj_align(gEdLo, LV_ALIGN_TOP_LEFT, -4, -2);
    gEdHi = ui_label(pv, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(gEdHi, LV_ALIGN_TOP_RIGHT, 4, -2);
    gEdLive = ui_label(pv, "", nullptr, lv_color_hex(UI_ACCENT));
    lv_obj_align(gEdLive, LV_ALIGN_TOP_MID, 0, -2);
    gEdSlider = lv_slider_create(pv);
    lv_obj_set_size(gEdSlider, 270, 8);
    lv_obj_align(gEdSlider, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(gEdSlider, 0, 1000);
    lv_obj_set_style_bg_color(gEdSlider, lv_color_hex(UI_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gEdSlider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(gEdSlider, lv_color_hex(UI_OUT), LV_PART_KNOB);
    lv_obj_set_style_radius(gEdSlider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(gEdSlider, -2, LV_PART_KNOB);  // ปุ่มลากเป็นเส้นตั้งแคบๆ
    lv_obj_set_style_pad_ver(gEdSlider, 5, LV_PART_KNOB);
    lv_obj_set_ext_click_area(gEdSlider, 12);
    lv_obj_add_event_cb(gEdSlider, onEdSlider, LV_EVENT_VALUE_CHANGED, nullptr);
    gEdBand = ui_box(pv, UI_OUT, 2);
    lv_obj_set_height(gEdBand, 8);
    lv_obj_align_to(gEdBand, gEdSlider, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(gEdBand, LV_OPA_40, 0);
    lv_obj_clear_flag(gEdBand, LV_OBJ_FLAG_CLICKABLE);
    gEdMark = ui_box(pv, UI_ACCENT, LV_RADIUS_CIRCLE);
    lv_obj_set_size(gEdMark, 10, 10);
    lv_obj_align_to(gEdMark, gEdSlider, LV_ALIGN_LEFT_MID, 0, -11);
    lv_obj_clear_flag(gEdMark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(gEdSlider);
    lv_obj_update_layout(gEdOv);

    r = plainRow(body, 6);
    lv_obj_t *off = edBtn(r, "ปิดกฎ", 22, false);
    lv_obj_set_style_text_color(off, lv_color_hex(UI_BAD), 0);
    sp = lv_obj_create(r);
    lv_obj_remove_style_all(sp);
    lv_obj_set_height(sp, 1);  // ตัวดันว่าง: ค่าเริ่มต้นของ lv_obj สูง ~130 px
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_t *cancel = edBtn(r, "ยกเลิก", 20, false);
    lv_obj_set_style_min_width(cancel, 70, 0);
    lv_obj_t *ok = edBtn(r, LV_SYMBOL_OK " บันทึก", 21, false);
    lv_obj_set_style_min_width(ok, 90, 0);
    lv_obj_set_style_bg_color(ok, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_text_color(ok, lv_color_hex(UI_BG), 0);

    edRefresh();
    edLive(nullptr);
    gEdTimer = lv_timer_create(edLive, 100, nullptr);
    ui_slide_in(gEdOv, UI_H, 240);
    lv_indev_t *indev = lv_indev_get_act();
    if (indev != nullptr) lv_indev_wait_release(indev);  // นิ้วที่แตะการ์ดอยู่ ไม่ให้ไปกดปุ่มบนหน้านี้ต่อ
    Serial.printf("[UI] rule editor %d\n", index + 1);
}

namespace {
int gBuilt = -1;  // ส่วนย่อยที่สร้างอยู่ตอนนี้ (สร้างเมื่อเปิดดู / ลบเมื่อออก เพื่อประหยัด RAM)
}  // namespace

void ui_rules_show(int index)
{
    if (index < 0 || index >= SUB_COUNT) return;
    if (gBuilt != index) {
        if (gBuilt >= 0) {
            // ลบแบบ async: อาจถูกเรียกจาก event ของ widget ในส่วนเก่า
            for (int k = static_cast<int>(lv_obj_get_child_cnt(gSub[gBuilt])) - 1; k >= 0; k--) {
                lv_obj_t *c = lv_obj_get_child(gSub[gBuilt], k);
                lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
                lv_obj_del_async(c);
            }
        }
        gBuilt = -1;  // ระหว่างสร้าง tick ห้ามแตะ
        gArcTarget = -1;
        gSelShown = -1;
        void (*builders[SUB_COUNT])(lv_obj_t *) = {buildList, buildBar, buildIn};
        const uint32_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        builders[index](gSub[index]);
        gBuilt = index;
        Serial.printf("[UI] rules page %d built: %d bytes, heap8 %u KB\n", index, static_cast<int>(before - heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    }
    ui_show_sub(gSub, gSubBtn, SUB_COUNT, index);
}

namespace {
void tick(lv_timer_t *)
{
    if (gBuilt == 0) {
        for (int i = 0; i < hal::RULES_MAX; i++) refreshCard(i);
    } else if (gBuilt == 1) {
        refreshBar();
    } else if (gBuilt == 2) {
        refreshIn();
    }
}
}  // namespace

void ui_rules_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_row(tab, 4, 0);
    static const char *const names[SUB_COUNT] = {"กฎ", "แถบไฟ", "ปุ่มเลือก"};
    ui_subnav(tab, names, SUB_COUNT, gSubBtn, onSub);
    for (int i = 0; i < SUB_COUNT; i++) {
        lv_obj_t *box = lv_obj_create(tab);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, 4, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
        gSub[i] = box;
    }
    ui_rules_show(0);
    lv_timer_create(tick, app::UI_REFRESH_MS, nullptr);
    tick(nullptr);
}
