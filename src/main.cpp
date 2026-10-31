// =====================================================================
//  ADS + PCF Combo Lab  --  ESP32 CYD Dev Level 2 (LVGL 8.3)
//  บอร์ด ESP32-2432S028 (CYD) 320x240 + จอสัมผัส XPT2046
//  + ADS1115/ADS1015 (ADC 4 ช่อง) และ PCF8574 (I/O 8 ขา) บนบัส I2C เดียวกัน (SDA=IO27, SCL=IO22, INT ร่วม=IO35)
//
//    รวม     แดชบอร์ด: A0-A3 + ไฟ P0-P7 + บัสย่อ + กฎที่ทำงานล่าสุด
//    ADC     ค่า / ช่อง / กราฟ / ตั้งค่า  (จากแล็บ ADS1115)
//    I/O     ขา P0-P7 แตะสลับ + ตั้งทิศทาง  (จากแล็บ PCF8574)
//    กฎ      กฎข้ามชิป (ค่า ADS -> ขา PCF) / แถบไฟ / ปุ่ม PCF เลือกช่อง-gain ของ ADS
//    บัส     แผนภาพบัส I2C มีแพ็กเก็ตวิ่ง + สถิติ + ความเร็ว + RDY/INT ร่วม IO35
//    ระบบ    จอ / สัมผัส (Calibration 5 จุด + ทดสอบ 9 จุด) / ไฟ RGB / เสียง / ข้อมูลบอร์ด  (จาก Lab_Template_Start)
//
//  ไฟล์ในโปรเจกต์: ดู src/ui.h และ src/hal.h
//    main.cpp      เริ่มระบบ + ผูก LVGL กับจอ (DMA) และจอสัมผัส + คำสั่ง Serial (ไฟล์นี้)
// =====================================================================

#include <Arduino.h>
#include <lvgl.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

// loop() มี LVGL + printf ภาษาไทย  stack 8 KB เดิมเสี่ยงล้น (ล้น = รีสตาร์ทเอง) จึงขยายเป็น 16 KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

constexpr int BUF_LINES = 40;  // บัฟเฟอร์วาด 40 บรรทัด x 2 ชุด (ประมาณ 51 KB)

lv_disp_draw_buf_t drawBuf;
lv_color_t buf1[cyd::DISPLAY_WIDTH * BUF_LINES];
lv_color_t buf2[cyd::DISPLAY_WIDTH * BUF_LINES];
lv_disp_drv_t dispDrv;
lv_indev_drv_t indevDrv;
bool gPrintRaw = false;

void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP)
{
    TFT_eSPI &tft = hal::tft();
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    // DMA: ส่งภาพออกจอเบื้องหลัง ระหว่างนั้น LVGL วาดบัฟเฟอร์อีกชุดได้เลย
    // byte ถูกสลับมาแล้วจาก LV_COLOR_16_SWAP จึงไม่ต้องสลับซ้ำ
    tft.dmaWait();  // รอชุดก่อนหน้าส่งเสร็จ
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixelsDMA(reinterpret_cast<uint16_t *>(&colorP->full), w * h);
    lv_disp_flush_ready(disp);
}

void touchRead(lv_indev_drv_t *, lv_indev_data_t *data)
{
    static bool prevDown = false;
    static int16_t lastX = 0, lastY = 0;

    if (hal::touchCaptured()) {  // หน้า calibrate อ่านชิปเองอยู่
        data->state = LV_INDEV_STATE_REL;
        data->point.x = lastX;
        data->point.y = lastY;
        prevDown = false;
        return;
    }

    hal::touchPoll(millis());
    const hal::TouchState &t = hal::touch();
    const bool down = t.down && hal::touchCal().valid;
    if (down) {
        lastX = t.x;
        lastY = t.y;
        if (!prevDown) hal::soundClick();
    }
    if (gPrintRaw && down != prevDown) Serial.printf("[TOUCH] %s x=%3d y=%3d raw=(%4d,%4d) z=%4d\n", down ? "down" : "up  ", t.x, t.y, t.rawX, t.rawY, t.z);
    data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = lastX;
    data->point.y = lastY;
    prevDown = down;
}

