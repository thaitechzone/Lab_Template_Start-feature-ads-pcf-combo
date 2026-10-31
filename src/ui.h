#pragma once

// =====================================================================
//  ui.h -- ส่วนกลางของ UI (LVGL 8.3)
//    ui_main.cpp       ธีม + tabview 6 แท็บ (รวม | ADC | I/O | กฎ | บัส | ระบบ) + ฟังก์ชันช่วยสร้าง widget/ภาพเคลื่อนไหว
//    ui_dash.cpp       แท็บ "รวม"   แดชบอร์ด: A0-A3 + ไฟ P0-P7 + บัสย่อ + กฎล่าสุดที่ทำงาน
//    ui_adc.cpp        แท็บ "ADC"   ปุ่มย่อย ค่า / ช่อง / กราฟ / ตั้งค่า (หน้าจากแล็บ ADS1115)
//      ui_overview.cpp ส่วน "ค่า"    การ์ด 4 ช่อง A0-A3 (หรือ 2 คู่ต่าง A0-A1 / A2-A3)
//      ui_detail.cpp   ส่วน "ช่อง"   เกจใหญ่ 1 ช่อง + ต่ำสุด/สูงสุด/เฉลี่ย + ค้างค่า
//      ui_chart.cpp    ส่วน "กราฟ"   กราฟสดหลายช่อง
//      ui_settings.cpp ส่วน "ตั้งค่า" ADC + ช่อง (ชื่อ/หน่วย/แปลง 2 จุด/เตือน) + แป้นตัวเลข
//    ui_io.cpp         แท็บ "I/O"   ขา P0-P7 ของ PCF8574 (แตะสลับ) + ui_pins.cpp ตั้งทิศทาง/ทำงานเมื่อ LOW
//    ui_rules.cpp      แท็บ "กฎ"    ปุ่มย่อย กฎ / แถบไฟ / ปุ่มเลือก + หน้าแก้กฎเต็มจอ
//    ui_busmap.cpp     แท็บ "บัส"   แผนภาพบัส I2C มีแพ็กเก็ตวิ่ง + สถิติ + ความเร็ว/โหมดรอผล/INT ร่วม
//    ui_sys.cpp        แท็บ "ระบบ"  ปุ่มย่อย จอ / สัมผัส / RGB / เสียง / บอร์ด
//      ui_touch.cpp    ส่วน "สัมผัส" Calibration ตั้งค่าการ Touch / ทดสอบ 9 จุด / ค่าดิบ / วาดเส้น
//      ui_calibrate.cpp หน้า calibrate จอสัมผัส (5 จุด + จุดตรวจสอบ)
//      ui_display.cpp / ui_led.cpp / ui_sound.cpp / ui_system.cpp  ส่วน จอ / RGB / เสียง / บอร์ด
// =====================================================================

#include <lvgl.h>

LV_FONT_DECLARE(font_th_16);
LV_FONT_DECLARE(font_th_24);

constexpr int UI_W = 320;
constexpr int UI_H = 240;
constexpr int UI_TAB_H = 34;  // ความสูงแถบแท็บด้านบน

// ---- ชุดสี (มืด) ----
constexpr uint32_t UI_BG = 0x0B1220;      // พื้นจอ
constexpr uint32_t UI_TABBAR = 0x070D18;  // แถบแท็บ
constexpr uint32_t UI_CARD = 0x16213A;    // การ์ด / ช่องขา
constexpr uint32_t UI_CARD2 = 0x22304D;   // ปุ่มในการ์ด
constexpr uint32_t UI_LINE = 0x2E3D5C;    // เส้นขอบ / ของที่ปิดอยู่
constexpr uint32_t UI_ACCENT = 0x38BDF8;  // ฟ้า: อินพุต / แท็บที่เลือก
constexpr uint32_t UI_OUT = 0xFBBF24;     // ส้มอำพัน
constexpr uint32_t UI_ALARM_BG = 0x3A1A1F;  // พื้นการ์ดที่ค่าเกินขีดเตือน
constexpr uint32_t UI_OK_BG = 0x0F2D2A;     // พื้นช่อง address ที่พบชิป
constexpr uint32_t UI_OUT_BG = 0x3A2E12;   // พื้นช่องเอาต์พุตที่ติด
constexpr uint32_t UI_IN_BG = 0x0F2D44;    // พื้นช่องอินพุตที่ถูกกด
constexpr uint32_t UI_OK = 0x34D399;      // เขียว: เชื่อมต่อแล้ว
constexpr uint32_t UI_BAD = 0xF87171;     // แดง: ไม่พบ / ผิดพลาด
constexpr uint32_t UI_WARN = 0xF97316;    // ส้มเข้ม: ปุ่มอันตราย (calibrate)
constexpr uint32_t UI_TEXT = 0xE6EDF7;
constexpr uint32_t UI_MUTED = 0x8A9BB8;

