#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "รวม": แดชบอร์ดของทั้งสองชิปในหน้าเดียว
//    แถว 1  การ์ด A0-A3 (หรือคู่ต่าง) ค่า + แถบเลื่อนนุ่ม + ขีดส้ม = ขีดของกฎที่ใช้ช่องนี้   แตะ = ดูละเอียด
//    แถว 2  ไฟ P0-P7 ของ PCF8574 (เอาต์พุตที่ติดมีวงแสงกระเพื่อม)                         แตะไฟ = สลับ
//    แถว 3  บัสย่อ: จุดวิ่งจาก CYD ไป ADS / PCF + ธุรกรรมต่อวินาที                       แตะ = แท็บบัส
//    แถว 4  กฎที่ทำงานล่าสุด (กะพริบเมื่อมีกฎเริ่มทำงาน)                                    แตะ = แท็บกฎ
// =====================================================================

namespace {

struct ACard {
    lv_obj_t *box, *name, *value, *bar, *tick[2];
    int slot;
};
struct Led {
    lv_obj_t *col, *dot, *inner, *name;
};

ACard gCard[4];
Led gLed[8];
lv_obj_t *gPcfInfo, *gBusBox, *gDotAds, *gDotPcf, *gLblAds, *gLblPcf, *gRate;
lv_obj_t *gRuleBox, *gBolt, *gRuleText, *gRuleCount;
uint32_t gFiredSum = 0;
uint16_t gAnimAds = 0, gAnimPcf = 0;  // อัตราที่ใช้ตั้ง anim ครั้งล่าสุด (เปลี่ยนมากจึงตั้งใหม่)

constexpr int BUS_X0 = 18, BUS_ADS_X = 112, BUS_PCF_X = 196, BUS_Y = 22;

void onCard(lv_event_t *e)
{
    const ACard *c = static_cast<const ACard *>(lv_event_get_user_data(e));
    ui_detail_select(c->slot);
}

void onLed(lv_event_t *e)
{
    const uint8_t pin = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (hal::pcfIsInput(pin)) return;
    ui_pop(gLed[pin].dot);
    ui_pin_tap(pin);
}

void onBus(lv_event_t *)
{
    ui_goto_tab(UI_TAB_BUS);
}

void onRules(lv_event_t *)
{
    ui_goto_tab(UI_TAB_RULES);
    ui_rules_show(0);
}

void dotExec(void *obj, int32_t v)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
}

// จุดวิ่งบนบัส: ยิ่งธุรกรรมต่อวินาทีมาก ยิ่งวิ่งเร็ว (0 = ซ่อน)
void runDot(lv_obj_t *dot, int toX, uint16_t rate, uint16_t &last)
{
    if (rate == 0) {
        lv_anim_del(dot, dotExec);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        last = 0;
        return;
    }
    if (last != 0 && abs(static_cast<int>(rate) - static_cast<int>(last)) * 4 < last) return;  // เปลี่ยนไม่ถึง 25% ไม่ต้องตั้งใหม่
    last = rate;
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    lv_anim_del(dot, dotExec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, dotExec);
    lv_anim_set_values(&a, BUS_X0, toX);
    lv_anim_set_time(&a, rate > 150 ? 500 : rate > 40 ? 800 : 1300);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 60);
    lv_anim_start(&a);
}

