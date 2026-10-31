#include <math.h>

#include "app_config.h"
#include "hal.h"

// =====================================================================
//  hal_rules.cpp -- กฎข้ามชิป (ไม่รู้จัก LVGL) ทำงานใน task "i2c" ต่อจากการอ่าน ADS/PCF ทุกรอบ
//
//  1) กฎ = "ตัวเปรียบเทียบเสมือน" (virtual comparator / relay)
//       ถ้า ค่าช่อง Ax > (หรือ <) ขีด  ->  ขา Pn ของ PCF8574 ติด / ดับ / กะพริบ / สลับ
//       ฮิสเทอรีซิส: เริ่มทำงานที่ขีด แต่เลิกทำงานเมื่อค่ากลับมาเลยขีดไปอีก hyst (กันรีเลย์สั่นเมื่อค่าแกว่งรอบขีด)
//       หน่วงเวลา: เงื่อนไขต้องเป็นจริง (หรือเท็จ) ต่อเนื่อง holdMs ก่อนจะเปลี่ยนสถานะ
//  2) แถบไฟ: ค่าของช่องที่เลือก (0-100% ของช่วงวัด) -> จำนวนขาเอาต์พุตที่ติด (แถบ / จุด / VU ค้างยอด)
//  3) ปุ่มเลือก: ขาอินพุต P4-P7 ของ PCF สั่ง ADS (เลือกช่อง, gain +/-, ค้างค่า, สลับเดี่ยว/ต่าง)
//
//  ขาที่กฎคุมอยู่ แต่ผู้ใช้แตะสั่งเองบนจอ -> "ยึดคืนชั่วคราว" จนกว่ากฎของขานั้นจะเปลี่ยนสถานะครั้งถัดไป
//  ค่าตั้งเก็บใน NVS key "rules" (struct เดียว)
// =====================================================================