void lvglBegin()
{
    lv_init();

    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, cyd::DISPLAY_WIDTH * BUF_LINES);
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = cyd::DISPLAY_WIDTH;
    dispDrv.ver_res = cyd::DISPLAY_HEIGHT;
    dispDrv.flush_cb = displayFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = touchRead;
    indevDrv.scroll_limit = 8;  // px ที่ต้องลากก่อนเริ่มเลื่อนหน้า
    lv_indev_drv_register(&indevDrv);
}

void resetDisplaySettings()
{
    hal::displayApply(hal::displayDefaults());
    hal::setBacklight(app::BL_DEFAULT_PERCENT, true);
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    ui_display_refresh();
    hal::soundPlay(hal::Melody::Success);
    Serial.println("[DISP] reset to defaults");
}

void printAds()
{
    const hal::AdsState &a = hal::ads();
    const hal::AdsCfg &c = hal::adsCfg();
    Serial.printf("[ADS] %s chip=%s (detected %s %uus) addr=0x%02X gain=+-%.3fV rate=%uSPS avg=%u %s %s | reads=%lu (%u/s) errors=%lu rdyMiss=%lu bus=%u | RDY pin IO35=%d\n",
                  a.found ? "FOUND" : a.demo ? "DEMO" : "NOT FOUND", hal::adsChipName(a.chip), hal::adsChipName(a.detected), a.detectUs, a.addr,
                  hal::ADS_FSR[c.gain], hal::adsSps(), hal::ADS_AVG_N[c.avg], c.diff ? "DIFF" : "SINGLE", c.rdy ? "RDY" : "POLL",
                  static_cast<unsigned long>(a.reads), a.readsPerSec, static_cast<unsigned long>(a.errors), static_cast<unsigned long>(a.rdyTimeouts),
                  a.busCount, digitalRead(cyd::ADS_RDY_PIN));
}

void printBus()
{
    const hal::BusState &b = hal::bus();
    const hal::PcfState &p = hal::pcf();
    Serial.printf("[BUS] %lu Hz devices=%u tx=%lu (%u/s: ads %u/s pcf %u/s) errors=%lu (ads %lu pcf %lu) load=%u%% IO35=%d\n",
                  static_cast<unsigned long>(b.clockHz), b.count, static_cast<unsigned long>(b.tx), b.txPerSec, b.adsPerSec, b.pcfPerSec,
                  static_cast<unsigned long>(b.errors), static_cast<unsigned long>(b.adsErr), static_cast<unsigned long>(b.pcfErr), b.loadPct,
                  digitalRead(cyd::INT_PIN));
    Serial.printf("[PCF] %s 0x%02X dir=0x%02X low=0x%02X out=0x%02X port w=0x%02X r=0x%02X %s reads=%lu ints=%lu errors=%lu\n",
                  p.found ? "FOUND" : "NOT FOUND", p.addr, p.dirMask, p.lowMask, p.outOn, p.outByte, p.inByte, p.intMode ? "INT" : "POLL",
                  static_cast<unsigned long>(p.reads), static_cast<unsigned long>(p.ints), static_cast<unsigned long>(p.errors));
}

void printRules()
{
    const hal::RulesCfg &c = hal::rulesCfg();
    const hal::RulesState &r = hal::rules();
    for (int i = 0; i < hal::RULES_MAX; i++) {
        const hal::Rule &x = c.rule[i];
        Serial.printf("[RULE] %d %s %-5s %s %8.3f hyst %.3f hold %ums -> P%u %-6s | %s fired=%lu\n", i + 1, x.en ? "ON " : "off", hal::ADS_SLOT_NAMES[x.slot],
                      x.above ? ">" : "<", x.thr, x.hyst, x.holdMs, x.pin, hal::RULE_ACT_NAMES[x.act < 4 ? x.act : 0], r.active[i] ? "ACTIVE" : "idle",
                      static_cast<unsigned long>(r.fired[i]));
    }
    Serial.printf("[RULE] bar %s mode %u sel %s lit %u/%u | buttons %s P4=%s P5=%s P6=%s P7=%s | hold %d own=0x%02X manual=0x%02X\n",
                  c.barOn ? "ON" : "off", c.barMode, hal::ADS_SLOT_NAMES[c.sel], r.barLit, r.barTotal, c.inOn ? "ON" : "off", hal::IN_ACT_NAMES[c.inAct[0]],
                  hal::IN_ACT_NAMES[c.inAct[1]], hal::IN_ACT_NAMES[c.inAct[2]], hal::IN_ACT_NAMES[c.inAct[3]], r.hold, r.ownMask, r.manualMask);
}

