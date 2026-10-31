#include <string.h>
#include <math.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  hal_ads.cpp -- ADS1115 (16 บิต) / ADS1015 (12 บิต) ADC 4 ช่องผ่าน I2C (ไม่รู้จัก LVGL)
//
//  register ของชิป (address pointer = ไบต์แรกที่เขียน):
//    0x00 Conversion  ผลแปลงล่าสุด (16 บิต, ADS1015 ชิดซ้าย 12 บิต)
//    0x01 Config      OS | MUX(3) | PGA(3) | MODE | DR(3) | COMP_MODE | COMP_POL | COMP_LAT | COMP_QUE(2)
//    0x02 Lo_thresh / 0x03 Hi_thresh  ใช้ทำขา ALRT/RDY เป็น "แปลงเสร็จ" (Hi บิตบนสุด = 1, Lo บิตบนสุด = 0)
//
//  ทำงานแบบ single-shot ทีละช่อง: เขียน Config (OS=1 = เริ่มแปลง) -> รอ -> อ่าน Conversion -> ช่องถัดไป
//  รอผลได้ 2 แบบ: อ่านบิต OS ซ้ำ (polling) หรือรอขา ALRT/RDY ลง LOW (IO35)
//  IO35 ใช้ร่วมกับ INT ของ PCF8574 (wired-OR) ขาลง LOW จึงอาจเป็นฝีมือ PCF ก็ได้
//    -> เห็น LOW แล้วต้องยืนยันด้วยบิต OS อีกครั้งเสมอ (ไม่อ่านผลแปลงทันทีเหมือนแล็บ ADS เดี่ยว)
//  adsService() ถูกเรียกจาก task "i2c" (hal_bus.cpp) ร่วมกับ PCF8574 และกฎ
//  UI เรียกฟังก์ชันตั้งค่าได้ตลอด: ทุกฟังก์ชันที่แตะ I2C / ค่าตั้ง ถือ BusGuard (mutex ของบัส) ก่อน
// =====================================================================

