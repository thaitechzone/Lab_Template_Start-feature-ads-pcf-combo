#include <Wire.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  hal_bus.cpp -- บัส I2C ร่วมของ ADS1x15 + PCF8574 และ "ตัวจัดคิว" (ไม่รู้จัก LVGL)
//
//  ชิป 2 ตัวอยู่บนสายคู่เดียวกัน (SDA=IO27, SCL=IO22) แยกกันด้วย address:
//    ADS1x15  0x48-0x4B   /   PCF8574 0x20-0x27   /   PCF8574A 0x38-0x3F   -> ไม่ชนกัน
//  Wire ของ ESP32 ใช้ได้ทีละงานเท่านั้น จึงมีเจ้าของคนเดียว = task "i2c" บน core 0
//    รอบหนึ่ง: ADS (เริ่มแปลง/อ่านผล) -> PCF (อ่านปุ่ม) -> กฎ (สั่งขา PCF) -> หลับ
//    ระหว่างที่ ADS กำลังแปลง (~8 ms ที่ 128 SPS) บัสว่าง: PCF ใช้ช่องว่างนี้ได้เต็มที่
//  UI (core 1) เรียกฟังก์ชันตั้งค่าได้ตลอด เพราะทุกฟังก์ชันที่แตะ Wire ถือ BusGuard (mutex ตัวเดียวกัน) ก่อน
// =====================================================================

