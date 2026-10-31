#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  hal_pcf8574.cpp -- PCF8574 Remote 8-Bit I/O Expander ผ่าน I2C (ไม่รู้จัก LVGL)
//
//  PCF8574 ไม่มี register ให้ตั้งค่า: มีแค่ "ไบต์เดียว" ของพอร์ต P7..P0
//    เขียน 1 ไบต์ = ตั้งระดับทั้ง 8 ขาพร้อมกัน      busWrite(addr, &byte, 1)
//    อ่าน 1 ไบต์  = ระดับไฟจริงของทั้ง 8 ขา         busRead(addr, &byte, 1)
//  ขาที่จะใช้เป็นอินพุตต้องเขียน 1 ไว้ (ปล่อยลอยให้ pull-up ภายใน) แล้วปุ่มค่อยดึงลง GND
//  ขา INT (open-drain) ลง LOW เมื่อขาใดเปลี่ยนจากค่าที่อ่านครั้งก่อน และกลับ HIGH เมื่อเราอ่าน/เขียนพอร์ต
//
//  ในแล็บนี้ INT ต่อรวมกับ ALRT/RDY ของ ADS1x15 ที่ IO35: ไม่ใช้ interrupt (ADS ดึงลงทุกครั้งที่แปลงเสร็จ)
//  แต่ให้ task "i2c" ดูระดับขาทุกรอบ: LOW = อ่านพอร์ต 1 ครั้ง (ถ้าเป็นฝีมือ ADS ก็แค่อ่านเกินมา 1 ไบต์)
// =====================================================================