namespace hal {

namespace {

constexpr uint8_t REG_CONV = 0x00, REG_CFG = 0x01, REG_LO = 0x02, REG_HI = 0x03;
constexpr uint16_t CFG_OS = 0x8000;      // เขียน 1 = เริ่มแปลง / อ่านได้ 1 = แปลงเสร็จแล้ว
constexpr uint16_t CFG_MODE_SS = 0x0100; // single-shot (แปลงครั้งเดียวแล้วพัก ประหยัดไฟ)
constexpr uint16_t CFG_QUE_OFF = 0x0003; // ปิด comparator (ขา ALRT/RDY ลอย)
// MUX ของแต่ละช่องวัด: 100-111 = AINx เทียบ GND, 000 = AIN0-AIN1, 011 = AIN2-AIN3
constexpr uint8_t SLOT_MUX[ADS_SLOTS] = {0b100, 0b101, 0b110, 0b111, 0b000, 0b011};

AdsState gS;
AdsCfg gCfg;
AdsChCfg gCh[ADS_SLOTS];
AdsSlot gSlot[ADS_SLOTS];

int16_t gRing[ADS_SLOTS][16];  // ค่าดิบล่าสุดสำหรับเฉลี่ยเคลื่อนที่
uint8_t gRingPos[ADS_SLOTS], gRingFill[ADS_SLOTS];

uint8_t gErrRun = 0;
uint32_t gLastScan = 0, gLastDemo = 0, gRateSince = 0, gRateCount = 0;
bool gBusy = false;     // กำลังรอผลแปลง
uint8_t gCur = 0;       // ช่องที่กำลังแปลง
uint32_t gStartUs = 0, gConvUs = 0;
bool gRdyBad = false;   // รอบนี้ขา RDY ใช้ไม่ได้ -> อ่านบิต OS

// ---------- I2C ระดับ register (ผ่าน busWrite/busRead ที่นับสถิติ + ส่งแพ็กเก็ตให้ภาพเคลื่อนไหว) ----------
bool writeReg(uint8_t reg, uint16_t v)
{
    const uint8_t b[3] = {reg, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF)};
    return busWrite(gS.addr, b, 3);
}

bool readReg(uint8_t reg, uint16_t &v)
{
    // ADS1x15: เขียน address pointer 1 ไบต์ แล้วอ่าน 2 ไบต์ (= 2 ธุรกรรมบนบัส)
    uint8_t b[2];
    if (!busWrite(gS.addr, &reg, 1)) return false;
    if (!busRead(gS.addr, b, 2)) return false;
    v = static_cast<uint16_t>(b[0] << 8 | b[1]);
    return true;
}

void noteResult(bool ok)
{
    if (ok) {
        gErrRun = 0;
        return;
    }
    gS.errors++;
    if (++gErrRun >= app::ADS_MAX_ERRORS && gS.found) {
        gS.found = false;
        gBusy = false;
        Serial.printf("[ADS] lost 0x%02X (%u errors in a row) - check wiring\n", gS.addr, gErrRun);
    }
}

bool is1015()
{
    return gS.chip == AdsChip::ADS1015;
}

uint16_t configFor(uint8_t slot)
{
    uint16_t c = CFG_OS | CFG_MODE_SS;
    c |= static_cast<uint16_t>(SLOT_MUX[slot]) << 12;
    c |= static_cast<uint16_t>(gCfg.gain) << 9;
    c |= static_cast<uint16_t>(gCfg.rate) << 5;
    c |= gCfg.rdy ? 0x0000 : CFG_QUE_OFF;  // RDY: COMP_QUE=00 (แจ้งหลังแปลงเสร็จ 1 ครั้ง), COMP_POL=0 (ลง LOW)
    return c;
}

// เขียน threshold ให้ขา ALRT/RDY ทำหน้าที่ "แปลงเสร็จ" (ข้อมูลจาก datasheet หัวข้อ Conversion-Ready Pin)
bool applyRdyRegs()
{
    if (!gS.found) return false;
    const bool ok = writeReg(REG_HI, 0x8000) && writeReg(REG_LO, 0x0000);
    noteResult(ok);
    return ok;
}

// ---------- ตรวจชิปที่ address หนึ่ง: ADS1x15 จริงไหม + เป็นรุ่นไหน ----------
AdsChip probe(uint8_t addr, uint16_t &us)
{
    const uint8_t keep = gS.addr;
    gS.addr = addr;
    // แปลง A0 ที่อัตราสูงสุด (DR=111): ADS1015 3300 SPS ~0.3 ms / ADS1115 860 SPS ~1.2 ms
    const uint16_t cfg = CFG_OS | CFG_MODE_SS | (0b100 << 12) | (1 << 9) | (7 << 5) | CFG_QUE_OFF;
    AdsChip result = AdsChip::None;
    if (writeReg(REG_CFG, cfg)) {
        const uint32_t t0 = micros();
        uint16_t v = 0;
        bool done = false;
        while (micros() - t0 < 5000) {
            if (!readReg(REG_CFG, v)) break;
            if (v & CFG_OS) {
                done = true;
                break;
            }
        }
        us = static_cast<uint16_t>(micros() - t0);
        // อ่านกลับต้องได้ค่าเดิม (ยกเว้นบิต OS) ไม่งั้นเป็นอุปกรณ์อื่นที่ address ชนกัน (เช่น LM75, PCF8591 ที่ 0x48)
        if (done && (v & 0x7FFF) == (cfg & 0x7FFF)) result = us < app::ADS_DETECT_SPLIT_US ? AdsChip::ADS1015 : AdsChip::ADS1115;
    }
    gS.addr = keep;
    return result;
}

void updateConvTime()
{
    const uint16_t sps = adsSps();
    gConvUs = 1000000UL / sps + 1000000UL / sps / 10 + 50;  // + 10% (oscillator ภายในคลาดได้ถึง 10%)
}

// ---------- ผลแปลง 1 ค่า -> เฉลี่ย -> แรงดัน -> หน่วย -> สถิติ -> เตือน ----------
void onSample(uint8_t slot, int16_t raw16)
{
    gRing[slot][gRingPos[slot]] = raw16;
    gRingPos[slot] = static_cast<uint8_t>((gRingPos[slot] + 1) & 15);
    const uint8_t want = ADS_AVG_N[gCfg.avg];
    if (gRingFill[slot] < 16) gRingFill[slot]++;
    const uint8_t n = gRingFill[slot] < want ? gRingFill[slot] : want;
    int32_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += gRing[slot][(gRingPos[slot] + 16 - 1 - i) & 15];

    AdsSlot &s = gSlot[slot];
    s.raw16 = static_cast<int16_t>(sum / n);
    s.counts = is1015() ? static_cast<int16_t>(s.raw16 >> 4) : s.raw16;
    s.volts = s.raw16 * ADS_FSR[gCfg.gain] / 32768.0f;
    s.value = adsScale(slot, s.volts);
    s.clip = raw16 >= 32752 || raw16 <= -32768;  // ADS1015 ค่าสูงสุด 0x7FF0 = 32752
    if (s.n == 0) {
        s.vMin = s.vMax = s.volts;
    } else {
        if (s.volts < s.vMin) s.vMin = s.volts;
        if (s.volts > s.vMax) s.vMax = s.volts;
    }
    s.vSum += s.volts;
    s.n++;

    const AdsChCfg &c = gCh[slot];
    uint8_t alarm = 0;
    if (c.hiOn && s.value > c.hi) alarm = 2;
    else if (c.loOn && s.value < c.lo) alarm = 1;
    if (alarm != s.alarm) {
        s.alarm = alarm;
        Serial.printf("[ADS] %s alarm %s (%.3f)\n", ADS_SLOT_NAMES[slot], alarm == 2 ? "HIGH" : alarm == 1 ? "LOW" : "cleared", s.value);
        if (alarm != 0 && gCfg.beep) soundTone(alarm == 2 ? 2200 : 1200, 120);
    }

    gS.reads++;
    gRateCount++;
}

uint8_t nextSlot(uint8_t slot)
{
    const uint8_t first = adsFirstSlot(), count = adsSlotCount();
    if (slot < first || slot >= first + count - 1) return first;
    return static_cast<uint8_t>(slot + 1);
}

bool startConversion(uint8_t slot)
{
    gCur = slot;
    const bool ok = writeReg(REG_CFG, configFor(slot));
    noteResult(ok);
    if (!ok) return false;
    gStartUs = micros();
    gBusy = true;
    // เพิ่งเริ่มแปลง ขา RDY ต้องยังเป็น HIGH (pull-up) ถ้า LOW อยู่แล้ว = ขาลอย/ไม่มี pull-up/ไม่ได้ต่อ
    // นับเป็น "RDY พลาด" แล้วรอบนี้อ่านบิต OS แทน ไม่งั้นจะอ่านผลก่อนแปลงเสร็จโดยไม่รู้ตัว
    gRdyBad = gCfg.rdy && digitalRead(cyd::ADS_RDY_PIN) == LOW;
    if (gRdyBad) gS.rdyTimeouts++;
    return true;
}

bool readConversion()
{
    gBusy = false;
    uint16_t v = 0;
    const bool ok = readReg(REG_CONV, v);
    noteResult(ok);
    if (ok) onSample(gCur, static_cast<int16_t>(v));
    return ok;
}

// ---------- โหมดจำลอง: คลื่นสวยๆ ไว้ลอง UI โดยไม่มีชิป ----------
void demoTick(uint32_t now)
{
    if (now - gLastDemo < app::ADS_DEMO_MS) return;
    gLastDemo = now;
    const float t = now / 1000.0f;
    const float noise = (random(-100, 101)) / 100.0f;
    float v[4];
    v[0] = 1.65f + 1.2f * sinf(t * 2.0f * PI / 8.0f);                // ปุ่มหมุนหมุนไปมา
    v[1] = 0.412f + 0.003f * noise;                                   // LM35 ~41 องศา
    const float tri = fmodf(t, 6.0f) / 3.0f;
    v[2] = 0.5f + 2.5f * (tri < 1.0f ? tri : 2.0f - tri);              // สามเหลี่ยม 0.5-3.0 V
    v[3] = 3.30f + 0.004f * noise;                                    // ไฟ 3.3 V
    const float fsr = ADS_FSR[gCfg.gain];
    const uint8_t first = adsFirstSlot(), count = adsSlotCount();
    for (uint8_t slot = first; slot < first + count; slot++) {
        float volts = slot < 4 ? v[slot] : (slot == 4 ? v[0] - v[1] : v[2] - v[3]);
        if (volts > fsr) volts = fsr;
        if (volts < -fsr) volts = -fsr;
        int32_t raw = lroundf(volts / fsr * 32768.0f);
        if (raw > 32767) raw = 32767;
        if (raw < -32768) raw = -32768;
        if (is1015()) raw &= ~0xF;  // ADS1015 มีแค่ 12 บิต
        onSample(slot, static_cast<int16_t>(raw));
    }
}

void loadCfg()
{
    AdsCfg c;
    if (prefs().isKey("adscfg") && prefs().getBytesLength("adscfg") == sizeof(AdsCfg)) prefs().getBytes("adscfg", &c, sizeof(c));
    if (c.version != 1 || c.gain >= ADS_GAINS || c.rate >= ADS_RATES || c.avg >= ADS_AVGS || c.chipSel > 2) c = AdsCfg();
    gCfg = c;
    if (prefs().isKey("adsch") && prefs().getBytesLength("adsch") == sizeof(gCh)) prefs().getBytes("adsch", gCh, sizeof(gCh));
}

}  // namespace