void printValues()
{
    for (uint8_t i = hal::adsFirstSlot(); i < hal::adsFirstSlot() + hal::adsSlotCount(); i++) {
        const hal::AdsSlot &s = hal::adsSlot(i);
        Serial.printf("[VAL] %-5s raw=%6d  %8.4f V  value=%10.3f  min=%.4f max=%.4f avg=%.4f V  n=%lu%s%s\n", hal::ADS_SLOT_NAMES[i], s.counts, s.volts,
                      s.value, s.vMin, s.vMax, s.n ? s.vSum / s.n : 0.0, static_cast<unsigned long>(s.n), s.clip ? " CLIP" : "",
                      s.alarm == 2 ? " ALARM-HIGH" : s.alarm == 1 ? " ALARM-LOW" : "");
    }
}

void printStatus()
{
    const hal::TouchCal &c = hal::touchCal();
    const hal::DisplayCfg &d = hal::displayCfg();
    const hal::AdsState &a = hal::ads();
    const hal::BusState &b = hal::bus();
    const hal::PcfState &p = hal::pcf();
    Serial.printf("[STATUS] up=%lus heap=%uKB (min %uKB, block %uKB) loopStackFree=%uB | disp %s inv=%d fH=%d fV=%d | touch %s | ads %s %s 0x%02X %u/s err=%lu"
                  " | pcf %s 0x%02X out=0x%02X in=0x%02X | bus %u/s load %u%% err %lu\n",
                  millis() / 1000, static_cast<unsigned>(ESP.getFreeHeap() / 1024), static_cast<unsigned>(ESP.getMinFreeHeap() / 1024),
                  static_cast<unsigned>(ESP.getMaxAllocHeap() / 1024), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  hal::displayDriverName(), d.invert, d.flipH, d.flipV, c.valid ? "calibrated" : "NOT calibrated",
                  a.found ? "OK" : a.demo ? "DEMO" : "MISSING", hal::adsChipName(a.chip), a.addr, a.readsPerSec, static_cast<unsigned long>(a.errors),
                  p.found ? "OK" : "MISSING", p.addr, p.outOn, p.inByte, b.txPerSec, b.loadPct, static_cast<unsigned long>(b.errors));
}

void printHelp()
{
    Serial.println(F("---- Serial commands ----\n"
                     " h  help            i  status          S  screenshot (tools/screenshot.py)\n"
                     " c  calibrate touch k  show calibration\n"
                     " d  display config  r  reset display config (also: hold BOOT 1 s)\n"
                     " t  toggle raw touch print\n"
                     " l  LED test R,G,B,W   s  sound test\n"
                     "---- shared I2C bus ----\n"
                     " b  bus + PCF state   n  scan bus (ADS + PCF)   #clk 0|1 (100 / 400 kHz)\n"
                     "---- ADS1115 / ADS1015 ----\n"
                     " a  ADS state       v  values of every channel\n"
                     " m  toggle polling / RDY(IO35)   x  toggle single / differential   D  toggle demo\n"
                     " z  reset min/max/avg   0-5  show channel on ADC detail\n"
                     " #gain 0-5  #rate 0-7  #avg 0-4  #chip 0-2  #addr 0|72-75  #beep 0|1\n"
                     " #hi SLOT V / #lo SLOT V (no V = off)   #key SLOT FIELD (keypad 0-5)   #scale SLOT V1 Y1 V2 Y2\n"
                     "---- PCF8574 ----\n"
                     " #out PIN 0|1   #tog PIN   #dir PIN 1=in|0=out   #low PIN 0|1   #int 0|1   #pcfreset\n"
                     "---- rules (ADS -> PCF) ----\n"
                     " u  print rules   #rule N EN SLOT ABOVE THR PIN ACT [HYST HOLDMS]  (N 1-8, ACT 0 on 1 off 2 blink 3 toggle)\n"
                     " #bar 0|1 MODE(0 bar 1 dot 2 vu)   #sel SLOT   #in 0|1   #hold 0|1   #edit N   #rdef (defaults)\n"
                     "---- UI ----\n"
                     " #tab 0-5 (dash adc io rules bus sys)   #adc 0-3   #rsub 0-2   #sub 0-4 (system: 1 = touch)   #set 0-1   #scroll Y"));
}

