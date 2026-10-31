#pragma once

// =====================================================================
//  hal.h -- ชั้นฮาร์ดแวร์ (ไม่รู้จัก LVGL): ค่าถาวร, จอ, จอสัมผัส, RGB LED, LDR, เสียง, บัส I2C ร่วม, ADS1x15, PCF8574, กฎ
//  UI (ui_*.cpp) เรียกฮาร์ดแวร์ผ่านฟังก์ชันในไฟล์นี้เท่านั้น
// =====================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

namespace hal {

// ---- ค่าที่จำถาวร (NVS) + ข้อมูลระบบ ----
Preferences &prefs();
void begin();
const char *resetReasonText();

// ---- จอ (hal_display.cpp) ----
// ค่าแก้ปัญหาจอที่ปรับได้ตอนรัน (เขียนลงชิปจอผ่านคำสั่ง MADCTL 0x36 และ INVON/INVOFF)
struct DisplayCfg {
    bool invert = false;  // กลับสี (ขาว <-> ดำ)
    bool bgr = false;     // ลำดับสีของแผงจอเป็น BGR (แดง <-> น้ำเงิน)
    bool flipH = false;   // กลับซ้าย-ขวา (กระจกเงา)
    bool flipV = false;   // กลับบน-ล่าง
};
TFT_eSPI &tft();
void displayBegin();                 // tft.init + ใช้ค่าที่บันทึกไว้ + ไฟหลังจอ
const DisplayCfg &displayCfg();
DisplayCfg displayDefaults();        // ค่าเริ่มต้นตามชิปจอที่เลือกใน platformio.ini
void displayApply(const DisplayCfg &c, bool save = true);
uint8_t displayMadctl();             // ค่า MADCTL ที่ส่งให้ชิปจอล่าสุด (ไว้แสดง/ดีบัก)
const char *displayDriverName();     // "ILI9341" / "ST7789"
void setBacklight(int percent, bool save = false);
int backlight();

// ---- จอสัมผัส XPT2046 (hal_touch.cpp) ----
struct RawTouch {
    int16_t x = 0, y = 0, z = 0;
};
// calibrate แบบ affine: x = a*u + b*v + c,  y = d*u + e*v + f   (u, v = ค่าดิบ)
// รองรับแกนสลับ / กลับด้าน / เอียงเล็กน้อย ได้ในสมการเดียว
struct TouchCal {
    bool valid = false;
    float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    float fitRms = 0, fitMax = 0, verifyErr = 0;  // คุณภาพของการ calibrate ครั้งล่าสุด (px)
};
struct TouchState {
    bool down = false;
    int16_t x = 0, y = 0;        // พิกัดจอหลัง calibrate + กรอง
    int16_t rawX = 0, rawY = 0;  // ค่าดิบล่าสุดขณะแตะ
    int16_t z = 0;               // แรงกดล่าสุด
};
void touchBegin();
bool touchReadRaw(RawTouch &out);  // อ่านชิป 1 ครั้ง (true = กำลังแตะ)
void touchPoll(uint32_t now);      // debounce + แปลงพิกัด (เรียกจาก read callback ของ LVGL)
const TouchState &touch();
void touchCapture(bool on);        // true = หน้า calibrate อ่านชิปเอง (LVGL จะไม่เห็นการแตะ)
bool touchCaptured();
const TouchCal &touchCal();
void touchCalSet(const TouchCal &c, bool save = true);
void touchMap(const TouchCal &c, float u, float v, float &x, float &y);
// fit สมการ affine จาก n จุด (least squares): คืน false ถ้าจุดไม่พอ/ซ้ำกัน/ช่วงค่าดิบแคบเกินไป
bool touchCalFit(const float *u, const float *v, const float *x, const float *y, int n, TouchCal &out);
void touchCalMirror(bool h, bool v);  // จอถูกกลับด้าน -> กลับสมการตาม (ไม่ต้อง calibrate ใหม่)

// ---- RGB LED (hal_led.cpp) ค่า 0-255 ต่อสี ----
void ledBegin();
void ledSetRgb(uint8_t r, uint8_t g, uint8_t b);
void ledGetRgb(uint8_t &r, uint8_t &g, uint8_t &b);

// ---- เซนเซอร์แสง ----
int ldrRaw();  // 0-4095 (ยิ่งสว่างยิ่งต่ำ)

// ---- เสียง (hal_sound.cpp) เล่นใน task แยก ไม่ทำให้ UI หน่วง ----
enum class Melody : uint8_t { Startup, Success, Error, Scale, Song, Count };
constexpr const char *VOLUME_NAMES[4] = {"OFF", "LOW", "MID", "HIGH"};
void soundBegin();
void soundClick();
void soundTone(uint16_t hz, uint16_t ms);
void soundSweep(uint16_t fromHz, uint16_t toHz, uint16_t ms);
void soundPlay(Melody m);
void soundStop();
bool soundBusy();
uint16_t soundNowHz();  // ความถี่ที่กำลังเล่น (0 = เงียบ)
uint8_t soundVolume();
void soundSetVolume(uint8_t v, bool save = true);

// ---- ADS1115 / ADS1015 ADC 4 ช่อง บน I2C (hal_ads.cpp) ----
// ADS1115 = 16 บิต สูงสุด 860 SPS / ADS1015 = 12 บิต สูงสุด 3300 SPS (register เหมือนกัน ต่างที่ความละเอียด+อัตรา)
// "ช่องวัด" (slot) 6 ช่อง: 0-3 = A0-A3 เทียบ GND (single-ended), 4 = A0-A1, 5 = A2-A3 (differential)
constexpr uint8_t ADS_SLOTS = 6;
constexpr uint8_t ADS_GAINS = 6;
constexpr uint8_t ADS_RATES = 8;
constexpr uint8_t ADS_AVGS = 5;
constexpr float ADS_FSR[ADS_GAINS] = {6.144f, 4.096f, 2.048f, 1.024f, 0.512f, 0.256f};  // ช่วงวัด +-V ตาม PGA
constexpr uint16_t ADS1115_SPS[ADS_RATES] = {8, 16, 32, 64, 128, 250, 475, 860};
constexpr uint16_t ADS1015_SPS[ADS_RATES] = {128, 250, 490, 920, 1600, 2400, 3300, 3300};
constexpr uint8_t ADS_AVG_N[ADS_AVGS] = {1, 2, 4, 8, 16};
constexpr const char *ADS_SLOT_NAMES[ADS_SLOTS] = {"A0", "A1", "A2", "A3", "A0-A1", "A2-A3"};

enum class AdsChip : uint8_t { None, ADS1115, ADS1015 };

// ค่าตั้งของชิป (บันทึกใน NVS key "adscfg")
struct AdsCfg {
    uint8_t version = 1;
    uint8_t chipSel = 0;   // 0 อัตโนมัติ, 1 ADS1115, 2 ADS1015
    uint8_t addrSel = 0;   // 0 อัตโนมัติ (0x48-0x4B), หรือ address ที่บังคับ
    uint8_t gain = 1;      // ดัชนี ADS_FSR (1 = +-4.096 V เหมาะกับไฟ 3.3 V)
    uint8_t rate = 4;      // ดัชนีอัตราสุ่ม (ADS1115 128 SPS / ADS1015 1600 SPS)
    uint8_t avg = 2;       // ดัชนี ADS_AVG_N (2 = เฉลี่ย 4 ค่า)
    bool diff = false;     // true = วัดแบบต่าง A0-A1 / A2-A3
    bool rdy = false;      // true = รอผลแปลงจากขา ALRT/RDY (IO35) / false = อ่านบิต OS (polling)
    bool beep = true;      // เสียงเตือนเมื่อค่าเกินขีด
};

// การแปลงหน่วยของแต่ละช่องวัด: เส้นตรงผ่าน 2 จุด (v1 -> y1), (v2 -> y2)  (บันทึกใน NVS key "adsch")
struct AdsChCfg {
    uint8_t name = 0;      // ดัชนี ADS_NAME_LIST (ui_settings.cpp)
    uint8_t unit = 0;      // ดัชนี ADS_UNIT_LIST
    float v1 = 0, y1 = 0, v2 = 1, y2 = 1;
    bool loOn = false, hiOn = false;
    float lo = 0, hi = 0;  // ขีดเตือน (หน่วยที่แปลงแล้ว)
};

struct AdsSlot {
    int16_t raw16 = 0;      // ผลแปลงแบบ 16 บิต (ADS1015 เลื่อนซ้ายมาแล้ว) เฉลี่ยแล้ว
    int16_t counts = 0;     // ค่าดิบตามชิปจริง (ADS1015 = 12 บิต -2048..2047)
    float volts = 0;
    float value = 0;        // หลังแปลงหน่วย
    float vMin = 0, vMax = 0;
    double vSum = 0;
    uint32_t n = 0;         // จำนวนครั้งที่อ่าน (ตั้งแต่รีเซ็ตสถิติ)
    uint8_t alarm = 0;      // 0 ปกติ, 1 ต่ำกว่าขีด, 2 สูงกว่าขีด
    bool clip = false;      // ชนขอบช่วงวัด (ควรลด gain)
};

struct AdsState {
    bool found = false;
    bool demo = false;         // โหมดจำลอง: สร้างค่าเองโดยไม่มีชิป
    AdsChip chip = AdsChip::None;
    AdsChip detected = AdsChip::None;  // ผลตรวจจากเวลาแปลง
    uint8_t addr = 0;
    uint16_t detectUs = 0;     // เวลาแปลงที่วัดได้ตอนตรวจชิป (us)
    uint32_t reads = 0, errors = 0, rdyTimeouts = 0;
    uint16_t readsPerSec = 0;
    uint8_t busCount = 0;
    uint8_t bus[16] = {};
};

void adsBegin();                 // สแกนหา ADS + ค่าตั้ง (เรียกหลัง busBegin, งานอ่านอยู่ใน task "i2c")
bool adsScan();                  // สแกนบัส + หา ADS1x15 ตามค่าตั้ง
const AdsState &ads();
const AdsCfg &adsCfg();
void adsSetCfg(const AdsCfg &c, bool save = true);  // ใช้ค่าใหม่ทันที (ถ้าเปลี่ยนชิป/address จะสแกนใหม่)
const AdsChCfg &adsChCfg(uint8_t slot);
void adsSetChCfg(uint8_t slot, const AdsChCfg &c, bool save = true);
const AdsSlot &adsSlot(uint8_t slot);
void adsResetStats(int slot = -1);  // -1 = ทุกช่อง
void adsSetDemo(bool on);
float adsScale(uint8_t slot, float volts);  // แปลงแรงดัน -> หน่วยของช่อง
void adsRange(uint8_t slot, float &lo, float &hi);  // ช่วงค่าที่เป็นไปได้ (หน่วยของช่อง) ไว้ทำเกจ/แถบ
uint8_t adsDecimals(uint8_t slot);  // ทศนิยมที่ควรแสดง (ตามช่วงค่า)
uint16_t adsSps();               // อัตราสุ่มจริงของชิปที่ใช้อยู่
float adsLsbVolts();             // แรงดันต่อ 1 ขั้นของชิป/ช่วงวัดตอนนี้
uint8_t adsFirstSlot();          // 0 (เดี่ยว) หรือ 4 (ต่าง)
uint8_t adsSlotCount();          // 4 หรือ 2
const char *adsChipName(AdsChip c);
uint32_t adsService(uint32_t now);  // เรียกจาก task "i2c" เท่านั้น: เดินงานแปลง 1 ขั้น คืนเวลาที่ควรหลับ (ms)
bool adsBusy();                  // กำลังรอผลแปลงอยู่ (ขา RDY ยังไม่ควรลง)

// ---- บัส I2C ร่วม + ตัวจัดคิว (hal_bus.cpp) ----
// ทุกการคุยกับชิปผ่าน busWrite/busRead ที่ถือ mutex ตัวเดียวกัน และนับสถิติ/เก็บ "แพ็กเก็ต" ไว้ให้ UI วาดภาพเคลื่อนไหว
// task "i2c" บน core 0 วนเรียก adsService -> pcfService -> rulesService (LVGL อยู่ใน loop() บน core 1)
enum class BusDev : uint8_t { Other, Ads, Pcf };
struct BusPacket {
    uint8_t addr = 0;
    bool write = false;  // true = เขียน (CYD -> ชิป) / false = อ่าน (ชิป -> CYD)
    bool ok = true;      // false = ไม่มี ACK / timeout
};
struct BusState {
    uint32_t tx = 0, errors = 0;          // ธุรกรรมทั้งหมด / ที่พลาด
    uint32_t adsTx = 0, pcfTx = 0, adsErr = 0, pcfErr = 0;
    uint16_t txPerSec = 0, adsPerSec = 0, pcfPerSec = 0;
    uint8_t loadPct = 0;                  // % ของเวลาที่บัสไม่ว่าง (1 วินาทีล่าสุด)
    uint32_t clockHz = 0;
    uint8_t count = 0;                    // อุปกรณ์ที่สแกนพบ
    uint8_t addr[16] = {};
    uint32_t scans = 0;
};
struct BusGuard {  // ถือ mutex ของบัสตลอดช่วงชีวิตของตัวแปร (ซ้อนกันได้ในงานเดียวกัน)
    BusGuard();
    ~BusGuard();
};
void busBegin();                       // Wire + mutex + ความเร็วที่บันทึกไว้
void busStartTask();                   // เริ่ม task "i2c" (หลัง adsBegin/pcfBegin/rulesBegin)
bool busWrite(uint8_t addr, const uint8_t *data, uint8_t n);
bool busRead(uint8_t addr, uint8_t *data, uint8_t n);
bool busProbe(uint8_t addr);           // มีอุปกรณ์ตอบ ACK ที่ address นี้ไหม (ไม่นับเป็นธุรกรรม)
uint8_t busScan();                     // สแกน 0x08-0x77 เก็บใน bus().addr
const BusState &bus();
BusDev busDevOf(uint8_t addr);
void busSetFast(bool fast, bool save = true);  // false = 100 kHz / true = 400 kHz
bool busFast();
bool busIntLow();                      // ขา INT ร่วม IO35 ตอนนี้ LOW ไหม
// คัดลอกแพ็กเก็ตใหม่ตั้งแต่หมายเลข since (คืนหมายเลขถัดไป) ถ้าตามไม่ทันจะข้ามไปของล่าสุด
uint32_t busPackets(uint32_t since, BusPacket *out, uint8_t max, uint8_t &got);
void busNote(uint8_t addr, bool write, bool ok, uint32_t us);  // ใช้ภายใน hal

// ---- PCF8574 I/O expander (hal_pcf8574.cpp) ----
// ขาของ PCF8574 เป็นแบบ quasi-bidirectional: เขียน 0 = ดึงลง GND แรง (sink ~25 mA)
// เขียน 1 = ปล่อยลอย มี pull-up ภายในอ่อนๆ ~100 uA  -> ขาที่จะใช้เป็นอินพุตต้องเขียน 1 ไว้เสมอ
struct PcfState {
    bool found = false;        // พบ PCF8574 บนบัสและสื่อสารได้
    uint8_t addr = 0;          // address 7 บิต (0x20-0x27 หรือ PCF8574A 0x38-0x3F)
    uint8_t outByte = 0xFF;    // ไบต์ที่เขียนไปที่พอร์ตล่าสุด
    uint8_t inByte = 0xFF;     // ไบต์ที่อ่านกลับล่าสุด = ระดับจริงของทั้ง 8 ขา
    uint8_t dirMask = 0xF0;    // บิต = 1 -> ขานั้นเป็นอินพุต (ค่าเริ่มต้น P4-P7 อินพุต)
    uint8_t lowMask = 0xFF;    // บิต = 1 -> "ทำงานเมื่อ LOW" (LED ต่อแบบ sink / ปุ่มกดลง GND)
    uint8_t outOn = 0x00;      // สถานะ "ติด/ดับ" ของขาเอาต์พุต (ตรรกะ ไม่ใช่ระดับไฟ)
    bool intMode = false;      // true = อ่านเมื่อขา INT (IO35) ลง LOW / false = polling
    uint32_t reads = 0, errors = 0, ints = 0;
    uint32_t intStuckMs = 0;   // INT ค้าง LOW นานเท่าไรแล้ว (ไม่มี pull-up / สายหลุด)
};
void pcfBegin();
void pcfService(uint32_t now);        // เรียกจาก task "i2c": อ่านขาเข้า + สแกนใหม่เองเมื่อหาไม่เจอ
bool pcfScan();                       // สแกนบัส I2C แล้วเลือก PCF8574 ตัวแรก
const PcfState &pcf();
bool pcfIsInput(uint8_t pin);
bool pcfActive(uint8_t pin);          // ขาเอาต์พุต = ติดอยู่ / ขาอินพุต = กำลังกด (คิด lowMask ให้แล้ว)
uint8_t pcfInputActiveMask();         // บิตของขาอินพุตที่กำลังกดอยู่
void pcfSetOutput(uint8_t pin, bool on);
void pcfToggle(uint8_t pin);
void pcfSetAllOutputs(bool on);
void pcfSetOutMask(uint8_t onMask, bool log = false);  // ตั้งเอาต์พุตทั้งพอร์ตในครั้งเดียว (บิตที่เป็นอินพุตถูกข้าม)
void pcfSetDir(uint8_t pin, bool input, bool save = true);
void pcfSetActiveLow(uint8_t pin, bool low, bool save = true);
void pcfSetIntMode(bool on, bool save = true);
void pcfResetConfig();                // กลับเป็น P0-P3 เอาต์พุต / P4-P7 อินพุต / ทำงานเมื่อ LOW ทุกขา
// เรียกทุกครั้งที่ขาอินพุตเปลี่ยน (pin, active) จาก task "i2c" (ห้ามเรียก lv_* ใน callback)
void pcfOnInputChange(void (*cb)(uint8_t pin, bool active));

// ---- กฎข้ามชิป (hal_rules.cpp): ค่า ADS -> ขา PCF / ปุ่ม PCF -> ค่าตั้ง ADS ----
constexpr uint8_t RULES_MAX = 8;
enum class RuleAct : uint8_t { On, Off, Blink, Toggle, Count };
constexpr const char *RULE_ACT_NAMES[4] = {"ติด", "ดับ", "กะพริบ", "สลับ"};
struct Rule {
    bool en = false;
    uint8_t slot = 0;      // ช่องวัด 0-5 (ADS_SLOT_NAMES)
    bool above = true;     // true: ทำงานเมื่อค่า > ขีด / false: เมื่อค่า < ขีด
    float thr = 1.65f;     // ขีด (หน่วยของช่องหลังแปลงแล้ว)
    float hyst = 0.05f;    // ช่วงหน่วงกลับ (ฮิสเทอรีซิส) กันไฟกะพริบตอนค่าแกว่งรอบขีด
    uint16_t holdMs = 0;   // เงื่อนไขต้องเป็นจริง/เท็จต่อเนื่องนานเท่านี้ก่อนเปลี่ยนสถานะ
    uint8_t pin = 0;       // ขาเอาต์พุตของ PCF8574 (P0-P7)
    uint8_t act = 0;       // RuleAct
};
enum class BarMode : uint8_t { Bar, Dot, Vu, Count };
enum class InAct : uint8_t { None, NextCh, GainUp, GainDown, Hold, Diff, Count };
constexpr const char *IN_ACT_NAMES[6] = {"ไม่ใช้", "ช่องถัดไป", "gain +", "gain -", "ค้างค่า", "เดี่ยว/ต่าง"};
struct RulesCfg {
    uint8_t version = 1;
    Rule rule[RULES_MAX];
    bool barOn = false;    // แถบไฟ: ใช้ขาเอาต์พุตทุกขาแสดงค่าของช่องที่เลือก
    uint8_t barMode = 0;   // BarMode
    bool inOn = false;     // ให้ปุ่ม P4-P7 คุม ADS
    uint8_t inAct[4] = {1, 2, 4, 5};  // InAct ของ P4..P7
    uint8_t sel = 0;       // ช่องวัดที่เลือก (แถบไฟ / ปุ่มเลือก)
};
struct RulesState {
    bool active[RULES_MAX] = {};
    uint32_t fired[RULES_MAX] = {};  // จำนวนครั้งที่กฎเริ่มทำงาน (UI ใช้ดูว่าเพิ่งทำงานไหม)
    uint32_t firedAt[RULES_MAX] = {};
    bool hold = false;               // ค้างค่า (จากปุ่ม)
    float heldValue = 0;
    float barFrac = 0;               // 0..1 ค่าที่ใช้กับแถบไฟ
    uint8_t barLit = 0, barTotal = 0, barPeak = 0;
    uint8_t ownMask = 0;             // ขาเอาต์พุตที่กฎ/แถบไฟคุมอยู่
    uint8_t manualMask = 0;          // ขาที่ผู้ใช้แตะเอง (ยึดคืนจนกว่ากฎจะเปลี่ยนสถานะ)
    uint8_t ruleOfPin[8] = {};       // กฎล่าสุดที่คุมขานี้ (1-8, 0 = ไม่มี) / 9 = แถบไฟ
    uint32_t cfgSeq = 0;             // เพิ่มขึ้นเมื่อปุ่มเปลี่ยนค่าตั้ง ADS (UI ต้องสร้างป้ายใหม่)
    uint32_t inSeq = 0;              // เพิ่มขึ้นเมื่อมีการกดปุ่มที่ใช้งาน
    uint8_t inLast = 0;              // ปุ่มล่าสุดที่กด (4-7)
};
void rulesBegin();
void rulesService(uint32_t now);     // เรียกจาก task "i2c"
const RulesCfg &rulesCfg();
const RulesState &rules();
void rulesSetRule(uint8_t i, const Rule &r, bool save = true);
void rulesSetBar(bool on, uint8_t mode, bool save = true);
void rulesSetInput(bool on, const uint8_t act[4], bool save = true);
void rulesSelect(uint8_t slot, bool save = true);
void rulesHold(bool on);
void rulesManual(uint8_t pin);       // ผู้ใช้สั่งขานี้เอง: กฎหยุดคุมชั่วคราว
void rulesResetDefaults();
uint8_t rulesNextSlot(uint8_t slot); // ช่องถัดไปในโหมดปัจจุบัน (เดี่ยว/ต่าง)

}  // namespace hal