// =====================================================================

void adsBegin()
{
    loadCfg();  // Wire + IO35 เริ่มแล้วใน busBegin() / งานอ่านค่าอยู่ใน task "i2c" (busStartTask)
    adsScan();
}

bool adsScan()
{
    BusGuard g;
    gLastScan = millis();
    gBusy = false;
    busScan();
    const BusState &b = bus();
    gS.busCount = b.count;
    memcpy(gS.bus, b.addr, sizeof(gS.bus));
    const bool was = gS.found;
    gS.found = false;
    for (uint8_t a = 0x48; a <= 0x4B; a++) {
        if (gCfg.addrSel != 0 && a != gCfg.addrSel) continue;
        bool present = false;
        for (uint8_t i = 0; i < gS.busCount; i++) present |= gS.bus[i] == a;
        if (!present) continue;
        uint16_t us = 0;
        const AdsChip c = probe(a, us);
        if (c == AdsChip::None) {
            Serial.printf("[ADS] 0x%02X answers but is not an ADS1x15\n", a);
            continue;
        }
        gS.addr = a;
        gS.detected = c;
        gS.detectUs = us;
        gS.found = true;
        break;
    }
    if (!gS.found) {
        if (was || gS.chip != AdsChip::None) Serial.println("[ADS] not found");
        if (!gS.demo) gS.chip = AdsChip::None;
        return false;
    }
    gS.chip = gCfg.chipSel == 1 ? AdsChip::ADS1115 : gCfg.chipSel == 2 ? AdsChip::ADS1015 : gS.detected;
    gErrRun = 0;
    updateConvTime();
    if (gCfg.rdy) applyRdyRegs();
    Serial.printf("[ADS] found %s at 0x%02X (detected %s, conversion %u us) | %u device(s) on bus\n", adsChipName(gS.chip), gS.addr,
                  adsChipName(gS.detected), gS.detectUs, gS.busCount);
    return true;
}

