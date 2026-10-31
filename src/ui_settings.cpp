#include <math.h>

#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "ตั้งค่า": ปุ่มย่อย 2 ปุ่ม  ADC | ช่อง
//    ADC   ชิป / address / ช่วงวัด (PGA) / อัตราสุ่ม / เฉลี่ย / เสียงเตือน  (เปลี่ยนแล้วใช้ทันที + บันทึก NVS)
//    ช่อง  ชื่อ + หน่วย + แปลงหน่วยแบบเส้นตรง 2 จุด + ขีดเตือนต่ำ/สูง ของแต่ละช่องวัด
//  + ฟังก์ชันช่วยแสดงผลที่ทุกแท็บใช้ (สีช่อง, ชื่อ, หน่วย, จัดรูปตัวเลข) + แป้นตัวเลขเต็มจอ
// =====================================================================

const char *const ADS_NAME_LIST[ADS_NAME_COUNT] = {"ช่อง",       "ปุ่มหมุน",   "LM35",           "LDR แสง",    "แบตเตอรี่",   "แรงดัน",     "กระแส",
                                                   "ความชื้นดิน", "แก๊ส",       "โหลดเซลล์",      "เทอร์โมคัปเปิล", "จอยสติ๊ก X", "จอยสติ๊ก Y", "เซนเซอร์"};
const char *const ADS_UNIT_LIST[ADS_UNIT_COUNT] = {"V", "mV", "%", "°C", "°F", "A", "mA", "kPa", "kg", "lux", "ppm", ""};

uint32_t ui_slot_color(int slot)
{
    static const uint32_t colors[hal::ADS_SLOTS] = {0x38BDF8, 0xFBBF24, 0x34D399, 0xF472B6, 0xA78BFA, 0x2DD4BF};
    return colors[slot >= 0 && slot < hal::ADS_SLOTS ? slot : 0];
}

const char *ui_slot_name(int slot)
{
    const uint8_t n = hal::adsChCfg(static_cast<uint8_t>(slot)).name;
    return ADS_NAME_LIST[n < ADS_NAME_COUNT ? n : 0];
}

const char *ui_slot_unit(int slot)
{
    const uint8_t u = hal::adsChCfg(static_cast<uint8_t>(slot)).unit;
    return ADS_UNIT_LIST[u < ADS_UNIT_COUNT ? u : 0];
}

void ui_fmt_value(char *buf, size_t n, int slot, float value)
{
    const int d = hal::adsDecimals(static_cast<uint8_t>(slot));
    if (fabsf(value) < 0.5f * powf(10.0f, -d)) value = 0;  // ไม่แสดง "-0.000"
    snprintf(buf, n, "%.*f", d, value);
}

void ui_fmt_volts(char *buf, size_t n, float volts)
{
    if (fabsf(volts) < 1.0f) snprintf(buf, n, "%.2f mV", volts * 1000.0f);
    else snprintf(buf, n, "%.3f V", volts);
}

// =====================================================================
//  แป้นตัวเลข (btnmatrix CLICK_TRIG: ปุ่มทำงานตอนยกนิ้ว ครั้งเดียว ไม่ทะลุไปโดนของข้างใต้)
// =====================================================================
namespace {

lv_obj_t *gPad = nullptr, *gPadText;
char gPadBuf[16];
void (*gPadDone)(float) = nullptr;

const char *PAD_MAP[] ={"7", "8", "9", LV_SYMBOL_BACKSPACE, "\n", "4", "5", "6", "-", "\n", "1", "2", "3", ".", "\n", "ยกเลิก", "0", "ตกลง", ""};

void padClose()
{
    lv_obj_del_async(gPad);  // ห้ามลบ widget ที่ event ของมันกำลังทำงานอยู่
    gPad = nullptr;
}

void onPadKey(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    const uint16_t id = lv_btnmatrix_get_selected_btn(m);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char *t = lv_btnmatrix_get_btn_text(m, id);
    const size_t len = strlen(gPadBuf);
    if (strcmp(t, "ตกลง") == 0) {
        const float v = static_cast<float>(atof(gPadBuf));
        void (*done)(float) = gPadDone;
        padClose();
        if (len > 0 && done != nullptr) done(v);
        return;
    }
    if (strcmp(t, "ยกเลิก") == 0) {
        padClose();
        return;
    }
    if (strcmp(t, LV_SYMBOL_BACKSPACE) == 0) {
        if (len > 0) gPadBuf[len - 1] = '\0';
    } else if (strcmp(t, "-") == 0) {
        if (gPadBuf[0] == '-') memmove(gPadBuf, gPadBuf + 1, len);  // สลับเครื่องหมาย
        else if (len < sizeof(gPadBuf) - 2) {
            memmove(gPadBuf + 1, gPadBuf, len + 1);
            gPadBuf[0] = '-';
        }
    } else if (strcmp(t, ".") == 0) {
        if (strchr(gPadBuf, '.') == nullptr && len < sizeof(gPadBuf) - 1) strcat(gPadBuf, ".");
    } else if (len < 10) {
        strcat(gPadBuf, t);
    }
    lv_label_set_text(gPadText, gPadBuf[0] ? gPadBuf : " ");
}

}  // namespace

