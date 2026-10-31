#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "ADC": ปุ่มย่อย 4 ปุ่ม  ค่า | ช่อง | กราฟ | ตั้งค่า
//  แต่ละส่วนคือหน้าเดิมจากแล็บ ADS1115 (ui_overview / ui_detail / ui_chart / ui_settings)
//
//  RAM ไม่พอสร้างครบทุกหน้าพร้อมกัน (แล็บนี้มีหน้าของ PCF + กฎ + บัสเพิ่ม) จึง "สร้างเมื่อเปิดดู / ลบเมื่อออก":
//    เปลี่ยนส่วนย่อย -> หยุดตัวจับเวลาของหน้าเก่า (close) -> ลบ widget เก่าแบบ async -> สร้างหน้าใหม่
//    ลบแบบ async เพราะอาจถูกเรียกจาก event ของ widget ในหน้าเก่าเอง (เช่น แตะการ์ดในหน้า "ค่า" -> ไปหน้า "ช่อง")
//  ผลข้างเคียง: กราฟเริ่มวาดใหม่ทุกครั้งที่กลับเข้าหน้ากราฟ
// =====================================================================

namespace {

constexpr int SUB_COUNT = 4;
lv_obj_t *gSubBtn[SUB_COUNT], *gSub[SUB_COUNT];
int gBuilt = -1;

void onSub(lv_event_t *e)
{
    ui_adc_show(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

void closePage(int i)
{
    void (*closers[SUB_COUNT])() = {ui_overview_close, ui_detail_close, ui_chart_close, ui_settings_close};
    closers[i]();
    // ลบลูกทุกตัวของกล่องแบบ async (ไล่จากท้าย: ดัชนีของลูกที่เหลือไม่ขยับ)
    for (int k = static_cast<int>(lv_obj_get_child_cnt(gSub[i])) - 1; k >= 0; k--) {
        lv_obj_t *c = lv_obj_get_child(gSub[i], k);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(c);
    }
}

}  // namespace

void ui_adc_show(int index)
{
    if (index < 0 || index >= SUB_COUNT) return;
    if (gBuilt != index) {
        if (gBuilt >= 0) closePage(gBuilt);
        void (*builders[SUB_COUNT])(lv_obj_t *) = {ui_overview_tab, ui_detail_tab, ui_chart_tab, ui_settings_tab};
        const uint32_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        builders[index](gSub[index]);
        gBuilt = index;
        Serial.printf("[UI] adc page %d built: %d bytes, heap8 %u KB\n", index, static_cast<int>(before - heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    }
    ui_show_sub(gSub, gSubBtn, SUB_COUNT, index);
}

void ui_adc_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 4, 0);
    lv_obj_set_style_pad_row(tab, 4, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const names[SUB_COUNT] = {"ค่า", "ช่อง", "กราฟ", "ตั้งค่า"};
    ui_subnav(tab, names, SUB_COUNT, gSubBtn, onSub);
    for (int i = 0; i < SUB_COUNT; i++) {
        lv_obj_t *box = lv_obj_create(tab);
        lv_obj_remove_style_all(box);
        lv_obj_set_width(box, LV_PCT(100));
        lv_obj_set_flex_grow(box, 1);  // = ความสูงที่เหลือใต้แถวปุ่ม (~164 px)
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, 4, 0);
        lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
        gSub[i] = box;
    }
    ui_adc_show(0);
}