namespace hal {

namespace {

RulesCfg gCfg;
RulesState gS;
uint32_t gLastEval = 0, gPendSince[RULES_MAX] = {}, gPeakAt = 0;
uint8_t gToggleLatch = 0;   // สถานะขาของกฎแบบ "สลับ"
uint8_t gPrevIn = 0;        // ขาอินพุตที่กดอยู่รอบก่อน (หาขอบกด)
bool gInPrimed = false;

void save()
{
    prefs().putBytes("rules", &gCfg, sizeof(gCfg));
}

bool slotMeasured(uint8_t slot)
{
    const uint8_t first = adsFirstSlot();
    return slot >= first && slot < first + adsSlotCount() && (ads().found || ads().demo);
}

void defaults()
{
    gCfg = RulesCfg();
    // ตัวอย่างพร้อมใช้: หมุนปุ่ม A0 ผ่าน 1.65 V -> P0 ติด / เกิน 2.8 V -> P1 กะพริบ
    Rule &a = gCfg.rule[0];
    a.en = true;
    a.slot = 0;
    a.thr = 1.65f;
    a.hyst = 0.10f;
    a.pin = 0;
    a.act = static_cast<uint8_t>(RuleAct::On);
    Rule &b = gCfg.rule[1];
    b.en = true;
    b.slot = 0;
    b.thr = 2.8f;
    b.hyst = 0.05f;
    b.pin = 1;
    b.act = static_cast<uint8_t>(RuleAct::Blink);
    for (uint8_t i = 2; i < RULES_MAX; i++) gCfg.rule[i].pin = i & 3;
}

// ---------- 1) กฎ ----------
void evalRules(uint32_t now)
{
    for (uint8_t i = 0; i < RULES_MAX; i++) {
        const Rule &r = gCfg.rule[i];
        if (!r.en || !slotMeasured(r.slot)) {
            gS.active[i] = false;
            gPendSince[i] = 0;
            continue;
        }
        const float v = adsSlot(r.slot).value;
        const bool was = gS.active[i];
        // ฮิสเทอรีซิส: ขณะทำงานอยู่ ใช้ขีดที่ถอยกลับไป hyst
        bool want;
        if (r.above) want = was ? v > r.thr - r.hyst : v > r.thr;
        else want = was ? v < r.thr + r.hyst : v < r.thr;
        if (want == was) {
            gPendSince[i] = 0;
            continue;
        }
        if (gPendSince[i] == 0) gPendSince[i] = now ? now : 1;
        if (now - gPendSince[i] < r.holdMs) continue;
        gPendSince[i] = 0;
        gS.active[i] = want;
        gS.manualMask &= ~static_cast<uint8_t>(1 << r.pin);  // กฎเปลี่ยนสถานะ = กฎคุมขาคืน
        if (want) {
            gS.fired[i]++;
            gS.firedAt[i] = now;
            if (static_cast<RuleAct>(r.act) == RuleAct::Toggle) gToggleLatch ^= static_cast<uint8_t>(1 << r.pin);
        }
        Serial.printf("[RULE] %u %s: %s %s %.3f (value %.3f) -> P%u %s\n", i + 1, want ? "ON " : "OFF", ADS_SLOT_NAMES[r.slot], r.above ? ">" : "<",
                      r.thr, v, r.pin, RULE_ACT_NAMES[r.act < 4 ? r.act : 0]);
    }
}

// ---------- 2) แถบไฟ ----------
float selValue()
{
    if (gS.hold) return gS.heldValue;
    return adsSlot(gCfg.sel).value;
}

uint8_t barMask(uint32_t now)
{
    float lo, hi;
    adsRange(gCfg.sel, lo, hi);
    float f = hi > lo ? (selValue() - lo) / (hi - lo) : 0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    gS.barFrac = f;

    uint8_t pins[8], n = 0;
    for (uint8_t p = 0; p < 8; p++) {
        if (!pcfIsInput(p)) pins[n++] = p;
    }
    gS.barTotal = n;
    if (n == 0) return 0;
    const uint8_t lit = static_cast<uint8_t>(lroundf(f * n));
    gS.barLit = lit;
    // VU: จุดสูงสุดค้างไว้ BAR_PEAK_HOLD_MS แล้วค่อยๆ ลง
    if (lit >= gS.barPeak) {
        gS.barPeak = lit;
        gPeakAt = now;
    } else if (now - gPeakAt > app::BAR_PEAK_HOLD_MS) {
        gS.barPeak--;
        gPeakAt = now - app::BAR_PEAK_HOLD_MS + app::BAR_PEAK_FALL_MS;
    }
    uint8_t mask = 0;
    switch (static_cast<BarMode>(gCfg.barMode)) {
        case BarMode::Dot: {
            const uint8_t k = static_cast<uint8_t>(lroundf(f * (n - 1)));
            mask = static_cast<uint8_t>(1 << pins[k]);
            break;
        }
        case BarMode::Vu:
            for (uint8_t k = 0; k < lit; k++) mask |= static_cast<uint8_t>(1 << pins[k]);
            if (gS.barPeak > 0) mask |= static_cast<uint8_t>(1 << pins[gS.barPeak - 1]);
            break;
        default:
            for (uint8_t k = 0; k < lit; k++) mask |= static_cast<uint8_t>(1 << pins[k]);
            break;
    }
    return mask;
}

// ---------- 3) ปุ่มเลือก ----------
void doInput(uint8_t idx)
{
    AdsCfg c = adsCfg();
    const InAct a = static_cast<InAct>(gCfg.inAct[idx]);
    bool cfgChanged = false;
    switch (a) {
        case InAct::NextCh: rulesSelect(rulesNextSlot(gCfg.sel)); break;
        case InAct::GainUp:
            if (c.gain + 1 < ADS_GAINS) c.gain++, cfgChanged = true;
            break;
        case InAct::GainDown:
            if (c.gain > 0) c.gain--, cfgChanged = true;
            break;
        case InAct::Hold: rulesHold(!gS.hold); break;
        case InAct::Diff:
            c.diff = !c.diff;
            cfgChanged = true;
            break;
        default: return;
    }
    if (cfgChanged) {
        adsSetCfg(c);
        if (!slotMeasured(gCfg.sel)) rulesSelect(adsFirstSlot());
        gS.cfgSeq++;
    }
    gS.inLast = static_cast<uint8_t>(4 + idx);
    gS.inSeq++;
    soundTone(1600, 40);
    Serial.printf("[RULE] button P%u -> %s (sel %s, gain +-%.3f V, %s)\n", 4 + idx, IN_ACT_NAMES[static_cast<uint8_t>(a)], ADS_SLOT_NAMES[gCfg.sel],
                  ADS_FSR[adsCfg().gain], adsCfg().diff ? "DIFF" : "SINGLE");
}

void pollInputs()
{
    const uint8_t now = pcfInputActiveMask();
    if (!gInPrimed) {
        gPrevIn = now;
        gInPrimed = true;
        return;
    }
    const uint8_t pressed = static_cast<uint8_t>(now & ~gPrevIn);
    gPrevIn = now;
    if (!gCfg.inOn || !pcf().found) return;
    for (uint8_t i = 0; i < 4; i++) {
        if (pressed & (1 << (4 + i))) doInput(i);
    }
}

}  // namespace

void rulesBegin()
{
    if (prefs().isKey("rules") && prefs().getBytesLength("rules") == sizeof(RulesCfg)) prefs().getBytes("rules", &gCfg, sizeof(gCfg));
    else defaults();
    if (gCfg.version != 1 || gCfg.sel >= ADS_SLOTS) defaults();
    Serial.printf("[RULE] loaded: bar %s, buttons %s, sel %s\n", gCfg.barOn ? "ON" : "off", gCfg.inOn ? "ON" : "off", ADS_SLOT_NAMES[gCfg.sel]);
}

void rulesService(uint32_t now)
{
    BusGuard g;
    if (now - gLastEval < app::RULE_EVAL_MS) return;
    gLastEval = now;

    pollInputs();
    evalRules(now);
    if (!pcf().found) return;

    // ขาที่ใครคุม: แถบไฟชนะกฎ / กฎหลังทับกฎก่อน (ขาเดียวกัน)
    uint8_t own = 0, out = pcf().outOn;
    const bool blinkOn = (now / app::RULE_BLINK_MS) & 1;
    uint8_t ruleOf[8] = {};
    for (uint8_t i = 0; i < RULES_MAX; i++) {
        const Rule &r = gCfg.rule[i];
        if (!r.en || r.pin > 7 || pcfIsInput(r.pin)) continue;
        const uint8_t bit = static_cast<uint8_t>(1 << r.pin);
        own |= bit;
        ruleOf[r.pin] = static_cast<uint8_t>(i + 1);
        if (gS.manualMask & bit) continue;
        bool on;
        switch (static_cast<RuleAct>(r.act)) {
            case RuleAct::Off: on = !gS.active[i]; break;
            case RuleAct::Blink: on = gS.active[i] && blinkOn; break;
            case RuleAct::Toggle: on = gToggleLatch & bit; break;
            default: on = gS.active[i]; break;
        }
        out = on ? (out | bit) : (out & ~bit);
    }
    if (gCfg.barOn) {
        const uint8_t outs = static_cast<uint8_t>(~pcf().dirMask);
        const uint8_t m = barMask(now);
        own |= outs;
        for (uint8_t p = 0; p < 8; p++) {
            if (outs & (1 << p)) ruleOf[p] = 9;
        }
        out = static_cast<uint8_t>((out & ~(outs & ~gS.manualMask)) | (m & ~gS.manualMask));
    } else {
        barMask(now);  // ยังคำนวณไว้ให้หน้าจอแสดงตัวอย่าง
    }
    gS.ownMask = own;
    memcpy(gS.ruleOfPin, ruleOf, sizeof(ruleOf));
    pcfSetOutMask(out);
}

const RulesCfg &rulesCfg()
{
    return gCfg;
}

const RulesState &rules()
{
    return gS;
}

void rulesSetRule(uint8_t i, const Rule &r, bool doSave)
{
    if (i >= RULES_MAX) return;
    BusGuard g;
    const uint8_t oldPin = gCfg.rule[i].pin;
    gCfg.rule[i] = r;
    gS.active[i] = false;
    gPendSince[i] = 0;
    gS.manualMask &= ~static_cast<uint8_t>((1 << r.pin) | (1 << oldPin));
    if (oldPin != r.pin && oldPin < 8) pcfSetOutMask(pcf().outOn & ~(1 << oldPin));  // ขาเดิมไม่มีใครคุมแล้ว: ดับ
    if (doSave) save();
    Serial.printf("[RULE] set %u: %s %s %s %.3f hyst %.3f hold %ums -> P%u %s\n", i + 1, r.en ? "ON" : "off", ADS_SLOT_NAMES[r.slot], r.above ? ">" : "<",
                  r.thr, r.hyst, r.holdMs, r.pin, RULE_ACT_NAMES[r.act < 4 ? r.act : 0]);
}

void rulesSetBar(bool on, uint8_t mode, bool doSave)
{
    BusGuard g;
    const bool wasOn = gCfg.barOn;
    gCfg.barOn = on;
    gCfg.barMode = mode < static_cast<uint8_t>(BarMode::Count) ? mode : 0;
    gS.manualMask = 0;
    if (wasOn && !on) pcfSetOutMask(0);  // ปิดแถบไฟ: ดับทุกดวงก่อน กฎค่อยคุมต่อ
    if (doSave) save();
    Serial.printf("[RULE] LED bar %s mode %u (source %s)\n", on ? "ON" : "off", gCfg.barMode, ADS_SLOT_NAMES[gCfg.sel]);
}

void rulesSetInput(bool on, const uint8_t act[4], bool doSave)
{
    BusGuard g;
    gCfg.inOn = on;
    for (uint8_t i = 0; i < 4; i++) gCfg.inAct[i] = act[i] < static_cast<uint8_t>(InAct::Count) ? act[i] : 0;
    if (doSave) save();
    Serial.printf("[RULE] buttons %s: P4=%s P5=%s P6=%s P7=%s\n", on ? "ON" : "off", IN_ACT_NAMES[gCfg.inAct[0]], IN_ACT_NAMES[gCfg.inAct[1]],
                  IN_ACT_NAMES[gCfg.inAct[2]], IN_ACT_NAMES[gCfg.inAct[3]]);
}

void rulesSelect(uint8_t slot, bool doSave)
{
    if (slot >= ADS_SLOTS) return;
    BusGuard g;
    gCfg.sel = slot;
    gS.hold = false;
    gS.barPeak = 0;
    if (doSave) save();
    Serial.printf("[RULE] selected %s\n", ADS_SLOT_NAMES[slot]);
}

void rulesHold(bool on)
{
    BusGuard g;
    gS.hold = on;
    gS.heldValue = adsSlot(gCfg.sel).value;
    Serial.printf("[RULE] hold %s\n", on ? "ON" : "off");
}

void rulesManual(uint8_t pin)
{
    if (pin > 7) return;
    BusGuard g;
    if (gS.ownMask & (1 << pin)) gS.manualMask |= static_cast<uint8_t>(1 << pin);
}

void rulesResetDefaults()
{
    BusGuard g;
    defaults();
    memset(gS.active, 0, sizeof(gS.active));
    gS.manualMask = 0;
    gToggleLatch = 0;
    pcfSetOutMask(0);
    save();
    Serial.println("[RULE] defaults restored");
}

uint8_t rulesNextSlot(uint8_t slot)
{
    const uint8_t first = adsFirstSlot(), count = adsSlotCount();
    if (slot < first || slot + 1 >= first + count) return first;
    return static_cast<uint8_t>(slot + 1);
}

}  // namespace hal