// ส่งเป็นไบต์ดิบ RGB565 (big-endian) 320x240 = 150 KB ใช้เวลา ~14 วินาทีที่ 115200  รับด้วย tools/screenshot.py
// อ่านกลับจากหน่วยความจำของชิปจอผ่าน MISO (ILI9341 ได้, ST7789 บางบอร์ดไม่ได้ต่อ MISO = ภาพดำ)
void screenshot()
{
    static uint16_t row[cyd::DISPLAY_WIDTH];
    TFT_eSPI &tft = hal::tft();
    tft.dmaWait();
    delay(20);
    Serial.printf("\n#SCREENSHOT %d %d\n", cyd::DISPLAY_WIDTH, cyd::DISPLAY_HEIGHT);
    for (int y = 0; y < cyd::DISPLAY_HEIGHT; y++) {
        tft.readRect(0, y, cyd::DISPLAY_WIDTH, 1, row);
        Serial.write(reinterpret_cast<const uint8_t *>(row), sizeof(row));
    }
    Serial.println("\n#END");
}

// คำสั่ง # ของบัส / PCF8574 / กฎ / แท็บใหม่ (คืน false = ไม่รู้จัก)
bool comboCommand(const char *word, int n, const float *f)
{
    const int a = static_cast<int>(f[0]);
    const int b = static_cast<int>(f[1]);
    if (n >= 2 && strcmp(word, "clk") == 0) {
        hal::busSetFast(a != 0);
        ui_busmap_refresh();
    } else if (n >= 3 && strcmp(word, "out") == 0 && a >= 0 && a < 8) {
        hal::rulesManual(static_cast<uint8_t>(a));
        hal::pcfSetOutput(static_cast<uint8_t>(a), b != 0);
    } else if (n >= 2 && strcmp(word, "tog") == 0 && a >= 0 && a < 8) {
        ui_pin_tap(static_cast<uint8_t>(a));
    } else if (n >= 3 && strcmp(word, "dir") == 0 && a >= 0 && a < 8) {
        hal::pcfSetDir(static_cast<uint8_t>(a), b != 0);
        ui_pins_refresh();
    } else if (n >= 3 && strcmp(word, "low") == 0 && a >= 0 && a < 8) {
        hal::pcfSetActiveLow(static_cast<uint8_t>(a), b != 0);
        ui_pins_refresh();
    } else if (n >= 2 && strcmp(word, "int") == 0) {
        hal::pcfSetIntMode(a != 0);
        ui_busmap_refresh();
    } else if (strcmp(word, "pcfreset") == 0) {
        hal::pcfResetConfig();
        ui_pins_refresh();
    } else if (n >= 8 && strcmp(word, "rule") == 0 && a >= 1 && a <= hal::RULES_MAX) {
        hal::Rule r = hal::rulesCfg().rule[a - 1];
        r.en = b != 0;
        r.slot = static_cast<uint8_t>(constrain(static_cast<int>(f[2]), 0, hal::ADS_SLOTS - 1));
        r.above = f[3] != 0;
        r.thr = f[4];
        r.pin = static_cast<uint8_t>(constrain(static_cast<int>(f[5]), 0, 7));
        r.act = static_cast<uint8_t>(constrain(static_cast<int>(f[6]), 0, 3));
        if (n >= 9) r.hyst = f[7];
        if (n >= 10) r.holdMs = static_cast<uint16_t>(f[8]);
        hal::rulesSetRule(static_cast<uint8_t>(a - 1), r);
    } else if (n >= 2 && strcmp(word, "bar") == 0) {
        hal::rulesSetBar(a != 0, static_cast<uint8_t>(n >= 3 ? b : hal::rulesCfg().barMode));
    } else if (n >= 2 && strcmp(word, "sel") == 0 && a >= 0 && a < hal::ADS_SLOTS) {
        hal::rulesSelect(static_cast<uint8_t>(a));
    } else if (n >= 2 && strcmp(word, "in") == 0) {
        hal::rulesSetInput(a != 0, hal::rulesCfg().inAct);
    } else if (n >= 2 && strcmp(word, "hold") == 0) {
        hal::rulesHold(a != 0);
    } else if (n >= 2 && strcmp(word, "edit") == 0) {
        ui_goto_tab(UI_TAB_RULES);
        ui_rules_show(0);
        ui_rule_edit(a - 1);
    } else if (strcmp(word, "rdef") == 0) {
        hal::rulesResetDefaults();
    } else if (n >= 2 && strcmp(word, "adc") == 0) {
        ui_goto_tab(UI_TAB_ADC);
        ui_adc_show(a);
        Serial.printf("[CMD] adc sub %d\n", a);
    } else if (n >= 2 && strcmp(word, "rsub") == 0) {
        ui_goto_tab(UI_TAB_RULES);
        ui_rules_show(a);
        Serial.printf("[CMD] rules sub %d\n", a);
    } else {
        return false;
    }
    return true;
}