void ui_keypad_open(const char *title, float value, void (*done)(float value))
{
    if (gPad != nullptr) return;
    gPadDone = done;
    snprintf(gPadBuf, sizeof(gPadBuf), "%g", value);

    gPad = ui_overlay();
    lv_obj_set_style_bg_opa(gPad, LV_OPA_70, 0);
    lv_obj_t *panel = ui_box(gPad, UI_CARD, 10);
    lv_obj_set_size(panel, 250, 226);
    lv_obj_center(panel);
    lv_obj_set_style_pad_all(panel, 6, 0);

    lv_obj_t *t = ui_label(panel, title, nullptr, lv_color_hex(UI_ACCENT));
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 2, -2);
    lv_obj_t *disp = ui_box(panel, UI_BG, 6);
    lv_obj_set_size(disp, LV_PCT(100), 34);
    lv_obj_align(disp, LV_ALIGN_TOP_MID, 0, 20);
    gPadText = ui_label(disp, gPadBuf, &font_th_24, lv_color_hex(UI_TEXT));
    lv_obj_align(gPadText, LV_ALIGN_RIGHT_MID, -6, 0);

    lv_obj_t *m = lv_btnmatrix_create(panel);
    lv_btnmatrix_set_map(m, PAD_MAP);
    lv_obj_set_size(m, LV_PCT(100), 154);
    lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_gap(m, 4, 0);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(UI_CARD2), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_hex(UI_TEXT), LV_PART_ITEMS);
    lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(m, onPadKey, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_indev_t *indev = lv_indev_get_act();
    if (indev != nullptr) lv_indev_wait_release(indev);  // นิ้วที่เพิ่งแตะเปิดแป้น ไม่ให้กดปุ่มบนแป้นต่อ
}

// =====================================================================
//  แท็บตั้งค่า
// =====================================================================
namespace {

// สร้างเมื่อเปิดดู / ลบเมื่อออก (แท็บ ADC): gTick = nullptr แปลว่าตอนนี้ไม่มีหน้านี้ ห้ามแตะ widget
lv_timer_t *gTick = nullptr;
lv_obj_t *gTab, *gSubBtn[2], *gSub[2];
// ADC
lv_obj_t *gDdChip, *gDdAddr, *gDdGain, *gDdRate, *gDdAvg, *gSwBeep, *gAdcInfo;
// ช่อง
constexpr int FIELD_COUNT = 6;  // v1 y1 v2 y2 lo hi
lv_obj_t *gSlotBtn[hal::ADS_SLOTS], *gDdName, *gDdUnit, *gField[FIELD_COUNT], *gSwLo, *gSwHi, *gChInfo;
int gSlot = 0, gEditField = 0;

lv_obj_t *settingRow(lv_obj_t *parent, const char *title)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 5, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if (title != nullptr) {
        lv_obj_t *l = ui_label(r, title);
        lv_obj_set_flex_grow(l, 1);
    }
    return r;
}

