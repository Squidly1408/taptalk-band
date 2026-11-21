#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "events.h"
#include "pins.h"
#include "power.h"

static const char *TAG = "power";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;

static void IRAM_ATTR charger_isr(void *arg)
{
    events_post_from_isr(EVT_CHARGER_CHANGED, gpio_get_level(PIN_CHARGER_PG) == 0);
}

void power_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_CHARGER_PG,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    gpio_isr_handler_add(PIN_CHARGER_PG, charger_isr, NULL);

    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));
    adc_oneshot_chan_cfg_t chan = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &chan));
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = adc_cali_create_scheme_curve_fitting(&cal, &s_cali) == ESP_OK;
}

bool power_on_charger(void)
{
    return gpio_get_level(PIN_CHARGER_PG) == 0;
}

int power_battery_percent(void)
{
    int raw = 0, mv = 0;
    if (adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw) != ESP_OK) {
        return -1;
    }
    if (s_cali_ok) {
        adc_cali_raw_to_voltage(s_cali, raw, &mv);
    } else {
        mv = raw * 3100 / 4095;
    }
    int cell_mv = mv * 2;  // 1:2 divider

    // Rough LiPo discharge curve (resting voltage). Good enough for a 5-step gauge.
    static const int table[][2] = {
        { 4150, 100 }, { 4000, 85 }, { 3870, 65 }, { 3790, 45 },
        { 3730, 25 }, { 3680, 10 }, { 3500, 0 },
    };
    if (cell_mv >= table[0][0]) {
        return 100;
    }
    for (int i = 1; i < (int)(sizeof table / sizeof table[0]); i++) {
        if (cell_mv >= table[i][0]) {
            int v0 = table[i][0], v1 = table[i - 1][0];
            int p0 = table[i][1], p1 = table[i - 1][1];
            return p0 + (cell_mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0;
}

void power_enter_ship_mode(void)
{
    // Depends on the charger IC chosen for the production board (for example a
    // ship-mode bit over I2C, or a long pulse on a dedicated pin).
    ESP_LOGW(TAG, "ship mode not implemented for the proof-of-concept charger board");
}
