#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "บัส": แผนภาพบัส I2C ที่เคลื่อนไหวตามธุรกรรมจริง
//    CYD --SDA/SCL-- ADS1x15 / PCF8574
//    แพ็กเก็ต: ฟ้า = เขียนไป ADS, เขียว = อ่านจาก ADS, ส้ม = เขียนไป PCF, เขียวน้ำทะเล = อ่านจาก PCF, แดง = ไม่มี ACK
//    (ธุรกรรมจริงหลายร้อยครั้ง/วินาที วาดทุกอันไม่ไหว จึงสุ่มมาวาด ~8 ครั้ง/วินาที ต่อชิป)
//    ใต้แผนภาพ: อุปกรณ์ที่พบ / TimeUp (ธุรกรรมต่อวินาที) / ผิดพลาด / Traffic (% เวลาที่บัสไม่ว่าง) + ตั้งค่าบัส + วิธีต่อสาย
// =====================================================================

namespace {

constexpr int CYD_W = 64, SDA_Y = 20, SCL_Y = 36, BOX_Y = 64, BOX_H = 50;
constexpr int ADS_X = 76, ADS_W = 112, PCF_X = 196, PCF_W = 112;
constexpr int ADS_SDA_X = ADS_X + 26, ADS_SCL_X = ADS_X + 40, PCF_SDA_X = PCF_X + 26, PCF_SCL_X = PCF_X + 40;

struct Pk {
    lv_obj_t *obj;
    int stubX;
    bool toChip, busy;
};
constexpr int POOL = 8;
Pk gPk[POOL];

lv_obj_t *gAdsBox, *gPcfBox, *gAdsName, *gAdsInfo, *gAdsRate, *gPcfName, *gPcfInfo, *gPcfRate;
lv_obj_t *gStatDev, *gStatTx, *gStatErr, *gStatLoad;
lv_obj_t *gClk[2], *gSwDemo, *gLive, *gWarn, *gDevList;
uint32_t gSince = 0, gLastErr = 0;

// ---------- แพ็กเก็ตวิ่ง: แนวนอนบน SDA จาก CYD ไปถึงขาของชิป แล้วลงแนวตั้งเข้าชิป (อ่าน = ย้อนทาง) ----------
void pkExec(void *var, int32_t v)
{
    Pk *p = static_cast<Pk *>(var);
    if (!p->toChip) v = 1000 - v;
    const int lh = p->stubX - CYD_W, lv = BOX_Y - SDA_Y;
    const int d = static_cast<int>(v) * (lh + lv) / 1000;
    if (d < lh) lv_obj_set_pos(p->obj, static_cast<lv_coord_t>(CYD_W + d - 5), SDA_Y - 4);
    else lv_obj_set_pos(p->obj, static_cast<lv_coord_t>(p->stubX - 5), static_cast<lv_coord_t>(SDA_Y - 4 + d - lh));
}

void pkDone(lv_anim_t *a)
{
    Pk *p = static_cast<Pk *>(a->var);
    lv_obj_add_flag(p->obj, LV_OBJ_FLAG_HIDDEN);
    p->busy = false;
}

void spawn(const hal::BusPacket &bp)
{
    Pk *p = nullptr;
    for (Pk &k : gPk) {
        if (!k.busy) {
            p = &k;
            break;
        }
    }
    if (p == nullptr) return;
    const bool ads = hal::busDevOf(bp.addr) == hal::BusDev::Ads;
    p->busy = true;
    p->toChip = bp.write;
    p->stubX = ads ? ADS_SDA_X : PCF_SDA_X;
    uint32_t col = !bp.ok ? UI_BAD : ads ? (bp.write ? UI_ACCENT : UI_OK) : (bp.write ? UI_OUT : 0x2DD4BF);
    lv_obj_set_style_bg_color(p->obj, lv_color_hex(col), 0);
    lv_obj_clear_flag(p->obj, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, p);
    lv_anim_set_exec_cb(&a, pkExec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_time(&a, ads ? 650 : 800);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&a, pkDone);
    lv_anim_start(&a);
    ui_flash(ads ? gAdsBox : gPcfBox, ads ? 0x1B3E5A : 0x4A3A12, UI_CARD, 300);
}

void pullPackets()
{
    hal::BusPacket buf[32];
    uint8_t got = 0;
    gSince = hal::busPackets(gSince, buf, 32, got);
    if (ui_active_tab() != UI_TAB_BUS) return;  // ไม่ได้ดูแท็บนี้: ไม่ต้องวาด
    // เลือกล่าสุดของแต่ละชิป (มีพลาด = เลือกอันที่พลาด ให้เห็นสีแดง)
    int ads = -1, pcf = -1;
    for (int i = 0; i < got; i++) {
        const hal::BusDev d = hal::busDevOf(buf[i].addr);
        if (d == hal::BusDev::Ads && (ads < 0 || buf[ads].ok)) ads = i;
        if (d == hal::BusDev::Pcf && (pcf < 0 || buf[pcf].ok)) pcf = i;
    }
    if (ads >= 0) spawn(buf[ads]);
    if (pcf >= 0) spawn(buf[pcf]);
}

// ---------- ตัวเลข ----------
void setStat(lv_obj_t *l, const char *text, uint32_t color)
{
    ui_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
}

void tick(lv_timer_t *)
{
    pullPackets();
    const hal::BusState &b = hal::bus();
    static uint32_t scans = 0;
    if (b.scans != scans) {  // สแกนใหม่ (เองหรือจากปุ่ม) -> รายชื่ออุปกรณ์ใหม่
        scans = b.scans;
        ui_busmap_refresh();
    }
    const hal::AdsState &a = hal::ads();
    const hal::PcfState &p = hal::pcf();
    char buf[48];
    snprintf(buf, sizeof(buf), "%u ตัว", b.count);
    setStat(gStatDev, buf, b.count ? UI_OK : UI_BAD);
    snprintf(buf, sizeof(buf), "%u/วิ", b.txPerSec);
    setStat(gStatTx, buf, UI_TEXT);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(b.errors));
    setStat(gStatErr, buf, b.errors ? UI_BAD : UI_OK);
    if (b.errors != gLastErr) {
        ui_flash(lv_obj_get_parent(gStatErr), 0x5A1F24, UI_CARD, 500);
        gLastErr = b.errors;
    }
    snprintf(buf, sizeof(buf), "%u%%", b.loadPct);
    setStat(gStatLoad, buf, b.loadPct > 70 ? UI_BAD : b.loadPct > 40 ? UI_OUT : UI_TEXT);