namespace {

// รอผลแปลง 1 ครั้งที่สั่งไปแล้ว (เรียกขณะถือ lock) คืน true ถ้าอ่านได้หรือควรเริ่มใหม่
bool waitResult()
{
    const uint32_t el = micros() - gStartUs;
    if (gCfg.rdy && !gRdyBad) {
        // IO35 ใช้ร่วมกับ INT ของ PCF8574: LOW = "มีใครสักตัวเรียก" -> ยืนยันด้วยบิต OS ข้างล่างเสมอ
        if (digitalRead(cyd::ADS_RDY_PIN) != LOW) {
            if (el < gConvUs * 2 + 2000) return false;  // รอขา RDY ต่ออีกนิด (ไม่แตะบัสเลย)
            gS.rdyTimeouts++;  // ขา RDY ไม่ลง LOW: ไม่ได้ต่อสาย / ไม่มี pull-up -> อ่านบิต OS แทน
        }
    }
    uint16_t cfg = 0;
    const bool ok = readReg(REG_CFG, cfg);
    noteResult(ok);
    if (!ok) return true;
    if (cfg & CFG_OS) return readConversion(), true;
    if (el > gConvUs * 3 + 5000) {
        gBusy = false;  // ไม่เสร็จสักที นับเป็นข้อผิดพลาดแล้วเริ่มใหม่
        noteResult(false);
        return true;
    }
    return false;
}

}  // namespace

