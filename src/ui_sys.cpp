#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "ระบบ": ปุ่มย่อย 5 ปุ่ม  จอ | สัมผัส | RGB | เสียง | บอร์ด
//  แต่ละส่วนคือหน้าเดิมจาก Lab_Template_Start (ui_display / ui_touch / ui_led / ui_sound / ui_system)
//  "สัมผัส" = Calibration ตั้งค่าการ Touch + ทดสอบ 9 จุด (ต้องมีในทุก Lab)
//
//  แล็บนี้ RAM ตึง (ทั้ง 5 ส่วนรวม ~60 KB) จึงสร้างทีละส่วนเมื่อเปิดดู แล้วลบเมื่อเปลี่ยนส่วน
//  ไฟล์ของแต่ละส่วนสร้าง lv_timer เองโดยไม่มีฟังก์ชันปิด: เราจด "ตัวจับเวลาที่เกิดขึ้นระหว่างสร้าง"
//  ไว้ แล้วลบทิ้งพร้อม widget (ไม่ต้องแก้ไฟล์ของส่วนนั้น)
// =====================================================================

namespace {

constexpr int SUB_COUNT = 5;
constexpr int MAX_TIMERS = 4;
lv_obj_t *gTab, *gSubBtn[SUB_COUNT], *gSub[SUB_COUNT];
lv_timer_t *gTimers[MAX_TIMERS];
int gTimerCount = 0, gBuilt = -1;

void onSub(lv_event_t *e)
{
    ui_sys_show(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

bool known(lv_timer_t *t, lv_timer_t *const *list, int n)
{
    for (int i = 0; i < n; i++) {
        if (list[i] == t) return true;
    }
    return false;
}

void closeSection()
{
    if (gBuilt < 0) return;
    for (int i = 0; i < gTimerCount; i++) lv_timer_del(gTimers[i]);
    gTimerCount = 0;
    for (int k = static_cast<int>(lv_obj_get_child_cnt(gSub[gBuilt])) - 1; k >= 0; k--) {
        lv_obj_t *c = lv_obj_get_child(gSub[gBuilt], k);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(c);  // อาจอยู่ใน event ของ widget ในส่วนนี้
    }
    gBuilt = -1;
}

void buildSection(int index)
{
    void (*builders[SUB_COUNT])(lv_obj_t *) = {ui_display_tab, ui_touch_tab, ui_led_tab, ui_sound_tab, ui_system_tab};
    // จดตัวจับเวลาที่มีอยู่ก่อน -> ตัวที่เพิ่มขึ้นหลังสร้าง = ของส่วนนี้
    lv_timer_t *before[48];
    int nb = 0;
    for (lv_timer_t *t = lv_timer_get_next(nullptr); t != nullptr && nb < 48; t = lv_timer_get_next(t)) before[nb++] = t;
    const uint32_t heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    gBuilt = index;  // ตั้งก่อนสร้าง: ระหว่างสร้าง ส่วนนั้นเรียก ui_touch_refresh()/ui_display_refresh() เอง
    builders[index](gSub[index]);
    gTimerCount = 0;
    for (lv_timer_t *t = lv_timer_get_next(nullptr); t != nullptr; t = lv_timer_get_next(t)) {
        if (!known(t, before, nb) && gTimerCount < MAX_TIMERS) gTimers[gTimerCount++] = t;
    }
    Serial.printf("[UI] system section %d built: %d bytes, %d timer(s), heap8 %u KB\n", index,
                  static_cast<int>(heap - heap_caps_get_free_size(MALLOC_CAP_8BIT)), gTimerCount,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
}

}  // namespace

bool ui_sys_built(int index)
{
    return gBuilt == index;
}

void ui_sys_show(int index)
{
    if (index < 0 || index >= SUB_COUNT) return;
    if (gBuilt != index) {
        closeSection();
        buildSection(index);
    }
    ui_show_sub(gSub, gSubBtn, SUB_COUNT, index);
    lv_obj_scroll_to_y(gTab, 0, LV_ANIM_OFF);
}

void ui_sys_tab(lv_obj_t *tab)
{
    gTab = tab;
    static const char *const names[SUB_COUNT] = {"จอ", "สัมผัส", "RGB", "เสียง", "บอร์ด"};
    ui_subnav(tab, names, SUB_COUNT, gSubBtn, onSub);
    for (int i = 0; i < SUB_COUNT; i++) {
        lv_obj_t *box = lv_obj_create(tab);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, 5, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
        gSub[i] = box;
    }
    ui_sys_show(1);  // เริ่มที่ "สัมผัส" (Calibration)
}