// ---- สร้างทั้ง UI ----
void ui_init();

// ---- ลำดับแท็บ ----
enum UiTab { UI_TAB_DASH, UI_TAB_ADC, UI_TAB_IO, UI_TAB_RULES, UI_TAB_BUS, UI_TAB_SYS, UI_TAB_COUNT };

// ---- แต่ละแท็บ (parent = หน้าของแท็บ, เลื่อนขึ้นลงได้) ----
void ui_dash_tab(lv_obj_t *tab);
void ui_adc_tab(lv_obj_t *tab);
void ui_io_tab(lv_obj_t *tab);
void ui_pins_tab(lv_obj_t *tab);    // เนื้อหาหน้าตั้งขา (ใน ui_pins_open)
void ui_pins_open();                // หน้าตั้งขาเต็มจอ (ปุ่มเฟืองในแท็บ I/O)
void ui_rules_tab(lv_obj_t *tab);
void ui_busmap_tab(lv_obj_t *tab);
void ui_overview_tab(lv_obj_t *tab);
void ui_detail_tab(lv_obj_t *tab);
void ui_chart_tab(lv_obj_t *tab);
void ui_settings_tab(lv_obj_t *tab);
void ui_touch_tab(lv_obj_t *tab);
void ui_sys_tab(lv_obj_t *tab);
// ส่วนย่อยในแท็บ "ระบบ" (parent = กล่องเรียงแนวตั้ง)
void ui_display_tab(lv_obj_t *tab);
void ui_led_tab(lv_obj_t *tab);
void ui_sound_tab(lv_obj_t *tab);
void ui_system_tab(lv_obj_t *tab);

// ---- เปลี่ยนแท็บ / ส่วนย่อยจากโค้ดหรือ Serial (#tab N, #sub N) ----
void ui_goto_tab(int index);
void ui_scroll_tab(int y);  // test hook: Serial #scroll Y
int ui_active_tab();
void ui_sys_show(int index);
bool ui_sys_built(int index);  // ส่วนย่อยนี้ของแท็บระบบสร้างอยู่ไหม  // 0 จอ, 1 สัมผัส, 2 RGB, 3 เสียง, 4 บอร์ด
void ui_settings_show(int index);  // 0 ADC, 1 ช่อง
void ui_adc_show(int index);       // 0 ค่า, 1 ช่อง, 2 กราฟ, 3 ตั้งค่า
void ui_rules_show(int index);     // 0 กฎ, 1 แถบไฟ, 2 ปุ่มเลือก
void ui_rule_edit(int index);      // เปิดหน้าแก้กฎ 0-7
void ui_pins_refresh();
void ui_rule_text(char *buf, size_t n, int index, bool withTarget);  // "A0 > 1.650 V > P0 ติด"
void ui_pin_tap(uint8_t pin);      // ผู้ใช้แตะขาเอาต์พุต: ยึดคืนจากกฎชั่วคราว + สลับ
void ui_settings_edit(int slot, int field);  // เปิดแป้นตัวเลขของช่องวัด slot (field 0 v1, 1 y1, 2 v2, 3 y2, 4 ต่ำ, 5 สูง)
void ui_busmap_refresh();     // เพิ่งสแกนใหม่ / เปลี่ยนโหมดรอผล
void ui_detail_select(int slot);  // เปิดแท็บ "ช่อง" ที่ช่องวัดนี้ (0-5)
void ui_ads_changed();        // ค่าตั้ง ADS เปลี่ยน (โหมดเดี่ยว/ต่าง, gain, ชื่อ, หน่วย) ให้ทุกแท็บสร้างป้ายใหม่
void ui_overview_refresh();
void ui_detail_refresh();
void ui_chart_refresh();
void ui_settings_refresh();
// ลบหน้าของแท็บ ADC (หยุดตัวจับเวลา) ก่อนล้าง widget ทิ้ง
void ui_overview_close();
void ui_detail_close();
void ui_chart_close();
void ui_settings_close();