    // กล่องชิป
    if (a.found) snprintf(buf, sizeof(buf), "0x%02X %uSPS", a.addr, hal::adsSps());
    else snprintf(buf, sizeof(buf), a.demo ? "จำลอง" : "ไม่พบ");
    ui_set_text(gAdsInfo, buf);
    snprintf(buf, sizeof(buf), "%u/วิ", b.adsPerSec);
    ui_set_text(gAdsRate, buf);
    ui_set_text(gAdsName, a.found || a.demo ? hal::adsChipName(a.chip) : "ADS1x15");
    lv_obj_set_style_border_color(gAdsBox, lv_color_hex(a.found ? UI_ACCENT : a.demo ? UI_MUTED : UI_BAD), 0);
    if (p.found) snprintf(buf, sizeof(buf), "0x%02X IO 0x%02X", p.addr, static_cast<unsigned>((p.outByte & ~p.dirMask) | (p.inByte & p.dirMask)));
    else snprintf(buf, sizeof(buf), "ไม่พบ");
    ui_set_text(gPcfInfo, buf);
    snprintf(buf, sizeof(buf), "%u/วิ", b.pcfPerSec);
    ui_set_text(gPcfRate, buf);
    ui_set_text(gPcfName, p.found && p.addr >= 0x38 ? "PCF8574A" : "PCF8574");
    lv_obj_set_style_border_color(gPcfBox, lv_color_hex(p.found ? UI_OUT : UI_BAD), 0);

    const bool low = hal::busIntLow();
    snprintf(buf, sizeof(buf), "IO35 = %s  |  RDY พลาด %lu  |  INT %lu", low ? "LOW" : "HIGH", static_cast<unsigned long>(a.rdyTimeouts),
             static_cast<unsigned long>(p.ints));
    ui_set_text(gLive, buf);

    const char *warn = "";
    if (p.intStuckMs > 500) warn = "INT ค้าง LOW: ต่อ 10k จาก IO35 ไป 3.3V หรือยัง?";
    else if (hal::busFast() && p.found) warn = "400 kHz เกินสเปก PCF8574 (100 kHz) ดูช่องผิดพลาด";
    ui_set_text(gWarn, warn);
}

// ---------- ปุ่ม ----------
void onScan(lv_event_t *)
{
    hal::adsScan();
    hal::pcfScan();
    ui_ads_changed();
    hal::soundTone(1568, 40);
}