uint32_t adsService(uint32_t now)
{
    BusGuard g;
    if (now - gRateSince >= 1000) {
        gS.readsPerSec = static_cast<uint16_t>(gRateCount * 1000UL / (now - gRateSince));
        gRateCount = 0;
        gRateSince = now;
    }
    if (gS.demo) {
        demoTick(now);
        return app::ADS_DEMO_MS;
    }
    if (!gS.found) {
        if (now - gLastScan >= app::ADS_RESCAN_MS) adsScan();
        return 100;
    }
    if (gBusy) {
        // ยังไม่ถึงเวลาแปลงเสร็จ: ไม่ต้องถามชิป (ทุกคำถาม = 2 ธุรกรรมบนบัสที่ PCF ใช้ร่วม) หลับรอให้พอดีเวลา
        const uint32_t el = micros() - gStartUs;
        if (el + 500 < gConvUs) return (gConvUs - el) / 1000 ? (gConvUs - el) / 1000 : 1;
        if (!waitResult()) return 1;  // เลยเวลาแล้วยังไม่เสร็จ (oscillator ช้า): ดูใหม่ใน 1 ms
    }
    // อ่านเสร็จแล้ว (หรือยังไม่ได้เริ่ม) -> เริ่มช่องถัดไปทันที ขา RDY จะกลับเป็น HIGH ก่อน PCF มาดูขา INT
    if (!gS.found || !startConversion(nextSlot(gCur))) return 1;
    // หลับจนใกล้เวลาแปลงเสร็จ ระหว่างนี้บัสว่าง (task i2c จะหลับไม่เกิน I2C_TASK_MAX_SLEEP_MS เพื่อดู PCF)
    return gConvUs / 1000;
}

bool adsBusy()
{
    return gBusy;
}

const AdsState &ads()
{
    return gS;
}

const AdsCfg &adsCfg()
{
    return gCfg;
}

void adsSetCfg(const AdsCfg &c, bool save)
{
    BusGuard g;
    const bool rescan = c.chipSel != gCfg.chipSel || c.addrSel != gCfg.addrSel;
    const bool modeChanged = c.diff != gCfg.diff || c.gain != gCfg.gain;
    gCfg = c;
    gCfg.version = 1;
    if (save) prefs().putBytes("adscfg", &gCfg, sizeof(gCfg));
    if (rescan) adsScan();
    gBusy = false;
    if (gS.found) {
        if (gCfg.chipSel != 0) gS.chip = gCfg.chipSel == 1 ? AdsChip::ADS1115 : AdsChip::ADS1015;
        else gS.chip = gS.detected;
        updateConvTime();
        if (gCfg.rdy) applyRdyRegs();
    } else if (gS.demo) {
        gS.chip = gCfg.chipSel == 2 ? AdsChip::ADS1015 : AdsChip::ADS1115;
    }
    if (modeChanged) adsResetStats();
    for (uint8_t i = 0; i < ADS_SLOTS; i++) gRingFill[i] = 0;  // gain/rate เปลี่ยน -> ค่าเก่าในค่าเฉลี่ยใช้ไม่ได้
    Serial.printf("[ADS] cfg chip=%u addr=0x%02X gain=+-%.3fV rate=%uSPS avg=%u %s %s beep=%d\n", gCfg.chipSel, gCfg.addrSel, ADS_FSR[gCfg.gain],
                  adsSps(), ADS_AVG_N[gCfg.avg], gCfg.diff ? "DIFF" : "SINGLE", gCfg.rdy ? "RDY(IO35)" : "POLL", gCfg.beep);
}