// คำสั่งแบบบรรทัด (#...) เรียกฟังก์ชันเดียวกับปุ่มบนจอ  Serial จึงทดสอบเส้นทางเดียวกับผู้ใช้
void lineCommand(const char *line)
{
    char word[12] = "";
    float f[9] = {};
    const int n = sscanf(line, "%11s %f %f %f %f %f %f %f %f %f", word, &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8]);
    const int a = static_cast<int>(f[0]);
    hal::AdsCfg c = hal::adsCfg();
    bool cfgChanged = true;
    if (n >= 2 && strcmp(word, "gain") == 0 && a >= 0 && a < hal::ADS_GAINS) c.gain = static_cast<uint8_t>(a);
    else if (n >= 2 && strcmp(word, "rate") == 0 && a >= 0 && a < hal::ADS_RATES) c.rate = static_cast<uint8_t>(a);
    else if (n >= 2 && strcmp(word, "avg") == 0 && a >= 0 && a < hal::ADS_AVGS) c.avg = static_cast<uint8_t>(a);
    else if (n >= 2 && strcmp(word, "chip") == 0 && a >= 0 && a <= 2) c.chipSel = static_cast<uint8_t>(a);
    else if (n >= 2 && strcmp(word, "addr") == 0 && (a == 0 || (a >= 0x48 && a <= 0x4B))) c.addrSel = static_cast<uint8_t>(a);
    else if (n >= 2 && strcmp(word, "beep") == 0) c.beep = a != 0;
    else cfgChanged = false;
    if (cfgChanged) {
        hal::adsSetCfg(c);
        ui_ads_changed();
        return;
    }
    if (n == 6 && strcmp(word, "scale") == 0 && a >= 0 && a < hal::ADS_SLOTS) {
        hal::AdsChCfg ch = hal::adsChCfg(static_cast<uint8_t>(a));
        ch.v1 = f[1];
        ch.y1 = f[2];
        ch.v2 = f[3];
        ch.y2 = f[4];
        hal::adsSetChCfg(static_cast<uint8_t>(a), ch);
        ui_ads_changed();
    } else if (n >= 2 && strcmp(word, "tab") == 0) {
        ui_goto_tab(a);
        Serial.printf("[CMD] tab %d\n", a);
    } else if (n >= 2 && strcmp(word, "scroll") == 0) {
        ui_scroll_tab(a);
        Serial.printf("[CMD] scroll %d\n", a);
    } else if (n >= 2 && strcmp(word, "sub") == 0) {
        ui_goto_tab(UI_TAB_SYS);
        ui_sys_show(a);
        Serial.printf("[CMD] system sub %d\n", a);
    } else if ((n == 2 || n == 3) && (strcmp(word, "hi") == 0 || strcmp(word, "lo") == 0) && a >= 0 && a < hal::ADS_SLOTS) {
        // #hi S V = เตือนเมื่อสูงกว่า V, #hi S = ปิด (เหมือนกันกับ #lo)
        hal::AdsChCfg ch = hal::adsChCfg(static_cast<uint8_t>(a));
        if (word[0] == 'h') {
            ch.hiOn = n == 3;
            if (n == 3) ch.hi = f[1];
        } else {
            ch.loOn = n == 3;
            if (n == 3) ch.lo = f[1];
        }
        hal::adsSetChCfg(static_cast<uint8_t>(a), ch);
        ui_ads_changed();
    } else if (n == 3 && strcmp(word, "key") == 0) {
        ui_settings_edit(a, static_cast<int>(f[1]));
        Serial.printf("[CMD] keypad slot %d field %d\n", a, static_cast<int>(f[1]));
    } else if (n >= 2 && strcmp(word, "set") == 0) {
        ui_goto_tab(UI_TAB_ADC);
        ui_adc_show(3);
        ui_settings_show(a);
        Serial.printf("[CMD] settings sub %d\n", a);
    } else if (!comboCommand(word, n, f)) {
        Serial.printf("[CMD] unknown '#%s'  (h = help)\n", line);
    }
}