void onClk(lv_event_t *e)
{
    hal::busSetFast(lv_event_get_user_data(e) != nullptr);
    ui_busmap_refresh();
}

void onDemo(lv_event_t *e)
{
    hal::adsSetDemo(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    ui_ads_changed();
}

// ---------- สร้าง ----------
lv_obj_t *wire(lv_obj_t *parent, int x, int y, int w, int h, uint32_t col)
{
    lv_obj_t *o = ui_box(parent, col, 0);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *chip(lv_obj_t *parent, int x, int w, uint32_t col, lv_obj_t **name, lv_obj_t **info, lv_obj_t **rate)
{
    lv_obj_t *b = ui_box(parent, UI_CARD, 7);
    lv_obj_set_size(b, w, BOX_H);
    lv_obj_set_pos(b, x, BOX_Y);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_pad_hor(b, 5, 0);
    *name = ui_label(b, "", &lv_font_montserrat_14, lv_color_hex(col));
    lv_obj_align(*name, LV_ALIGN_TOP_LEFT, 0, 0);
    *info = ui_label(b, "", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    lv_obj_set_style_text_font(*info, &font_th_16, 0);
    lv_obj_align(*info, LV_ALIGN_LEFT_MID, 0, 1);
    *rate = ui_label(b, "", nullptr, lv_color_hex(UI_TEXT));
    lv_obj_align(*rate, LV_ALIGN_BOTTOM_LEFT, 0, 3);
    return b;
}

lv_obj_t *statChip(lv_obj_t *row, const char *title)
{
    lv_obj_t *c = ui_box(row, UI_CARD, 6);
    lv_obj_set_size(c, 10, 44);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_pad_hor(c, 5, 0);
    lv_obj_t *t = ui_label(c, title, nullptr, lv_color_hex(UI_MUTED));
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, -2);
    lv_obj_t *v = ui_label(c, "--", nullptr, lv_color_hex(UI_TEXT));
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, 1);
    return v;
}

void buildDiagram(lv_obj_t *tab)
{
    lv_obj_t *d = lv_obj_create(tab);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, LV_PCT(100), BOX_Y + BOX_H + 2);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cyd = ui_box(d, UI_CARD, 7);
    lv_obj_set_size(cyd, CYD_W, 62);
    lv_obj_set_pos(cyd, 0, 2);
    lv_obj_set_style_border_width(cyd, 2, 0);
    lv_obj_set_style_border_color(cyd, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_pad_hor(cyd, 4, 0);
    lv_obj_t *l = ui_label(cyd, "CYD", nullptr, lv_color_hex(UI_TEXT));
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, -2);
    l = ui_label(cyd, "SDA 27\nSCL 22", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    // SDA (ฟ้า) / SCL (เทา) วิ่งยาวทั้งบัส + ขาลงไปที่แต่ละชิป
    wire(d, CYD_W, SDA_Y, 308 - CYD_W, 2, UI_ACCENT);
    wire(d, CYD_W, SCL_Y, 308 - CYD_W, 2, UI_MUTED);
    wire(d, ADS_SDA_X, SDA_Y, 2, BOX_Y - SDA_Y, UI_ACCENT);
    wire(d, ADS_SCL_X, SCL_Y, 2, BOX_Y - SCL_Y, UI_MUTED);
    wire(d, PCF_SDA_X, SDA_Y, 2, BOX_Y - SDA_Y, UI_ACCENT);
    wire(d, PCF_SCL_X, SCL_Y, 2, BOX_Y - SCL_Y, UI_MUTED);
    l = ui_label(d, "SDA", &lv_font_montserrat_14, lv_color_hex(UI_ACCENT));
    lv_obj_set_pos(l, 272, SDA_Y - 17);  // ชิดขวาแต่ไม่ล้นขอบ (เนื้อหาแท็บกว้าง 308 px)
    l = ui_label(d, "SCL", &lv_font_montserrat_14, lv_color_hex(UI_MUTED));
    lv_obj_set_pos(l, 272, SCL_Y + 2);

    gAdsBox = chip(d, ADS_X, ADS_W, UI_ACCENT, &gAdsName, &gAdsInfo, &gAdsRate);
    gPcfBox = chip(d, PCF_X, PCF_W, UI_OUT, &gPcfName, &gPcfInfo, &gPcfRate);

    for (Pk &p : gPk) {
        p.obj = ui_box(d, UI_ACCENT, 3);
        lv_obj_set_size(p.obj, 10, 9);
        lv_obj_add_flag(p.obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(p.obj, LV_OBJ_FLAG_CLICKABLE);
        p.busy = false;
    }
}

void buildStats(lv_obj_t *tab)
{
    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    gStatDev = statChip(row, "อุปกรณ์");
    gStatTx = statChip(row, "TimeUp");
    gStatErr = statChip(row, "ผิดพลาด");
    gStatLoad = statChip(row, "Traffic");
    lv_obj_t *b = ui_btn(row, LV_SYMBOL_REFRESH, onScan);
    lv_obj_set_size(b, 40, 44);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(UI_BG), 0);
}

void buildSettings(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "ตั้งค่าบัสร่วม");
    lv_obj_t *r = ui_row(card);
    ui_label(r, "ความเร็ว", nullptr, lv_color_hex(UI_MUTED));
    gClk[0] = ui_btn(r, "100 kHz", onClk, nullptr);
    gClk[1] = ui_btn(r, "400 kHz", onClk, reinterpret_cast<void *>(1));
    gSwDemo = ui_switch_row(card, "ADS โหมดจำลอง (ไม่มีชิป)", hal::ads().demo, onDemo);
    gLive = ui_label(card, "", nullptr, lv_color_hex(UI_TEXT));
    lv_obj_set_width(gLive, LV_PCT(100));
    lv_label_set_long_mode(gLive, LV_LABEL_LONG_WRAP);
    gWarn = ui_label(card, "", nullptr, lv_color_hex(UI_BAD));
    lv_obj_set_width(gWarn, LV_PCT(100));
    lv_label_set_long_mode(gWarn, LV_LABEL_LONG_WRAP);

    card = ui_card(tab, "อุปกรณ์บนบัส");
    gDevList = ui_label(card, "", nullptr, lv_color_hex(UI_TEXT));

    card = ui_card(tab, "ความรู้: สองชิป สายคู่เดียว");
    lv_obj_t *note = ui_label(card,
                              "SDA=IO27  SCL=IO22  VCC=3.3V  GND ร่วม\n"
                              "ADS1x15 0x48-0x4B  PCF8574 0x20-0x27\n"
                              "(PCF8574A 0x38-0x3F) address ไม่ชนกัน\n"
                              "INT/ALRT เป็น open-drain ทั้งคู่ ต่อรวมกันได้\n"
                              "ขา LOW = มีใครเรียก ต้องถามทั้งสองตัว\n"
                              "IO35 ไม่มี pull-up ในตัว: ต่อ 10k ไป 3.3V\n"
                              "บัสร่วมใช้ความเร็วของตัวที่ช้าสุด (100 kHz)",
                              nullptr, lv_color_hex(UI_TEXT));
    lv_obj_set_width(note, LV_PCT(100));
}

}  // namespace