namespace hal {

namespace {

SemaphoreHandle_t gLock = nullptr;
BusState gS;
bool gFast = false;

constexpr uint8_t RING = 32;
BusPacket gRing[RING];
volatile uint32_t gHead = 0;  // หมายเลขแพ็กเก็ตถัดไป

uint32_t gRateSince = 0, gBusyUs = 0, gTxCount = 0, gAdsCount = 0, gPcfCount = 0;

void updateRates(uint32_t now)
{
    const uint32_t el = now - gRateSince;
    if (el < 1000) return;
    gS.txPerSec = static_cast<uint16_t>(gTxCount * 1000UL / el);
    gS.adsPerSec = static_cast<uint16_t>(gAdsCount * 1000UL / el);
    gS.pcfPerSec = static_cast<uint16_t>(gPcfCount * 1000UL / el);
    const uint32_t pct = gBusyUs / (el * 10UL);
    gS.loadPct = static_cast<uint8_t>(pct > 100 ? 100 : pct);
    gTxCount = gAdsCount = gPcfCount = gBusyUs = 0;
    gRateSince = now;
}

void i2cTask(void *)
{
    for (;;) {
        const uint32_t now = millis();
        uint32_t sleep;
        {
            BusGuard g;
            updateRates(now);
            sleep = adsService(now);   // 1) ADS: เริ่มแปลง / อ่านผลที่เสร็จแล้ว
            pcfService(now);           // 2) PCF: อ่านปุ่ม (ระหว่างที่ ADS กำลังแปลง บัสว่างอยู่พอดี)
            rulesService(now);         // 3) กฎ: ค่า ADS ล่าสุด -> เขียนขา PCF
        }
        if (sleep > app::I2C_TASK_MAX_SLEEP_MS) sleep = app::I2C_TASK_MAX_SLEEP_MS;
        // หลับอย่างน้อย 1 tick ให้ idle task ของ core 0 ได้ทำงาน (watchdog)
        vTaskDelay(sleep ? pdMS_TO_TICKS(sleep) : 1);
    }
}

}  // namespace

BusGuard::BusGuard()
{
    xSemaphoreTakeRecursive(gLock, portMAX_DELAY);
}

BusGuard::~BusGuard()
{
    xSemaphoreGiveRecursive(gLock);
}

void busBegin()
{
    gLock = xSemaphoreCreateRecursiveMutex();
    gFast = prefs().getBool("busfast", false);
    pinMode(cyd::INT_PIN, INPUT);  // IO35 ไม่มี pull-up ภายใน (ต้องต่อ 10k ภายนอก)
    gS.clockHz = gFast ? app::I2C_FREQ_FAST_HZ : app::I2C_FREQ_SLOW_HZ;
    Wire.begin(cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL, gS.clockHz);
    Wire.setTimeOut(20);
    gRateSince = millis();
    Serial.printf("[BUS] I2C SDA=%u SCL=%u %lu Hz | INT shared on IO%u\n", cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL,
                  static_cast<unsigned long>(gS.clockHz), cyd::INT_PIN);
}

void busStartTask()
{
    // อ่าน/เขียนชิปทั้งหมดใน task เดียวบน core 0 (LVGL อยู่ใน loop() core 1)
    xTaskCreatePinnedToCore(i2cTask, "i2c", 4096, nullptr, 2, nullptr, 0);
}

void busNote(uint8_t addr, bool write, bool ok, uint32_t us)
{
    gS.tx++;
    gTxCount++;
    gBusyUs += us;
    const BusDev d = busDevOf(addr);
    if (d == BusDev::Ads) {
        gS.adsTx++;
        gAdsCount++;
        if (!ok) gS.adsErr++;
    } else if (d == BusDev::Pcf) {
        gS.pcfTx++;
        gPcfCount++;
        if (!ok) gS.pcfErr++;
    }
    if (!ok) gS.errors++;
    BusPacket &p = gRing[gHead % RING];
    p.addr = addr;
    p.write = write;
    p.ok = ok;
    gHead = gHead + 1;
}

bool busWrite(uint8_t addr, const uint8_t *data, uint8_t n)
{
    BusGuard g;
    const uint32_t t0 = micros();
    Wire.beginTransmission(addr);
    Wire.write(data, n);
    const bool ok = Wire.endTransmission() == 0;
    busNote(addr, true, ok, micros() - t0);
    return ok;
}

bool busRead(uint8_t addr, uint8_t *data, uint8_t n)
{
    BusGuard g;
    const uint32_t t0 = micros();
    bool ok = Wire.requestFrom(addr, n) == n;
    for (uint8_t i = 0; i < n; i++) data[i] = ok ? static_cast<uint8_t>(Wire.read()) : 0;
    busNote(addr, false, ok, micros() - t0);
    return ok;
}

bool busProbe(uint8_t addr)
{
    BusGuard g;
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

uint8_t busScan()
{
    BusGuard g;
    gS.count = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (!busProbe(a)) continue;
        if (gS.count < sizeof(gS.addr)) gS.addr[gS.count++] = a;
    }
    gS.scans++;
    Serial.printf("[BUS] scan: %u device(s)", gS.count);
    for (uint8_t i = 0; i < gS.count; i++) Serial.printf(" 0x%02X", gS.addr[i]);
    Serial.println();
    return gS.count;
}

const BusState &bus()
{
    return gS;
}

BusDev busDevOf(uint8_t addr)
{
    if (addr >= 0x48 && addr <= 0x4B) return BusDev::Ads;
    if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F)) return BusDev::Pcf;
    return BusDev::Other;
}

void busSetFast(bool fast, bool save)
{
    BusGuard g;
    gFast = fast;
    gS.clockHz = fast ? app::I2C_FREQ_FAST_HZ : app::I2C_FREQ_SLOW_HZ;
    Wire.setClock(gS.clockHz);
    if (save) prefs().putBool("busfast", fast);
    Serial.printf("[BUS] clock %lu Hz%s\n", static_cast<unsigned long>(gS.clockHz), fast ? " (PCF8574 is rated 100 kHz!)" : "");
}

bool busFast()
{
    return gFast;
}

bool busIntLow()
{
    return digitalRead(cyd::INT_PIN) == LOW;
}

uint32_t busPackets(uint32_t since, BusPacket *out, uint8_t max, uint8_t &got)
{
    const uint32_t head = gHead;
    got = 0;
    if (head - since > RING) since = head - RING;  // ตามไม่ทัน: ทิ้งของเก่า
    while (since != head && got < max) out[got++] = gRing[since++ % RING];
    return since;
}

}  // namespace hal