void toggleCfg(bool diff)
{
    hal::AdsCfg c = hal::adsCfg();
    if (diff) c.diff = !c.diff;
    else c.rdy = !c.rdy;
    hal::adsSetCfg(c);
    ui_ads_changed();
}

void serialCommands()
{
    static char line[64];
    static int lineLen = -1;  // -1 = ไม่ได้อยู่ในคำสั่ง #
    while (Serial.available()) {
        const char ch = static_cast<char>(Serial.read());
        if (lineLen >= 0) {
            if (ch == '\n' || ch == '\r') {
                line[lineLen] = '\0';
                lineLen = -1;
                lineCommand(line);
            } else if (lineLen < static_cast<int>(sizeof(line)) - 1) {
                line[lineLen++] = ch;
            }
            continue;
        }
        if (ch >= '0' && ch <= '5') {
            ui_detail_select(ch - '0');
            continue;
        }
        switch (ch) {
            case '#': lineLen = 0; break;
            case 'S': screenshot(); break;
            case 'a': printAds(); break;
            case 'v': printValues(); break;
            case 'n':
                hal::adsScan();
                hal::pcfScan();
                ui_ads_changed();
                printAds();
                printBus();
                break;
            case 'b': printBus(); break;
            case 'u': printRules(); break;
            case 'm': toggleCfg(false); break;
            case 'x': toggleCfg(true); break;
            case 'D':
                hal::adsSetDemo(!hal::ads().demo);
                ui_ads_changed();
                break;
            case 'z':
                hal::adsResetStats();
                Serial.println("[ADS] stats reset");
                break;
            case 'h': printHelp(); break;
            case 'i': printStatus(); break;
            case 'c': ui_calibrate_start(false); break;
            case 'k': {
                const hal::TouchCal &c = hal::touchCal();
                Serial.printf("[CAL] %s  x = %.5f*u %+.5f*v %+.1f   y = %.5f*u %+.5f*v %+.1f  fit rms %.2f max %.2f verify %.2f px\n",
                              c.valid ? "valid" : "INVALID", c.a, c.b, c.c, c.d, c.e, c.f, c.fitRms, c.fitMax, c.verifyErr);
                const hal::TouchState &t = hal::touch();
                Serial.printf("[TOUCH] last raw=(%d,%d) z=%d -> x=%d y=%d\n", t.rawX, t.rawY, t.z, t.x, t.y);
                break;
            }
            case 'd': {
                const hal::DisplayCfg &d = hal::displayCfg();
                Serial.printf("[DISP] driver=%s invert=%d bgr=%d flipH=%d flipV=%d MADCTL=0x%02X backlight=%d%%\n", hal::displayDriverName(), d.invert,
                              d.bgr, d.flipH, d.flipV, hal::displayMadctl(), hal::backlight());
                break;
            }
            case 'r': resetDisplaySettings(); break;
            case 't':
                gPrintRaw = !gPrintRaw;
                Serial.printf("[TOUCH] raw print %s\n", gPrintRaw ? "ON" : "OFF");
                break;
            case 'l': {  // ทดสอบ LED แบบบล็อกสั้นๆ (ใช้ดีบักเท่านั้น)
                const uint8_t seq[5][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}};
                const char *names[5] = {"RED", "GREEN", "BLUE", "WHITE", "OFF"};
                for (int i = 0; i < 5; i++) {
                    hal::ledSetRgb(seq[i][0], seq[i][1], seq[i][2]);
                    Serial.printf("[LED] %s\n", names[i]);
                    delay(500);
                }
                break;
            }
            case 's': hal::soundPlay(hal::Melody::Scale); Serial.println("[SOUND] scale C5-C6"); break;
            default: break;
        }
    }
}