void ui_busmap_refresh()
{
    if (gDevList == nullptr) return;
    for (int i = 0; i < 2; i++) {
        if ((i == 1) == hal::busFast()) lv_obj_add_state(gClk[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(gClk[i], LV_STATE_CHECKED);
    }
    auto sw = [](lv_obj_t *o, bool on) {
        if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
        else lv_obj_clear_state(o, LV_STATE_CHECKED);
    };
    sw(gSwDemo, hal::ads().demo);

    const hal::BusState &b = hal::bus();
    char buf[200];
    int n = 0;
    if (b.count == 0) n = snprintf(buf, sizeof(buf), "ไม่พบอุปกรณ์ (ตรวจสาย/pull-up)");
    for (uint8_t i = 0; i < b.count && n < static_cast<int>(sizeof(buf)) - 40; i++) {
        const hal::BusDev d = hal::busDevOf(b.addr[i]);
        const char *name = d == hal::BusDev::Ads ? hal::adsChipName(hal::ads().found && hal::ads().addr == b.addr[i] ? hal::ads().chip : hal::AdsChip::ADS1115)
                           : d == hal::BusDev::Pcf ? (b.addr[i] >= 0x38 ? "PCF8574A" : "PCF8574")
                                                  : "อื่นๆ";
        n += snprintf(buf + n, sizeof(buf) - n, "%s0x%02X  %s", i ? "\n" : "", b.addr[i], name);
    }
    ui_set_text(gDevList, buf);
}

void ui_busmap_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_row(tab, 5, 0);
    buildDiagram(tab);
    buildStats(tab);
    buildSettings(tab);
    ui_busmap_refresh();
    lv_timer_create(tick, 120, nullptr);
    tick(nullptr);
}