const AdsChCfg &adsChCfg(uint8_t slot)
{
    return gCh[slot < ADS_SLOTS ? slot : 0];
}

void adsSetChCfg(uint8_t slot, const AdsChCfg &c, bool save)
{
    BusGuard g;
    if (slot >= ADS_SLOTS) return;
    gCh[slot] = c;
    if (save) prefs().putBytes("adsch", gCh, sizeof(gCh));
    gSlot[slot].value = adsScale(slot, gSlot[slot].volts);
    Serial.printf("[ADS] %s scale %.3fV->%.3f  %.3fV->%.3f  lo %s %.3f hi %s %.3f\n", ADS_SLOT_NAMES[slot], c.v1, c.y1, c.v2, c.y2, c.loOn ? "on" : "off",
                  c.lo, c.hiOn ? "on" : "off", c.hi);
}

const AdsSlot &adsSlot(uint8_t slot)
{
    return gSlot[slot < ADS_SLOTS ? slot : 0];
}

void adsResetStats(int slot)
{
    BusGuard g;
    for (uint8_t i = 0; i < ADS_SLOTS; i++) {
        if (slot >= 0 && i != slot) continue;
        gSlot[i].n = 0;
        gSlot[i].vSum = 0;
        gSlot[i].vMin = gSlot[i].vMax = gSlot[i].volts;
    }
}

void adsSetDemo(bool on)
{
    BusGuard g;
    gS.demo = on;
    if (on && !gS.found) gS.chip = gCfg.chipSel == 2 ? AdsChip::ADS1015 : AdsChip::ADS1115;
    if (!on) adsScan();
    adsResetStats();
    Serial.printf("[ADS] demo %s\n", on ? "ON" : "OFF");
}

float adsScale(uint8_t slot, float volts)
{
    const AdsChCfg &c = gCh[slot];
    if (fabsf(c.v2 - c.v1) < 1e-6f) return volts;
    return c.y1 + (volts - c.v1) * (c.y2 - c.y1) / (c.v2 - c.v1);
}

void adsRange(uint8_t slot, float &lo, float &hi)
{
    const float fsr = ADS_FSR[gCfg.gain];
    // ขาเข้า single-ended ห้ามเกิน VDD (3.3 V) ช่วงที่วัดได้จริงจึงเป็น 0..min(FSR, 3.3)
    const float vLo = slot < 4 ? 0.0f : -fsr;
    const float vHi = slot < 4 ? (fsr < 3.3f ? fsr : 3.3f) : fsr;
    lo = adsScale(slot, vLo);
    hi = adsScale(slot, vHi);
    if (lo > hi) {
        const float t = lo;
        lo = hi;
        hi = t;
    }
}

uint8_t adsDecimals(uint8_t slot)
{
    float lo, hi;
    adsRange(slot, lo, hi);
    const float span = hi - lo;
    if (span >= 1000) return 0;
    if (span >= 100) return 1;
    if (span >= 10) return 2;
    return 3;
}

uint16_t adsSps()
{
    return is1015() ? ADS1015_SPS[gCfg.rate] : ADS1115_SPS[gCfg.rate];
}

float adsLsbVolts()
{
    return ADS_FSR[gCfg.gain] / (is1015() ? 2048.0f : 32768.0f);
}

uint8_t adsFirstSlot()
{
    return gCfg.diff ? 4 : 0;
}

uint8_t adsSlotCount()
{
    return gCfg.diff ? 2 : 4;
}

const char *adsChipName(AdsChip c)
{
    switch (c) {
        case AdsChip::ADS1115: return "ADS1115";
        case AdsChip::ADS1015: return "ADS1015";
        default: return "-";
    }
}

}  // namespace hal