// กดปุ่ม BOOT ค้าง 1 วินาที = รีเซ็ตค่าจอ (ทางออกเมื่อตั้งค่าจอผิดจนมองไม่เห็น)
void bootButton(uint32_t now)
{
    static uint32_t downSince = 0;
    static bool done = false;
    if (digitalRead(cyd::BOOT_BUTTON_PIN) == LOW) {
        if (downSince == 0) downSince = now;
        if (!done && now - downSince >= 1000) {
            done = true;
            resetDisplaySettings();
        }
    } else {
        downSince = 0;
        done = false;
    }
}

}  // namespace

void setup()
{
    Serial.begin(app::SERIAL_BAUD);
    delay(200);
    Serial.printf("\n=== %s %s | LVGL %d.%d.%d ===\n", app::APP_NAME, app::FW_VERSION, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    hal::begin();  // ค่าที่จำถาวร (NVS)
    Serial.printf("[BOOT] reset reason: %s\n", hal::resetReasonText());
    hal::ledBegin();
    hal::soundBegin();
    hal::displayBegin();  // จอ + ค่าแก้จอที่บันทึกไว้ + ไฟหลังจอ
    hal::touchBegin();    // โหลดค่า calibrate (ถ้ายังไม่มี หน้า calibrate จะขึ้นเองหลัง splash)
    hal::busBegin();      // บัส I2C ร่วม IO27/IO22 + mutex + INT ร่วม IO35
    hal::adsBegin();      // สแกนหา ADS1x15 + ค่าตั้งที่บันทึกไว้
    hal::pcfBegin();      // สแกนหา PCF8574 + ทิศทางขาที่บันทึกไว้
    hal::rulesBegin();    // กฎ / แถบไฟ / ปุ่มเลือก ที่บันทึกไว้
    hal::busStartTask();  // task "i2c" บน core 0: ADS -> PCF -> กฎ วนตลอด

    lvglBegin();
    ui_init();
    hal::soundPlay(hal::Melody::Startup);
    printHelp();
}

void loop()
{
    const uint32_t wait = lv_timer_handler();  // LVGL บอกเองว่าอีกกี่ ms ถึงมีงานถัดไป
    delay(constrain(wait, 1, 5));

    const uint32_t now = millis();
    serialCommands();
    bootButton(now);

    static uint32_t lastStatus = 0;
    if (app::STATUS_PRINT_MS != 0 && now - lastStatus >= app::STATUS_PRINT_MS) {
        lastStatus = now;
        printStatus();
    }
}
