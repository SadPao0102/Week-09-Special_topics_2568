#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "LED_PATTERNS"

// === Pin & LEDC config ===
#define LED1_GPIO GPIO_NUM_2
#define LED2_GPIO GPIO_NUM_4
#define LED3_GPIO GPIO_NUM_5
static const gpio_num_t LEDS[] = { LED1_GPIO, LED2_GPIO, LED3_GPIO };
static const size_t NLEDS = sizeof(LEDS)/sizeof(LEDS[0]);

// LEDC timer: 5 kHz, 13-bit resolution (0..8191)
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ     5000
#define LEDC_DUTY_BITS   LEDC_TIMER_13_BIT
#define LEDC_MAX_DUTY    ((1 << LEDC_DUTY_BITS) - 1)

// ถ้า LED ของบอร์ดเป็น Active-Low ให้เปลี่ยนเป็น 1
#define ACTIVE_LOW 0  

// เลือกลวดลายที่ต้องการรัน
typedef enum { PATTERN_KNIGHT, PATTERN_BINARY, PATTERN_RANDOM } pattern_t;
static const pattern_t RUN_PATTERN = PATTERN_KNIGHT;

// พารามิเตอร์หายใจ
#define BREATH_PERIOD_MS 1600
#define TICK_MS          16

// ===== Utilities =====
static inline uint32_t ms() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline float ease_sine(float t01) {
    return 0.5f * (1.0f - cosf(2.0f * (float)M_PI * t01));
}

static inline uint32_t breath_duty_from_time(uint32_t t_ms) {
    float phase = (float)(t_ms % BREATH_PERIOD_MS) / (float)BREATH_PERIOD_MS;
    float b = ease_sine(phase);
    return (uint32_t)(b * LEDC_MAX_DUTY);
}

static void ledc_setup_all(void) {
    ledc_timer_config_t tcfg = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_BITS,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    for (int i = 0; i < (int)NLEDS; ++i) {
        ledc_channel_config_t ccfg = {
            .gpio_num       = LEDS[i],
            .speed_mode     = LEDC_MODE,
            .channel        = (ledc_channel_t)i,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER,
            .duty           = 0,
            .hpoint         = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
    }
}

static inline void set_led_inv(int idx, uint32_t duty) {
    if (ACTIVE_LOW) duty = LEDC_MAX_DUTY - duty;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, (ledc_channel_t)idx, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, (ledc_channel_t)idx));
}

static inline void set_all(uint32_t duty) {
    for (int i = 0; i < (int)NLEDS; ++i) set_led_inv(i, duty);
}

// ===== Patterns =====

// Knight Rider หายใจ
static void pattern_knight(void) {
    int dir = 1, idx = 0;
    const float tail_decay = 0.45f;
    const int   segments   = 2;

    uint32_t t0 = ms();
    while (1) {
        uint32_t t = ms() - t0;
        uint32_t base = breath_duty_from_time(t);

        set_all(0);
        set_led_inv(idx, base);

        for (int s = 1; s <= segments; ++s) {
            float factor = powf(tail_decay, (float)s);
            uint32_t d = (uint32_t)(base * factor);

            int left  = idx - s;
            int right = idx + s;
            if (left  >= 0)          set_led_inv(left,  d);
            if (right < (int)NLEDS)  set_led_inv(right, d);
        }

        idx += dir;
        if (idx == (int)NLEDS - 1) dir = -1;
        else if (idx == 0)         dir =  1;

        vTaskDelay(pdMS_TO_TICKS(150)); // ช้าหน่อยเพื่อเห็นการวิ่ง
    }
}

// (ยังเก็บ Binary / Random ได้ แต่ย่อออกเพื่อโฟกัส Knight Rider)
static void pattern_binary(void) { set_all(0); }
static void pattern_random(void) { set_all(0); }

// ===== Main =====
static void pattern_task(void *pv) {
    switch (RUN_PATTERN) {
        case PATTERN_KNIGHT:  ESP_LOGI(TAG, "Pattern: Knight Rider");  pattern_knight();  break;
        case PATTERN_BINARY:  ESP_LOGI(TAG, "Pattern: Binary Counter"); pattern_binary();  break;
        case PATTERN_RANDOM:  ESP_LOGI(TAG, "Pattern: Random Blink");   pattern_random();  break;
        default: set_all(0); break;
    }
    vTaskDelete(NULL);
}

void app_main(void) {
    ledc_setup_all();
    xTaskCreate(pattern_task, "pattern_task", 4096, NULL, 5, NULL);
}