lv_obj_t *dropdown(lv_obj_t *parent, int w, lv_event_cb_t cb)
{
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_obj_set_width(d, w);
    lv_obj_set_style_pad_ver(d, 6, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(UI_CARD2), 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_shadow_width(d, 0, 0);
    lv_obj_t *list = lv_dropdown_get_list(d);
    lv_obj_set_style_text_font(list, &font_th_16, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(UI_CARD2), 0);
    lv_obj_set_style_max_height(list, 180, 0);
    lv_obj_add_event_cb(d, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return d;
}

// ---------- ADC ----------
void onAdc(lv_event_t *)
{
    hal::AdsCfg c = hal::adsCfg();
    const uint16_t chip = lv_dropdown_get_selected(gDdChip);
    const uint16_t addr = lv_dropdown_get_selected(gDdAddr);
    c.chipSel = static_cast<uint8_t>(chip);
    c.addrSel = addr == 0 ? 0 : static_cast<uint8_t>(0x48 + addr - 1);
    c.gain = static_cast<uint8_t>(lv_dropdown_get_selected(gDdGain));
    c.rate = static_cast<uint8_t>(lv_dropdown_get_selected(gDdRate));
    c.avg = static_cast<uint8_t>(lv_dropdown_get_selected(gDdAvg));
    c.beep = lv_obj_has_state(gSwBeep, LV_STATE_CHECKED);
    hal::adsSetCfg(c);
    ui_ads_changed();
}

void onAdcDefaults(lv_event_t *)
{
    hal::AdsCfg c;
    c.diff = hal::adsCfg().diff;
    hal::adsSetCfg(c);
    ui_ads_changed();
    hal::soundPlay(hal::Melody::Success);
}

void buildRateOptions()
{
    // อัตราสุ่มของ 2 รุ่นต่างกัน (ADS1015 เร็วกว่า) จึงสร้างรายการใหม่ตามชิปที่ใช้อยู่
    const bool is1015 = hal::ads().chip == hal::AdsChip::ADS1015;
    char opts[96] = "";
    for (int i = 0; i < hal::ADS_RATES; i++) {
        char one[16];
        snprintf(one, sizeof(one), "%s%u SPS", i ? "\n" : "", is1015 ? hal::ADS1015_SPS[i] : hal::ADS1115_SPS[i]);
        strcat(opts, one);
    }
    if (strcmp(lv_dropdown_get_options(gDdRate), opts) != 0) lv_dropdown_set_options(gDdRate, opts);
}

void buildAdc(lv_obj_t *box)
{
    lv_obj_t *card = ui_card(box, nullptr);
    lv_obj_set_style_pad_row(card, 4, 0);
    gDdChip = dropdown(settingRow(card, "ชิป"), 160, onAdc);
    lv_dropdown_set_options(gDdChip, "อัตโนมัติ\nADS1115 16 บิต\nADS1015 12 บิต");
    gDdAddr = dropdown(settingRow(card, "Address"), 160, onAdc);
    lv_dropdown_set_options(gDdAddr, "อัตโนมัติ\n0x48 ADDR-GND\n0x49 ADDR-VDD\n0x4A ADDR-SDA\n0x4B ADDR-SCL");
    gDdGain = dropdown(settingRow(card, "ช่วงวัด (PGA)"), 160, onAdc);
    lv_dropdown_set_options(gDdGain, "±6.144 V (x2/3)\n±4.096 V (x1)\n±2.048 V (x2)\n±1.024 V (x4)\n±0.512 V (x8)\n±0.256 V (x16)");
    gDdRate = dropdown(settingRow(card, "อัตราสุ่ม"), 160, onAdc);
    gDdAvg = dropdown(settingRow(card, "เฉลี่ยกี่ค่า"), 160, onAdc);
    lv_dropdown_set_options(gDdAvg, "1 (ไม่เฉลี่ย)\n2\n4\n8\n16");
    gSwBeep = ui_switch_row(card, "เสียงเตือนเมื่อเกินขีด", true, onAdc);

    gAdcInfo = ui_label(box, "", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(gAdcInfo, LV_PCT(100));
    lv_label_set_long_mode(gAdcInfo, LV_LABEL_LONG_WRAP);

    lv_obj_t *b = ui_btn(box, LV_SYMBOL_REFRESH " ค่าเริ่มต้น ADC", onAdcDefaults);
    lv_obj_set_width(b, LV_PCT(100));
}

void refreshAdc()
{
    const hal::AdsCfg &c = hal::adsCfg();
    buildRateOptions();
    lv_dropdown_set_selected(gDdChip, c.chipSel);
    lv_dropdown_set_selected(gDdAddr, c.addrSel == 0 ? 0 : c.addrSel - 0x48 + 1);
    lv_dropdown_set_selected(gDdGain, c.gain);
    lv_dropdown_set_selected(gDdRate, c.rate);
    lv_dropdown_set_selected(gDdAvg, c.avg);
    if (c.beep) lv_obj_add_state(gSwBeep, LV_STATE_CHECKED);
    else lv_obj_clear_state(gSwBeep, LV_STATE_CHECKED);

    const hal::AdsState &st = hal::ads();
    char buf[200];
    snprintf(buf, sizeof(buf), "ใช้ %s%s | 1 ขั้น = %.1f uV\nเต็มสเกล ±%.3f V แต่ขาเข้าห้ามเกิน VDD+0.3 V | เปลี่ยนแล้วบันทึกเอง",
             hal::adsChipName(st.chip), c.chipSel == 0 && st.found ? " (ตรวจพบเอง)" : "", hal::adsLsbVolts() * 1e6f, hal::ADS_FSR[c.gain]);
    ui_set_text(gAdcInfo, buf);
}

// ---------- ช่อง ----------
void saveCh(const hal::AdsChCfg &c)
{
    hal::adsSetChCfg(static_cast<uint8_t>(gSlot), c);
    ui_ads_changed();
}

// ชื่อบางชื่อตั้งหน่วย + จุดแปลงให้เลย (แก้ต่อเองได้)
void applyPreset(hal::AdsChCfg &c)
{
    struct Preset {
        uint8_t name, unit;
        float v1, y1, v2, y2;
    };
    static const Preset presets[] = {
        {1, 2, 0.0f, 0, 3.3f, 100},    // ปุ่มหมุน 0-3.3 V = 0-100 %
        {2, 3, 0.0f, 0, 1.0f, 100},    // LM35 10 mV/องศา
        {3, 2, 0.0f, 0, 3.3f, 100},    // LDR
        {4, 0, 0.0f, 0, 1.0f, 2},      // แบตเตอรี่ผ่านตัวแบ่งแรงดัน 1:2
        {7, 2, 2.8f, 0, 1.2f, 100},    // ความชื้นดิน (capacitive) แห้ง 2.8 V / เปียก 1.2 V
        {11, 2, 0.0f, -100, 3.3f, 100}, // จอยสติ๊ก กลาง = 0
        {12, 2, 0.0f, -100, 3.3f, 100},
    };
    for (const Preset &p : presets) {
        if (p.name != c.name) continue;
        c.unit = p.unit;
        c.v1 = p.v1;
        c.y1 = p.y1;
        c.v2 = p.v2;
        c.y2 = p.y2;
    }
}

void onName(lv_event_t *)
{
    hal::AdsChCfg c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    c.name = static_cast<uint8_t>(lv_dropdown_get_selected(gDdName));
    applyPreset(c);
    saveCh(c);
}

void onUnit(lv_event_t *)
{
    hal::AdsChCfg c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    c.unit = static_cast<uint8_t>(lv_dropdown_get_selected(gDdUnit));
    saveCh(c);
}

void onAlarmSw(lv_event_t *)
{
    hal::AdsChCfg c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    c.loOn = lv_obj_has_state(gSwLo, LV_STATE_CHECKED);
    c.hiOn = lv_obj_has_state(gSwHi, LV_STATE_CHECKED);
    saveCh(c);
}

float *fieldPtr(hal::AdsChCfg &c, int f)
{
    float *p[FIELD_COUNT] = {&c.v1, &c.y1, &c.v2, &c.y2, &c.lo, &c.hi};
    return p[f];
}

void onFieldDone(float v)
{
    hal::AdsChCfg c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    *fieldPtr(c, gEditField) = v;
    if (gEditField == 4) c.loOn = true;  // พิมพ์ขีดเตือนแล้วเปิดเตือนให้เลย
    if (gEditField == 5) c.hiOn = true;
    saveCh(c);
}

void editField(int f)
{
    static const char *titles[FIELD_COUNT] = {"จุดที่ 1: แรงดัน (V)", "จุดที่ 1: ค่าที่ต้องการ", "จุดที่ 2: แรงดัน (V)",
                                              "จุดที่ 2: ค่าที่ต้องการ", "เตือนเมื่อต่ำกว่า",     "เตือนเมื่อสูงกว่า"};
    gEditField = f;
    hal::AdsChCfg c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    ui_keypad_open(titles[gEditField], *fieldPtr(c, gEditField), onFieldDone);
}

void onField(lv_event_t *e)
{
    editField(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

void onCapture(lv_event_t *e)
{
    // "ใช้ค่าตอนนี้": วัดแรงดันจริงตอนนี้ใส่เป็นแรงดันของจุดที่ 1/2 (calibrate จากของจริง)
    const int point = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    hal::AdsChCfg c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    const float v = hal::adsSlot(static_cast<uint8_t>(gSlot)).volts;
    if (point == 0) c.v1 = v;
    else c.v2 = v;
    saveCh(c);
    hal::soundTone(1568, 40);
}

void onIdentity(lv_event_t *)
{
    hal::AdsChCfg c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    c.unit = 0;
    c.v1 = 0;
    c.y1 = 0;
    c.v2 = 1;
    c.y2 = 1;
    saveCh(c);
}

void onSlot(lv_event_t *e)
{
    gSlot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    ui_settings_refresh();
}

lv_obj_t *fieldBtn(lv_obj_t *parent, int f, int w)
{
    lv_obj_t *b = ui_btn(parent, "0", onField, reinterpret_cast<void *>(static_cast<uintptr_t>(f)));
    lv_obj_set_width(b, w);
    lv_obj_set_style_pad_hor(b, 4, 0);
    lv_obj_set_style_min_height(b, 34, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(UI_ACCENT), 0);
    gField[f] = b;
    return b;
}

void pointRow(lv_obj_t *parent, const char *title, int point)
{
    lv_obj_t *r = settingRow(parent, nullptr);
    lv_obj_t *l = ui_label(r, title, nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_width(l, 44);
    fieldBtn(r, point * 2, 74);
    ui_label(r, "→");
    fieldBtn(r, point * 2 + 1, 66);
    lv_obj_t *cap = ui_btn(r, "วัดเลย", onCapture, reinterpret_cast<void *>(static_cast<uintptr_t>(point)));
    lv_obj_set_flex_grow(cap, 1);
    lv_obj_set_style_pad_hor(cap, 2, 0);
}

lv_obj_t *alarmRow(lv_obj_t *parent, const char *title, int f)
{
    lv_obj_t *r = settingRow(parent, nullptr);
    lv_obj_t *l = ui_label(r, title, nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_flex_grow(l, 1);
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, 46, 24);
    lv_obj_add_event_cb(sw, onAlarmSw, LV_EVENT_VALUE_CHANGED, nullptr);
    fieldBtn(r, f, 90);
    return sw;
}

void buildCh(lv_obj_t *box)
{
    lv_obj_t *row = settingRow(box, nullptr);
    lv_obj_set_style_pad_column(row, 3, 0);
    static const char *labels[hal::ADS_SLOTS] = {"A0", "A1", "A2", "A3", "A0-1", "A2-3"};
    for (int i = 0; i < hal::ADS_SLOTS; i++) {
        lv_obj_t *b = ui_btn(row, labels[i], onSlot, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ui_slot_color(i)), LV_STATE_CHECKED);
        gSlotBtn[i] = b;
    }

    lv_obj_t *card = ui_card(box, nullptr);
    lv_obj_set_style_pad_row(card, 5, 0);
    row = settingRow(card, nullptr);
    gDdName = dropdown(row, 150, onName);
    char opts[400] = "";
    for (int i = 0; i < ADS_NAME_COUNT; i++) {
        if (i) strcat(opts, "\n");
        strcat(opts, ADS_NAME_LIST[i]);
    }
    lv_dropdown_set_options(gDdName, opts);
    lv_obj_t *l = ui_label(row, "หน่วย", nullptr, lv_color_hex(UI_MUTED));
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    gDdUnit = dropdown(row, 74, onUnit);
    opts[0] = '\0';
    for (int i = 0; i < ADS_UNIT_COUNT; i++) {
        if (i) strcat(opts, "\n");
        strcat(opts, ADS_UNIT_LIST[i][0] ? ADS_UNIT_LIST[i] : "(ไม่มี)");
    }
    lv_dropdown_set_options(gDdUnit, opts);

    ui_label(card, "แปลงหน่วย: แรงดัน → ค่า (เส้นตรงผ่าน 2 จุด)", nullptr, lv_color_hex(UI_ACCENT));
    pointRow(card, "จุด 1", 0);
    pointRow(card, "จุด 2", 1);
    gChInfo = ui_label(card, "", nullptr, lv_color_hex(UI_TEXT));

    card = ui_card(box, "ขีดเตือน (การ์ดเป็นสีแดง + เสียง)");
    gSwLo = alarmRow(card, "ต่ำกว่า", 4);
    gSwHi = alarmRow(card, "สูงกว่า", 5);

    lv_obj_t *b = ui_btn(box, "แสดงเป็นโวลต์ (ไม่แปลง)", onIdentity);
    lv_obj_set_width(b, LV_PCT(100));
}

void refreshCh()
{
    const int first = hal::adsFirstSlot(), count = hal::adsSlotCount();
    for (int i = 0; i < hal::ADS_SLOTS; i++) {
        // ช่องที่ไม่ได้วัดในโหมดนี้ยังตั้งค่าได้ แต่จางลงให้รู้
        lv_obj_set_style_text_opa(gSlotBtn[i], (i >= first && i < first + count) ? LV_OPA_COVER : LV_OPA_50, 0);
        if (i == gSlot) lv_obj_add_state(gSlotBtn[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(gSlotBtn[i], LV_STATE_CHECKED);
    }
    const hal::AdsChCfg &c = hal::adsChCfg(static_cast<uint8_t>(gSlot));
    lv_dropdown_set_selected(gDdName, c.name);
    lv_dropdown_set_selected(gDdUnit, c.unit);
    char buf[96];
    const float *vals[FIELD_COUNT] = {&c.v1, &c.y1, &c.v2, &c.y2, &c.lo, &c.hi};
    for (int f = 0; f < FIELD_COUNT; f++) {
        if (f == 0 || f == 2) snprintf(buf, sizeof(buf), "%.3f V", *vals[f]);
        else ui_fmt_value(buf, sizeof(buf), gSlot, *vals[f]);
        lv_label_set_text(lv_obj_get_child(gField[f], 0), buf);
    }
    if (c.loOn) lv_obj_add_state(gSwLo, LV_STATE_CHECKED);
    else lv_obj_clear_state(gSwLo, LV_STATE_CHECKED);
    if (c.hiOn) lv_obj_add_state(gSwHi, LV_STATE_CHECKED);
    else lv_obj_clear_state(gSwHi, LV_STATE_CHECKED);
}

void tick(lv_timer_t *)
{
    // ค่าปัจจุบันของช่องที่กำลังตั้ง เห็นผลการแปลงหน่วยทันที
    const hal::AdsSlot &s = hal::adsSlot(static_cast<uint8_t>(gSlot));
    char v[16], y[16], buf[64];
    ui_fmt_volts(v, sizeof(v), s.volts);
    ui_fmt_value(y, sizeof(y), gSlot, s.value);
    snprintf(buf, sizeof(buf), "ตอนนี้ %s = %s %s", v, y, ui_slot_unit(gSlot));
    ui_set_text(gChInfo, buf);
}

void onSub(lv_event_t *e)
{
    ui_settings_show(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

}  // namespace

void ui_settings_edit(int slot, int field)
{
    if (slot < 0 || slot >= hal::ADS_SLOTS || field < 0 || field >= FIELD_COUNT) return;
    gSlot = slot;
    ui_goto_tab(UI_TAB_ADC);
    ui_adc_show(3);
    ui_settings_show(1);
    ui_settings_refresh();
    editField(field);
}

void ui_settings_show(int index)
{
    if (gTick == nullptr) return;
    if (index < 0 || index > 1) return;
    for (int i = 0; i < 2; i++) {
        if (i == index) {
            lv_obj_clear_flag(gSub[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(gSubBtn[i], LV_STATE_CHECKED);
        } else {
            lv_obj_add_flag(gSub[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(gSubBtn[i], LV_STATE_CHECKED);
        }
    }
    lv_obj_scroll_to_y(gTab, 0, LV_ANIM_OFF);
}

void ui_settings_close()
{
    if (gTick != nullptr) lv_timer_del(gTick);
    gTick = nullptr;
}

void ui_settings_refresh()
{
    if (gTick == nullptr) return;
    refreshAdc();
    refreshCh();
    tick(nullptr);
}

void ui_settings_tab(lv_obj_t *tab)
{
    gTab = tab;
    lv_obj_t *nav = settingRow(tab, nullptr);
    lv_obj_set_style_pad_column(nav, 4, 0);
    const char *names[2] = {"ADC", "ช่อง A0-A3"};
    void (*builders[2])(lv_obj_t *) = {buildAdc, buildCh};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = ui_btn(nav, names[i], onSub, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(UI_LINE), 0);
        lv_obj_set_style_border_width(b, 0, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_CARD), 0);
        gSubBtn[i] = b;
    }
    for (int i = 0; i < 2; i++) {
        lv_obj_t *box = lv_obj_create(tab);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, 5, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        gSub[i] = box;
        builders[i](box);
    }
    gTick = lv_timer_create(tick, 250, nullptr);
    ui_settings_refresh();
    ui_settings_show(0);
}