// ---- ช่วยแสดงผลช่องวัด (ui_settings.cpp) ----
constexpr int ADS_NAME_COUNT = 14;
constexpr int ADS_UNIT_COUNT = 12;
extern const char *const ADS_NAME_LIST[ADS_NAME_COUNT];
extern const char *const ADS_UNIT_LIST[ADS_UNIT_COUNT];
uint32_t ui_slot_color(int slot);
const char *ui_slot_name(int slot);   // ชื่อที่ผู้ใช้เลือก เช่น "ปุ่มหมุน"
const char *ui_slot_unit(int slot);   // หน่วย เช่น "%"
void ui_fmt_value(char *buf, size_t n, int slot, float value);  // ตัวเลขตามทศนิยมของช่อง (ไม่มีหน่วย)
void ui_fmt_volts(char *buf, size_t n, float volts);            // "1.652 V" / "76.66 mV"
void ui_keypad_open(const char *title, float value, void (*done)(float value));  // แป้นตัวเลขเต็มจอ

// ---- หน้าเต็มจอ (วางบน lv_layer_top ทับทุกอย่าง) ----
void ui_calibrate_start(bool firstTime);
bool ui_calibrate_active();
void ui_display_pattern_start();
void ui_touch_accuracy_start();

// เรียกเมื่อค่าจอเปลี่ยนจากที่อื่น (Serial / ปุ่ม BOOT) ให้สวิตช์บนแท็บจอภาพตรงกับค่าจริง
void ui_display_refresh();

// เรียกเมื่อค่า calibrate เปลี่ยน (แท็บสัมผัส/ระบบอัปเดตตัวเลข)
void ui_touch_refresh();

// ---- ฟังก์ชันช่วย ----
lv_obj_t *ui_card(lv_obj_t *parent, const char *title);  // การ์ดเต็มความกว้าง เรียงลูกแนวตั้ง (flex column)
lv_obj_t *ui_row(lv_obj_t *parent);                       // แถวโปร่งใส เรียงลูกแนวนอน ขึ้นบรรทัดใหม่เมื่อเต็ม
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font = nullptr, lv_color_t color = lv_color_white());
lv_obj_t *ui_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user = nullptr, lv_event_code_t code = LV_EVENT_CLICKED);
lv_obj_t *ui_box(lv_obj_t *parent, uint32_t bg, int radius = 8);  // กล่องสีพื้น ไม่มีขอบ ไม่เลื่อน
void ui_set_text(lv_obj_t *label, const char *text);       // เปลี่ยนข้อความเฉพาะเมื่อไม่เหมือนเดิม (ไม่วาดซ้ำเปล่าๆ)
lv_obj_t *ui_switch_row(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb);  // ข้อความ + สวิตช์ (คืนสวิตช์)
lv_obj_t *ui_overlay();                                   // พื้นเต็มจอบน layer_top (แตะได้ ไม่เลื่อน)
lv_obj_t *ui_crosshair(lv_obj_t *parent, lv_color_t color);  // เป้ากากบาท 41x41 (วางด้วย ui_crosshair_move)
void ui_crosshair_move(lv_obj_t *cross, int x, int y);    // ให้ "กลางเป้า" อยู่ที่ (x, y)
void ui_no_scroll(lv_obj_t *obj);                         // ลากบน widget นี้ไม่ให้หน้าเลื่อนตาม
lv_obj_t *ui_subnav(lv_obj_t *parent, const char *const *names, int count, lv_obj_t **btns, lv_event_cb_t cb);  // แถวปุ่มย่อย
void ui_show_sub(lv_obj_t **boxes, lv_obj_t **btns, int count, int index);  // แสดงส่วนย่อยเดียว (จางเข้า)

// ---- ภาพเคลื่อนไหว (lv_anim) ใช้ outline/transform/bg เท่านั้น ไม่มีเงา (เงากิน RAM ทุกครั้งที่วาด) ----
void ui_pulse(lv_obj_t *obj, bool on, uint32_t color);    // วงแสงกระเพื่อมรอบ obj (ซ้ำไม่สิ้นสุด)
void ui_pop(lv_obj_t *obj);                               // ยุบ-เด้ง เมื่อแตะ
void ui_flash(lv_obj_t *obj, uint32_t from, uint32_t to, uint32_t ms);  // พื้นกะพริบจากสี from กลับเป็น to
void ui_bar_anim(lv_obj_t *bar, int value);               // lv_bar เลื่อนนุ่มไปค่าใหม่ (0-1000)
void ui_slide_in(lv_obj_t *obj, int dy, uint32_t ms);     // เลื่อนเข้าจากล่าง dy px + จางเข้า
uint32_t ui_mix(uint32_t a, uint32_t b, uint8_t t);       // ผสมสี a->b (t 0-255)
