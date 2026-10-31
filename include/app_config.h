#pragma once

// =====================================================================
//  app_config.h -- ค่าที่ "ปรับได้" ของ Lab_Template_Start (แก้แล้ว upload ใหม่ได้เลย)
//
//  board_config.h = ข้อเท็จจริงของฮาร์ดแวร์ (ไม่แก้)
//  app_config.h   = ค่าที่อยากลองปรับ
// =====================================================================

#include <Arduino.h>

namespace app {

inline constexpr char APP_NAME[] = "ADS + PCF Combo Lab";
inline constexpr char FW_VERSION[] = "v1.0.0";
inline constexpr char NVS_NAMESPACE[] = "labstart";  // ชื่อที่เก็บค่าถาวร (เปลี่ยนชื่อ = ค่าเดิมหายทั้งหมด)

constexpr uint32_t SERIAL_BAUD = 115200;   // ต้องตรงกับ monitor_speed ใน platformio.ini
constexpr uint32_t STATUS_PRINT_MS = 5000; // พิมพ์ [STATUS] ออก Serial ทุกกี่ ms (0 = ปิด)

// ---- บัส I2C ร่วม (SDA=IO27 / SCL=IO22) ADS1x15 + PCF8574 ----
// PCF8574 รองรับสูงสุด 100 kHz ตาม datasheet / ADS1x15 ได้ถึง 400 kHz  -> บัสร่วมต้องใช้ความเร็วของตัวที่ช้าที่สุด
// สลับเป็น 400 kHz ได้บนแท็บ "บัส" (ลองเทียบ % การใช้บัส) แต่ PCF8574 บางตัวอาจตอบผิดพลาด
constexpr uint32_t I2C_FREQ_SLOW_HZ = 100000;
constexpr uint32_t I2C_FREQ_FAST_HZ = 400000;
constexpr uint32_t I2C_TASK_MAX_SLEEP_MS = 5;  // task "i2c" หลับนานสุดกี่ ms (ปุ่ม PCF / กฎ ตอบสนองช้าสุดเท่านี้)

// ---- PCF8574 ----
constexpr uint32_t PCF_POLL_MS = 20;          // โหมด polling: อ่านขาเข้าทุกกี่ ms
constexpr uint32_t PCF_INT_SAFETY_MS = 1000;  // โหมด INT: อ่านซ้ำเผื่อพลาดทุกกี่ ms
constexpr uint32_t PCF_RESCAN_MS = 2000;      // หา PCF8574 ไม่เจอ -> สแกนใหม่ทุกกี่ ms
constexpr uint8_t PCF_MAX_ERRORS = 3;         // อ่าน/เขียนพลาดติดกันกี่ครั้งจึงถือว่า "หลุด"

// ---- กฎ (ตัวเปรียบเทียบเสมือน) + แถบไฟ ----
constexpr uint32_t RULE_EVAL_MS = 10;         // ตรวจกฎทุกกี่ ms
constexpr uint32_t RULE_BLINK_MS = 250;       // กะพริบ: ติด/ดับ ครึ่งรอบกี่ ms
constexpr uint32_t BAR_PEAK_HOLD_MS = 600;    // แถบไฟแบบ VU: ค้างจุดสูงสุดกี่ ms ก่อนลดลง
constexpr uint32_t BAR_PEAK_FALL_MS = 150;    // VU: จุดสูงสุดลดลง 1 ดวงทุกกี่ ms

// ---- ADS1115 / ADS1015 ----
constexpr uint32_t ADS_RESCAN_MS = 2000;      // หา ADS ไม่เจอ -> สแกนใหม่ทุกกี่ ms
constexpr uint8_t ADS_MAX_ERRORS = 3;         // อ่าน/เขียนพลาดติดกันกี่ครั้งจึงถือว่า "หลุด"
constexpr uint16_t ADS_DETECT_SPLIT_US = 700; // ตรวจชิป: แปลงที่อัตราสูงสุดเร็วกว่านี้ = ADS1015 (~0.3 ms) ช้ากว่า = ADS1115 (~1.2 ms)
constexpr uint32_t ADS_DEMO_MS = 20;          // โหมดจำลอง: สร้างค่าใหม่ทุกกี่ ms
constexpr uint32_t UI_REFRESH_MS = 100;       // แท็บรวม/ช่อง อัปเดตตัวเลขทุกกี่ ms

// ---- จอสัมผัส XPT2046 ----
constexpr uint32_t TOUCH_POLL_MS = 5;         // อ่านจอสัมผัสทุกกี่ ms
constexpr int TOUCH_Z_MIN = 400;              // แรงกดขั้นต่ำที่ถือว่า "แตะ" (เพิ่ม = ต้องกดแรงขึ้น)
constexpr uint8_t TOUCH_PRESS_SAMPLES = 2;    // เห็นการแตะติดกันกี่ครั้งจึงนับว่า "กดลง"
constexpr uint8_t TOUCH_RELEASE_SAMPLES = 3;  // ไม่เห็นการแตะติดกันกี่ครั้งจึงนับว่า "ปล่อย"
constexpr float TOUCH_SMOOTH_ALPHA = 0.6f;    // ตัวกรองพิกัดขณะลาก (0.0-1.0 ยิ่งน้อยยิ่งนิ่ง แต่ตามนิ้วช้า)

// ---- การ calibrate แบบแม่นยำ (5 จุด + affine least-squares + จุดตรวจสอบ) ----
constexpr int CAL_MARGIN = 28;            // ระยะเป้าจากขอบจอ (px) ขอบจอ resistive มักไม่เป็นเชิงเส้น จึงไม่ชิดขอบเกินไป
constexpr uint8_t CAL_SETTLE_SAMPLES = 4; // ทิ้งค่าช่วงแรกที่นิ้วเพิ่งแตะ (แรงกดยังไม่นิ่ง)
constexpr uint8_t CAL_SAMPLES = 16;       // จำนวนตัวอย่างต่อเป้า (ใช้ค่ามัธยฐาน)
constexpr int CAL_MAX_SPREAD_RAW = 30;    // ค่าดิบแกว่ง (IQR) เกินนี้ = กดไม่นิ่ง ให้กดใหม่
constexpr int CAL_MIN_SPAN = 300;         // ช่วงค่าดิบระหว่างเป้าขั้นต่ำ (กันแตะผิดจุด)
constexpr float CAL_MAX_FIT_ERROR_PX = 6.0f;  // ความคลาดเคลื่อนสูงสุดของจุด calibrate หลัง fit
constexpr float CAL_MAX_VERIFY_PX = 7.0f;     // ความคลาดเคลื่อนสูงสุดของจุดตรวจสอบ (จุดที่ไม่ได้ใช้คำนวณ)
constexpr uint32_t CAL_CANCEL_MS = 20000;     // calibrate ซ้ำ (มีค่าเดิมอยู่) แล้วไม่แตะนานเกินนี้ = ยกเลิก ใช้ค่าเดิม

// ---- ไฟหลังจอ (PWM ที่ GPIO21) ----
constexpr uint32_t BL_PWM_FREQ_HZ = 5000;
constexpr uint8_t BL_PWM_BITS = 8;
constexpr uint8_t BL_CHANNEL = 2;  // LEDC ช่อง 2 (timer 1)
constexpr int BL_MIN_PERCENT = 10; // ต่ำสุด (กันจอมืดจนมองไม่เห็นปุ่ม)
constexpr int BL_DEFAULT_PERCENT = 80;

// ---- RGB LED (PWM 8 บิต) ช่อง 0,1 = timer 0 / ช่อง 6 = timer 3 (ไม่ชนกับไฟหลังจอและลำโพง) ----
constexpr uint8_t LED_CH_R = 0;
constexpr uint8_t LED_CH_G = 1;
constexpr uint8_t LED_CH_B = 6;
constexpr uint32_t LED_PWM_FREQ_HZ = 5000;
constexpr uint32_t LED_TEST_STEP_MS = 700;  // ทดสอบอัตโนมัติ: เปลี่ยนสีทุกกี่ ms

// ---- เสียง (ลำโพง GPIO26, PWM คลื่นสี่เหลี่ยม) ----
constexpr uint8_t AUDIO_CHANNEL = 4;  // LEDC ช่อง 4 (timer 2) ห้ามใช้ช่อง 2-3 เพราะ timer เดียวกับไฟหลังจอ
constexpr uint8_t AUDIO_BITS = 10;
constexpr uint16_t VOLUME_DUTY[4] = {0, 24, 96, 512};  // OFF / LOW / MID / HIGH (512 = duty 50% = ดังสุด)
constexpr uint8_t VOLUME_DEFAULT = 2;                  // MID
constexpr bool CLICK_SOUND = true;                     // เสียงติ๊กทุกครั้งที่แตะจอ

}  // namespace app
