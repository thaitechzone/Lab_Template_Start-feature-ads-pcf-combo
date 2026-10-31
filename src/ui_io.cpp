#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "I/O": ขาทั้ง 8 ของ PCF8574 (จากแล็บ PCF8574)
//    แถวบน  P0-P3  /  แถวล่าง P4-P7  (ช่องส้ม = เอาต์พุต แตะเพื่อสลับ, ช่องฟ้า = อินพุต แสดงว่ากดอยู่ไหม)
//    ขาที่กฎ/แถบไฟคุมอยู่มีป้าย "กฎ N" / "แถบ" -- แตะ = ยึดคืนชั่วคราว (ป้ายเป็น "มือ") จนกว่ากฎจะเปลี่ยนสถานะ
//    แถวล่างสุด: ระดับไฟจริงของทั้ง 8 บิต (P7 ซ้าย ... P0 ขวา) + ค่าเป็นเลขฐานสิบหก
//    ปุ่มเฟือง: หน้าตั้งทิศทาง / ทำงานเมื่อ LOW เต็มจอ (ui_pins.cpp)
//    ไม่พบ PCF8574 -> แผงบอกวิธีตรวจสายทับหน้าแรก
// =====================================================================

namespace {

struct Tile {
    lv_obj_t *obj, *name, *ind, *dot, *text, *tag;
};

Tile gTile[8];
lv_obj_t *gDot, *gAddr, *gBit[8], *gHex, *gLost;
uint32_t gSig = 0xFFFFFFFF;

lv_obj_t *smallLabel(lv_obj_t *parent, const char *text, lv_color_t color)
{
    return ui_label(parent, text, &lv_font_montserrat_14, color);
}

void onTile(lv_event_t *e)
{
    const uint8_t pin = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (hal::pcfIsInput(pin)) return;  // อินพุตสั่งไม่ได้ (อ่านอย่างเดียว)
    ui_pop(gTile[pin].obj);
    ui_pin_tap(pin);
}

void onRescan(lv_event_t *)
{
    hal::pcfScan();
    ui_busmap_refresh();
}

void onPins(lv_event_t *)
{
    ui_pins_open();
}

void onGotoBus(lv_event_t *)
{
    ui_goto_tab(UI_TAB_BUS);
}

void onInput(uint8_t, bool active)  // เรียกจาก task "i2c": ห้ามแตะ LVGL (เสียงส่งผ่านคิว ปลอดภัย)
{
    if (active) hal::soundClick();
}

void styleTile(int pin)
{
    const Tile &t = gTile[pin];
    const bool in = hal::pcfIsInput(pin);
    const bool on = hal::pcfActive(pin);
    const uint32_t col = in ? UI_ACCENT : UI_OUT;
    lv_obj_set_style_bg_color(t.obj, lv_color_hex(on ? (in ? UI_IN_BG : UI_OUT_BG) : UI_CARD), 0);
    lv_obj_set_style_border_color(t.obj, lv_color_hex(on ? col : UI_LINE), 0);
    if (in) {
        // อินพุต: วงแหวน (ปุ่ม) มีจุดกลางเมื่อกด
        lv_obj_set_style_bg_opa(t.ind, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(t.ind, lv_color_hex(on ? UI_ACCENT : 0x3B4A69), 0);
        if (on) lv_obj_clear_flag(t.dot, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(t.dot, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(t.text, on ? "กด" : "ปล่อย");
        ui_pulse(t.ind, false, 0);
    } else {
        // เอาต์พุต: หลอดไฟ ติด = ส้มเต็มดวง + วงแสงกระเพื่อม
        lv_obj_set_style_bg_opa(t.ind, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(t.ind, lv_color_hex(on ? UI_OUT : UI_LINE), 0);
        lv_obj_set_style_border_color(t.ind, lv_color_hex(on ? 0xFDE68A : 0x3B4A69), 0);
        lv_obj_add_flag(t.dot, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(t.text, on ? "ติด" : "ดับ");
        ui_pulse(t.ind, on, UI_OUT);
    }
    lv_obj_set_style_text_color(t.text, lv_color_hex(on ? col : UI_MUTED), 0);

    // ใครคุมขานี้อยู่
    const hal::RulesState &r = hal::rules();
    const uint8_t bit = static_cast<uint8_t>(1 << pin);
    char tag[16] = "";
    if (!in && (r.ownMask & bit)) {
        if (r.manualMask & bit) snprintf(tag, sizeof(tag), "มือ");
        else if (r.ruleOfPin[pin] == 9) snprintf(tag, sizeof(tag), "แถบ");
        else if (r.ruleOfPin[pin] != 0) snprintf(tag, sizeof(tag), "กฎ %u", r.ruleOfPin[pin]);
    }
    ui_set_text(t.tag, tag);
}

void refresh()
{
    const hal::PcfState &s = hal::pcf();
    if (!s.found) lv_obj_clear_flag(gLost, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(gLost, LV_OBJ_FLAG_HIDDEN);

    char buf[24];
    snprintf(buf, sizeof(buf), "0x%02X", s.addr);
    ui_set_text(gAddr, s.found ? buf : "--");
    lv_obj_set_style_bg_color(gDot, lv_color_hex(s.found ? UI_OK : UI_BAD), 0);
    lv_obj_set_style_text_color(gAddr, lv_color_hex(s.found ? UI_OK : UI_BAD), 0);

    // ไบต์ของพอร์ต: เอาต์พุตใช้ค่าที่เขียน, อินพุตใช้ค่าที่อ่าน (= ระดับไฟจริงที่ขา)
    const uint8_t port = static_cast<uint8_t>((s.outByte & ~s.dirMask) | (s.inByte & s.dirMask));
    for (int b = 0; b < 8; b++) {
        lv_obj_t *cell = gBit[b];
        const bool one = (port >> b) & 1;
        const uint32_t col = hal::pcfIsInput(b) ? UI_ACCENT : UI_OUT;
        ui_set_text(cell, one ? "1" : "0");
        lv_obj_set_style_bg_opa(cell, one ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(col), 0);
        lv_obj_set_style_border_width(cell, one ? 1 : 0, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(col), 0);
        lv_obj_set_style_text_color(cell, lv_color_hex(one ? col : UI_BG), 0);
    }
    snprintf(buf, sizeof(buf), "0x%02X", port);
    ui_set_text(gHex, buf);
    for (int p = 0; p < 8; p++) styleTile(p);
}

void tick(lv_timer_t *)
{
    const hal::PcfState &s = hal::pcf();
    const hal::RulesState &r = hal::rules();
    // วาดใหม่เฉพาะเมื่อมีอะไรเปลี่ยน
    uint32_t sig = (s.found ? 1u : 0u) ^ (s.intMode ? 2u : 0u) ^ (static_cast<uint32_t>(s.addr) << 2) ^
                   (static_cast<uint32_t>(s.outByte) << 9) ^ (static_cast<uint32_t>(s.inByte) << 17) ^
                   (static_cast<uint32_t>(s.dirMask) << 24) ^ (static_cast<uint32_t>(s.lowMask) << 12) ^ (static_cast<uint32_t>(s.outOn) << 20);
    sig = sig * 31 + r.ownMask * 7 + r.manualMask * 13;
    for (int p = 0; p < 8; p++) sig = sig * 3 + r.ruleOfPin[p];
    if (sig == gSig) return;
    gSig = sig;
    refresh();
}

lv_obj_t *makeTile(lv_obj_t *row, int pin)
{
    Tile &t = gTile[pin];
    t.obj = ui_box(row, UI_CARD, 8);
    lv_obj_set_size(t.obj, 72, 54);
    lv_obj_set_flex_grow(t.obj, 1);
    lv_obj_set_style_border_width(t.obj, 2, 0);
    lv_obj_set_style_pad_hor(t.obj, 6, 0);
    lv_obj_set_style_pad_ver(t.obj, 3, 0);
    lv_obj_add_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t.obj, onTile, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(pin)));

    char name[4];
    snprintf(name, sizeof(name), "P%d", pin);
    t.name = ui_label(t.obj, name, nullptr, lv_color_hex(UI_TEXT));
    lv_obj_align(t.name, LV_ALIGN_TOP_LEFT, 0, -2);

    t.ind = ui_box(t.obj, UI_LINE, LV_RADIUS_CIRCLE);
    lv_obj_set_size(t.ind, 16, 16);
    lv_obj_set_style_border_width(t.ind, 2, 0);
    lv_obj_align(t.ind, LV_ALIGN_TOP_RIGHT, -2, 4);
    t.dot = ui_box(t.ind, UI_ACCENT, LV_RADIUS_CIRCLE);
    lv_obj_set_size(t.dot, 6, 6);
    lv_obj_center(t.dot);

    t.text = ui_label(t.obj, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(t.text, LV_ALIGN_BOTTOM_LEFT, 0, 2);
    t.tag = ui_label(t.obj, "", &lv_font_montserrat_14, lv_color_hex(UI_OUT));
    lv_obj_set_style_text_font(t.tag, &font_th_16, 0);
    lv_obj_align(t.tag, LV_ALIGN_BOTTOM_RIGHT, 0, 2);
    for (lv_obj_t *o : {t.name, t.ind, t.dot, t.text, t.tag}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return t.obj;
}

lv_obj_t *flexRow(lv_obj_t *parent, int gap)
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

void buildLostPanel(lv_obj_t *tab)
{
    gLost = ui_box(tab, UI_BG, 0);
    lv_obj_add_flag(gLost, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(gLost, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(gLost, 0, 0);
    lv_obj_set_style_pad_all(gLost, 2, 0);
    lv_obj_set_flex_flow(gLost, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(gLost, 6, 0);

    lv_obj_t *head = flexRow(gLost, 6);
    lv_obj_t *dot = ui_box(head, UI_BAD, LV_RADIUS_CIRCLE);
    lv_obj_set_size(dot, 10, 10);
    ui_label(head, "ไม่พบ PCF8574 บนบัส I2C", nullptr, lv_color_hex(UI_BAD));

    lv_obj_t *card = ui_card(gLost, nullptr);
    lv_obj_set_style_pad_row(card, 2, 0);
    ui_label(card, "ตรวจการต่อสาย (บัสเดียวกับ ADS):", nullptr, lv_color_hex(UI_MUTED));
    ui_label(card, "SDA = IO27 (CN1)   SCL = IO22");
    ui_label(card, "VCC = 3.3V   GND = GND");
    ui_label(card, "ตำแหน่ง 0x20-0x27 / 0x38-0x3F", nullptr, lv_color_hex(UI_MUTED));

    lv_obj_t *row = flexRow(gLost, 6);
    lv_obj_t *scan = ui_btn(row, LV_SYMBOL_REFRESH " สแกนอีกครั้ง", onRescan);
    lv_obj_set_flex_grow(scan, 1);
    lv_obj_set_height(scan, 40);
    lv_obj_set_style_bg_color(scan, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_text_color(scan, lv_color_hex(UI_BG), 0);
    lv_obj_t *bus = ui_btn(row, "ดูแท็บบัส", onGotoBus);
    lv_obj_set_height(bus, 40);
}

}  // namespace

void ui_pin_tap(uint8_t pin)
{
    if (pin > 7 || hal::pcfIsInput(pin)) return;
    hal::rulesManual(pin);  // ขาที่กฎคุมอยู่: กฎหยุดคุมจนกว่าจะเปลี่ยนสถานะครั้งถัดไป
    hal::pcfToggle(pin);
}

void ui_io_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_top(tab, 4, 0);
    lv_obj_set_style_pad_row(tab, 4, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    // ---- แถวสถานะ ----
    lv_obj_t *status = flexRow(tab, 6);
    lv_obj_set_height(status, 22);
    gDot = ui_box(status, UI_OK, LV_RADIUS_CIRCLE);
    lv_obj_set_size(gDot, 10, 10);
    ui_label(status, "PCF8574");
    gAddr = smallLabel(status, "--", lv_color_hex(UI_OK));

    // ---- 8 ขา: 2 แถว แถวละ 4 ----
    for (int r = 0; r < 2; r++) {
        lv_obj_t *row = flexRow(tab, 4);
        for (int c = 0; c < 4; c++) makeTile(row, r * 4 + c);
    }

    // ---- ไบต์ของพอร์ต P7..P0 ----
    lv_obj_t *bits = ui_box(tab, UI_CARD, 8);
    lv_obj_set_size(bits, LV_PCT(100), 36);
    lv_obj_set_flex_flow(bits, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bits, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bits, 6, 0);
    lv_obj_set_style_pad_column(bits, 3, 0);
    for (int b = 7; b >= 0; b--) {
        lv_obj_t *cell = smallLabel(bits, "1", lv_color_hex(UI_TEXT));
        lv_obj_set_size(cell, 22, 24);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(cell, 4, 0);
        lv_obj_set_style_radius(cell, 4, 0);
        gBit[b] = cell;
    }
    lv_obj_t *spacer = lv_obj_create(bits);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_height(spacer, 1);  // ตัวดันว่าง: ค่าเริ่มต้นของ lv_obj สูง ~130 px
    lv_obj_set_flex_grow(spacer, 1);
    gHex = ui_label(bits, "0xFF", &font_th_24, lv_color_hex(UI_TEXT));
    lv_obj_t *set = ui_btn(bits, LV_SYMBOL_SETTINGS, onPins);
    lv_obj_set_style_min_height(set, 30, 0);
    lv_obj_set_style_pad_ver(set, 2, 0);


    buildLostPanel(tab);
    hal::pcfOnInputChange(onInput);
    lv_timer_create(tick, 40, nullptr);
    tick(nullptr);
}