void styleLed(int pin)
{
    const Led &l = gLed[pin];
    const bool in = hal::pcfIsInput(pin), on = hal::pcfActive(pin) && hal::pcf().found;
    if (in) {
        lv_obj_set_style_bg_opa(l.dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(l.dot, 2, 0);
        lv_obj_set_style_border_color(l.dot, lv_color_hex(on ? UI_ACCENT : 0x3B4A69), 0);
        if (on) lv_obj_clear_flag(l.inner, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(l.inner, LV_OBJ_FLAG_HIDDEN);
        ui_pulse(l.dot, false, 0);
    } else {
        lv_obj_set_style_bg_opa(l.dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(l.dot, 0, 0);
        lv_obj_set_style_bg_color(l.dot, lv_color_hex(on ? UI_OUT : UI_LINE), 0);
        lv_obj_add_flag(l.inner, LV_OBJ_FLAG_HIDDEN);
        ui_pulse(l.dot, on, UI_OUT);
    }
    lv_obj_set_style_text_color(l.name, lv_color_hex(on ? (in ? UI_ACCENT : UI_OUT) : UI_MUTED), 0);
}

void refreshCards()
{
    const int first = hal::adsFirstSlot(), count = hal::adsSlotCount();
    const bool ok = hal::ads().found || hal::ads().demo;
    char buf[40], num[16];
    for (int i = 0; i < 4; i++) {
        ACard &c = gCard[i];
        if (i >= count) {
            lv_obj_add_flag(c.box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(c.box, LV_OBJ_FLAG_HIDDEN);
        const int slot = first + i;
        if (c.slot != slot) {
            c.slot = slot;
            lv_label_set_text(c.name, hal::ADS_SLOT_NAMES[slot]);
            lv_obj_set_style_text_color(c.name, lv_color_hex(ui_slot_color(slot)), 0);
            lv_obj_set_style_bg_color(c.bar, lv_color_hex(ui_slot_color(slot)), LV_PART_INDICATOR);
        }
        const hal::AdsSlot &s = hal::adsSlot(static_cast<uint8_t>(slot));
        if (!ok || s.n == 0) {
            ui_set_text(c.value, "--");
            ui_bar_anim(c.bar, 0);
        } else {
            ui_fmt_value(num, sizeof(num), slot, s.value);
            snprintf(buf, sizeof(buf), "%s %s", num, ui_slot_unit(slot));
            ui_set_text(c.value, buf);
            lv_obj_set_style_text_color(c.value, lv_color_hex(s.alarm ? UI_BAD : UI_TEXT), 0);
            float lo, hi;
            hal::adsRange(static_cast<uint8_t>(slot), lo, hi);
            const float f = hi > lo ? (s.value - lo) / (hi - lo) : 0;
            ui_bar_anim(c.bar, static_cast<int>(f < 0 ? 0 : f > 1 ? 1000 : f * 1000));
        }
        // ขีดของกฎ (สูงสุด 2 ขีดต่อการ์ด)
        int k = 0;
        const int w = lv_obj_get_width(c.bar);
        for (int r = 0; r < hal::RULES_MAX && k < 2; r++) {
            const hal::Rule &rule = hal::rulesCfg().rule[r];
            if (!rule.en || rule.slot != slot) continue;
            float lo, hi;
            hal::adsRange(static_cast<uint8_t>(slot), lo, hi);
            float f = hi > lo ? (rule.thr - lo) / (hi - lo) : 0;
            f = f < 0 ? 0 : f > 1 ? 1 : f;
            lv_obj_clear_flag(c.tick[k], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_x(c.tick[k], static_cast<lv_coord_t>(f * (w - 2)));
            lv_obj_set_style_bg_color(c.tick[k], lv_color_hex(hal::rules().active[r] ? UI_OUT : 0x9A7A2A), 0);
            k++;
        }
        for (; k < 2; k++) lv_obj_add_flag(c.tick[k], LV_OBJ_FLAG_HIDDEN);
    }
}

void refreshPcf()
{
    const hal::PcfState &p = hal::pcf();
    char buf[24];
    snprintf(buf, sizeof(buf), "PCF\n0x%02X", p.addr);
    ui_set_text(gPcfInfo, p.found ? buf : "PCF\n--");
    lv_obj_set_style_text_color(gPcfInfo, lv_color_hex(p.found ? UI_OK : UI_BAD), 0);
    for (int i = 0; i < 8; i++) styleLed(i);
}

void refreshBus()
{
    const hal::BusState &b = hal::bus();
    const bool adsOk = hal::ads().found, pcfOk = hal::pcf().found;
    runDot(gDotAds, BUS_ADS_X + 8, adsOk ? b.adsPerSec : 0, gAnimAds);
    runDot(gDotPcf, BUS_PCF_X + 8, pcfOk ? b.pcfPerSec : 0, gAnimPcf);
    lv_obj_set_style_text_color(gLblAds, lv_color_hex(adsOk ? UI_ACCENT : hal::ads().demo ? UI_MUTED : UI_BAD), 0);
    lv_obj_set_style_text_color(gLblPcf, lv_color_hex(pcfOk ? UI_OUT : UI_BAD), 0);
    char buf[32];
    if (b.errors) snprintf(buf, sizeof(buf), "%u/วิ #f87171 !%lu#", b.txPerSec, static_cast<unsigned long>(b.errors));
    else snprintf(buf, sizeof(buf), "%u/วิ", b.txPerSec);
    ui_set_text(gRate, buf);
}

void refreshRule()
{
    const hal::RulesState &r = hal::rules();
    uint32_t sum = 0, best = 0;
    int pick = -1, on = 0;
    for (int i = 0; i < hal::RULES_MAX; i++) {
        sum += r.fired[i];
        if (hal::rulesCfg().rule[i].en) on++;
        if (r.active[i] && (pick < 0 || r.firedAt[i] >= best)) {
            pick = i;
            best = r.firedAt[i];
        }
    }
    char buf[80];
    if (pick >= 0) {
        char t[64];
        ui_rule_text(t, sizeof(t), pick, true);
        snprintf(buf, sizeof(buf), "กฎ %d  %s", pick + 1, t);
    } else if (hal::rulesCfg().barOn) {
        snprintf(buf, sizeof(buf), "แถบไฟ %s  %d/%d ดวง", hal::ADS_SLOT_NAMES[hal::rulesCfg().sel], r.barLit, r.barTotal);
    } else {
        snprintf(buf, sizeof(buf), "ยังไม่มีกฎทำงาน");
    }
    ui_set_text(gRuleText, buf);
    lv_obj_set_style_text_color(gRuleText, lv_color_hex(pick >= 0 ? UI_TEXT : UI_MUTED), 0);
    lv_obj_set_style_text_color(gBolt, lv_color_hex(pick >= 0 ? UI_OUT : UI_MUTED), 0);
    snprintf(buf, sizeof(buf), "%d เปิด", on);
    ui_set_text(gRuleCount, buf);
    if (sum != gFiredSum) {
        if (gFiredSum != 0 || sum < 4) {  // ไม่กะพริบตอนเปิดเครื่องที่นับย้อนหลัง
            ui_flash(gRuleBox, 0x5A3D0B, UI_CARD, 700);
            ui_pop(gBolt);
        }
        gFiredSum = sum;
    }
}

void tick(lv_timer_t *)
{
    refreshCards();
    refreshPcf();
    refreshBus();
    refreshRule();
}

lv_obj_t *strip(lv_obj_t *tab, int h)
{
    lv_obj_t *b = ui_box(tab, UI_CARD, 7);
    lv_obj_set_size(b, LV_PCT(100), h);
    lv_obj_set_style_pad_hor(b, 8, 0);
    return b;
}

void buildCards(lv_obj_t *tab)
{
    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 58);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) {
        ACard &c = gCard[i];
        c.slot = -1;
        c.box = ui_box(row, UI_CARD, 7);
        lv_obj_set_height(c.box, 58);
        lv_obj_set_flex_grow(c.box, 1);
        lv_obj_set_style_pad_all(c.box, 5, 0);
        lv_obj_add_flag(c.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c.box, onCard, LV_EVENT_CLICKED, &c);
        c.name = ui_label(c.box, "A0", nullptr, lv_color_hex(UI_ACCENT));
        lv_obj_align(c.name, LV_ALIGN_TOP_LEFT, 0, -4);
        c.value = ui_label(c.box, "--", nullptr, lv_color_hex(UI_TEXT));
        lv_label_set_long_mode(c.value, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(c.value, LV_PCT(100));
        lv_obj_align(c.value, LV_ALIGN_LEFT_MID, 0, 2);
        c.bar = lv_bar_create(c.box);
        lv_obj_set_size(c.bar, LV_PCT(100), 5);
        lv_obj_align(c.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_bar_set_range(c.bar, 0, 1000);
        lv_obj_set_style_bg_color(c.bar, lv_color_hex(UI_LINE), LV_PART_MAIN);
        lv_obj_set_style_radius(c.bar, 3, 0);
        lv_obj_set_style_radius(c.bar, 3, LV_PART_INDICATOR);
        for (int k = 0; k < 2; k++) {
            c.tick[k] = ui_box(c.box, UI_OUT, 1);
            lv_obj_set_size(c.tick[k], 2, 11);
            lv_obj_align(c.tick[k], LV_ALIGN_BOTTOM_LEFT, 0, 3);
            lv_obj_add_flag(c.tick[k], LV_OBJ_FLAG_HIDDEN);
        }
        for (lv_obj_t *o : {c.name, c.value, c.bar, c.tick[0], c.tick[1]}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
}

void buildPcf(lv_obj_t *tab)
{
    lv_obj_t *b = strip(tab, 44);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    gPcfInfo = ui_label(b, "PCF\n--", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    for (int i = 0; i < 8; i++) {
        Led &l = gLed[i];
        l.col = lv_obj_create(b);
        lv_obj_remove_style_all(l.col);
        lv_obj_set_size(l.col, 28, 40);
        lv_obj_clear_flag(l.col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(l.col, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(l.col, onLed, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        l.dot = ui_box(l.col, UI_LINE, LV_RADIUS_CIRCLE);
        lv_obj_set_size(l.dot, 16, 16);
        lv_obj_align(l.dot, LV_ALIGN_TOP_MID, 0, 5);
        l.inner = ui_box(l.dot, UI_ACCENT, LV_RADIUS_CIRCLE);
        lv_obj_set_size(l.inner, 6, 6);
        lv_obj_center(l.inner);
        char name[4];
        snprintf(name, sizeof(name), "P%d", i);
        l.name = ui_label(l.col, name, &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
        lv_obj_align(l.name, LV_ALIGN_BOTTOM_MID, 0, 0);
        for (lv_obj_t *o : {l.dot, l.inner, l.name}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
}

void buildBus(lv_obj_t *tab)
{
    gBusBox = strip(tab, 34);
    lv_obj_add_flag(gBusBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gBusBox, onBus, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = ui_label(gBusBox, "CYD", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    lv_obj_set_pos(l, -4, -1);
    gLblAds = ui_label(gBusBox, "ADS", &lv_font_montserrat_14, lv_color_hex(UI_ACCENT));
    lv_obj_set_pos(gLblAds, BUS_ADS_X - 4, -1);
    gLblPcf = ui_label(gBusBox, "PCF", &lv_font_montserrat_14, lv_color_hex(UI_OUT));
    lv_obj_set_pos(gLblPcf, BUS_PCF_X - 4, -1);
    lv_obj_t *line = ui_box(gBusBox, UI_LINE, 1);
    lv_obj_set_size(line, BUS_PCF_X + 26 - BUS_X0, 2);
    lv_obj_set_pos(line, BUS_X0, BUS_Y);
    gDotAds = ui_box(gBusBox, UI_ACCENT, 3);
    lv_obj_set_size(gDotAds, 10, 7);
    lv_obj_set_pos(gDotAds, BUS_X0, BUS_Y - 3);
    gDotPcf = ui_box(gBusBox, UI_OUT, 3);
    lv_obj_set_size(gDotPcf, 10, 7);
    lv_obj_set_pos(gDotPcf, BUS_X0, BUS_Y - 3);
    gRate = ui_label(gBusBox, "", nullptr, lv_color_hex(UI_TEXT));
    lv_label_set_recolor(gRate, true);
    lv_obj_align(gRate, LV_ALIGN_RIGHT_MID, 0, 0);
    for (lv_obj_t *o : {l, gLblAds, gLblPcf, line, gDotAds, gDotPcf, gRate}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

void buildRule(lv_obj_t *tab)
{
    gRuleBox = strip(tab, 32);
    lv_obj_set_flex_flow(gRuleBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gRuleBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(gRuleBox, 6, 0);
    lv_obj_add_flag(gRuleBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gRuleBox, onRules, LV_EVENT_CLICKED, nullptr);
    gBolt = ui_label(gRuleBox, LV_SYMBOL_CHARGE, nullptr, lv_color_hex(UI_MUTED));
    gRuleText = ui_label(gRuleBox, "", nullptr, lv_color_hex(UI_MUTED));
    lv_label_set_long_mode(gRuleText, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(gRuleText, 1);
    gRuleCount = ui_label(gRuleBox, "", nullptr, lv_color_hex(UI_MUTED));
    for (lv_obj_t *o : {gBolt, gRuleText, gRuleCount}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

}  // namespace

void ui_dash_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_row(tab, 5, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    buildCards(tab);
    buildPcf(tab);
    buildBus(tab);
    buildRule(tab);
    lv_timer_create(tick, app::UI_REFRESH_MS, nullptr);
    tick(nullptr);
}