namespace hal {

namespace {

PcfState gS;
uint8_t gErrRun = 0;            // จำนวนครั้งที่พลาดติดกัน
uint32_t gLastRead = 0, gLastScan = 0, gStuckSince = 0;
void (*gInputCb)(uint8_t, bool) = nullptr;

constexpr uint8_t DEFAULT_DIR = 0xF0;  // P4-P7 อินพุต
constexpr uint8_t DEFAULT_LOW = 0xFF;  // ทุกขาทำงานเมื่อ LOW

// ไบต์ที่ต้องเขียน: ขาอินพุต = 1 เสมอ, ขาเอาต์พุต = (ติด XOR ทำงานเมื่อ LOW)
//   ติด + ทำงานเมื่อ LOW  -> 0 (ดึงลง GND, LED แบบ sink สว่าง)
//   ดับ + ทำงานเมื่อ LOW  -> 1
uint8_t portByte()
{
    return static_cast<uint8_t>(((gS.outOn ^ gS.lowMask) & ~gS.dirMask) | gS.dirMask);
}

void noteResult(bool ok)
{
    if (ok) {
        gErrRun = 0;
        return;
    }
    gS.errors++;
    if (++gErrRun >= app::PCF_MAX_ERRORS && gS.found) {
        gS.found = false;
        Serial.printf("[PCF] lost 0x%02X (%u errors in a row) - check wiring\n", gS.addr, gErrRun);
    }
}

bool writePort()
{
    if (!gS.found) return false;
    const uint8_t b = portByte();
    const bool ok = busWrite(gS.addr, &b, 1);
    if (ok) gS.outByte = b;
    noteResult(ok);
    return ok;
}

bool readPort(uint32_t now)
{
    if (!gS.found) return false;
    gLastRead = now;
    uint8_t b = 0;
    if (!busRead(gS.addr, &b, 1)) {
        noteResult(false);
        return false;
    }
    noteResult(true);
    gS.reads++;
    const uint8_t changed = static_cast<uint8_t>((b ^ gS.inByte) & gS.dirMask);
    gS.inByte = b;
    for (uint8_t pin = 0; pin < 8; pin++) {
        if (!(changed & (1 << pin))) continue;
        const bool active = pcfActive(pin);
        Serial.printf("[PCF] IN P%u %s (port 0x%02X)\n", pin, active ? "ACTIVE" : "idle", b);
        if (gInputCb != nullptr) gInputCb(pin, active);
    }
    return true;
}

}  // namespace

void pcfBegin()
{
    gS.dirMask = prefs().getUChar("pcfdir", DEFAULT_DIR);
    gS.lowMask = prefs().getUChar("pcflow", DEFAULT_LOW);
    gS.intMode = prefs().getBool("pcfint", false);
    pcfScan();
}

bool pcfScan()
{
    BusGuard g;
    gLastScan = millis();
    const BusState &b = bus();
    busScan();
    uint8_t pick = 0;
    for (uint8_t i = 0; i < b.count && pick == 0; i++) {
        if (busDevOf(b.addr[i]) == BusDev::Pcf) pick = b.addr[i];
    }
    const bool was = gS.found;
    if (pick == 0) {
        gS.found = false;
        if (was) Serial.println("[PCF] not found");
        return false;
    }
    gS.addr = pick;
    gS.found = true;
    gErrRun = 0;
    Serial.printf("[PCF] found %s at 0x%02X\n", pick >= 0x38 ? "PCF8574A" : "PCF8574", pick);
    writePort();
    gS.inByte = portByte();  // ค่าเริ่มเปรียบเทียบ: ไม่ให้นับว่า "เปลี่ยน" ตอนเพิ่งเจอ
    readPort(millis());
    return true;
}

void pcfService(uint32_t now)
{
    BusGuard g;
    if (!gS.found) {
        if (now - gLastScan >= app::PCF_RESCAN_MS) pcfScan();
        return;
    }
    if (gS.intMode) {
        // INT ค้าง LOW (ไม่มี pull-up / สายลัด) -> อย่าอ่านทุกรอบจนบัสเต็ม: ถอยไปอ่านตามจังหวะ polling
        const bool stuck = gS.intStuckMs > 1000 && now - gLastRead < app::PCF_POLL_MS;
        if (stuck) {
            gS.intStuckMs = now - gStuckSince;
        } else if (busIntLow()) {
            gS.ints++;
            readPort(now);
            // อ่านแล้ว INT ต้องกลับเป็น HIGH ถ้ายัง LOW ต่อเนื่อง = ไม่มี pull-up / สาย INT ลัด GND
            // ยกเว้น ADS (โหมด RDY) แปลงเสร็จแล้วแต่ยังไม่เริ่มใหม่ = ADS เป็นคนดึงอยู่ ไม่ใช่ค้าง
            const bool adsHolds = adsCfg().rdy && !adsBusy();
            if (busIntLow() && !adsHolds) {
                if (gStuckSince == 0) gStuckSince = now;
                gS.intStuckMs = now - gStuckSince;
            } else {
                gStuckSince = 0;
                gS.intStuckMs = 0;
            }
        } else {
            gStuckSince = 0;
            gS.intStuckMs = 0;
            if (now - gLastRead >= app::PCF_INT_SAFETY_MS) readPort(now);
        }
    } else if (now - gLastRead >= app::PCF_POLL_MS) {
        readPort(now);
    }
}

const PcfState &pcf()
{
    return gS;
}

bool pcfIsInput(uint8_t pin)
{
    return (gS.dirMask >> pin) & 1;
}

bool pcfActive(uint8_t pin)
{
    if (!pcfIsInput(pin)) return (gS.outOn >> pin) & 1;
    const bool level = (gS.inByte >> pin) & 1;
    const bool low = (gS.lowMask >> pin) & 1;
    return low ? !level : level;
}

uint8_t pcfInputActiveMask()
{
    return static_cast<uint8_t>((gS.inByte ^ gS.lowMask) & gS.dirMask);  // ระดับ XOR ทำงานเมื่อ LOW = กดอยู่
}

void pcfSetOutMask(uint8_t onMask, bool log)
{
    BusGuard g;
    const uint8_t want = static_cast<uint8_t>(onMask & ~gS.dirMask);
    if (want == gS.outOn && gS.outByte == portByte()) return;  // ไม่เปลี่ยน ไม่ต้องใช้บัส
    gS.outOn = want;
    writePort();
    if (log) Serial.printf("[PCF] out 0x%02X (port 0x%02X)\n", gS.outOn, gS.outByte);
}

void pcfSetOutput(uint8_t pin, bool on)
{
    if (pin > 7 || pcfIsInput(pin)) return;
    BusGuard g;
    pcfSetOutMask(on ? (gS.outOn | (1 << pin)) : (gS.outOn & ~(1 << pin)));
    Serial.printf("[PCF] OUT P%u %s (port 0x%02X)\n", pin, on ? "ON" : "OFF", gS.outByte);
}

void pcfToggle(uint8_t pin)
{
    if (pin <= 7) pcfSetOutput(pin, !pcfActive(pin));
}

void pcfSetAllOutputs(bool on)
{
    pcfSetOutMask(on ? 0xFF : 0x00);
    Serial.printf("[PCF] all outputs %s (port 0x%02X)\n", on ? "ON" : "OFF", gS.outByte);
}

void pcfSetDir(uint8_t pin, bool input, bool save)
{
    if (pin > 7) return;
    BusGuard g;
    const uint8_t bit = static_cast<uint8_t>(1 << pin);
    gS.dirMask = input ? (gS.dirMask | bit) : (gS.dirMask & ~bit);
    gS.outOn &= ~bit;  // เปลี่ยนทิศแล้วเริ่มที่ "ดับ"
    if (save) prefs().putUChar("pcfdir", gS.dirMask);
    writePort();
    gS.inByte = static_cast<uint8_t>((gS.inByte & ~bit) | (portByte() & bit));
    Serial.printf("[PCF] P%u -> %s (dir 0x%02X)\n", pin, input ? "INPUT" : "OUTPUT", gS.dirMask);
}

void pcfSetActiveLow(uint8_t pin, bool low, bool save)
{
    if (pin > 7) return;
    BusGuard g;
    const uint8_t bit = static_cast<uint8_t>(1 << pin);
    gS.lowMask = low ? (gS.lowMask | bit) : (gS.lowMask & ~bit);
    if (save) prefs().putUChar("pcflow", gS.lowMask);
    writePort();
    Serial.printf("[PCF] P%u active-%s (low 0x%02X)\n", pin, low ? "LOW" : "HIGH", gS.lowMask);
}

void pcfSetIntMode(bool on, bool save)
{
    BusGuard g;
    gS.intMode = on;
    gStuckSince = 0;
    gS.intStuckMs = 0;
    if (save) prefs().putBool("pcfint", on);
    Serial.printf("[PCF] read mode %s\n", on ? "INT (IO35 shared)" : "polling");
}

void pcfResetConfig()
{
    BusGuard g;
    gS.dirMask = DEFAULT_DIR;
    gS.lowMask = DEFAULT_LOW;
    gS.outOn = 0;
    prefs().putUChar("pcfdir", gS.dirMask);
    prefs().putUChar("pcflow", gS.lowMask);
    writePort();
    Serial.println("[PCF] config reset: P0-P3 OUT, P4-P7 IN, all active-LOW");
}

void pcfOnInputChange(void (*cb)(uint8_t pin, bool active))
{
    gInputCb = cb;
}

}  // namespace hal
