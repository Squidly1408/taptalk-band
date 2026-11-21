// Pin map for the proof-of-concept build (ESP32-S3-DevKitC-1).
// See hardware/README.md. Buttons, page key and charger pin must be RTC GPIOs (0-21)
// so they can wake the chip from deep sleep.
#pragma once

#include "driver/gpio.h"

#define PIN_BTN_1        GPIO_NUM_4   // top left
#define PIN_BTN_2        GPIO_NUM_5   // top right
#define PIN_BTN_3        GPIO_NUM_6   // bottom left
#define PIN_BTN_4        GPIO_NUM_7   // bottom right
#define PIN_PAGE_KEY     GPIO_NUM_15

#define PIN_EPD_SCK      GPIO_NUM_12
#define PIN_EPD_MOSI     GPIO_NUM_11
#define PIN_EPD_CS       GPIO_NUM_10
#define PIN_EPD_DC       GPIO_NUM_9
#define PIN_EPD_RST      GPIO_NUM_8
#define PIN_EPD_BUSY     GPIO_NUM_18

#define PIN_I2S_BCLK     GPIO_NUM_13
#define PIN_I2S_WS       GPIO_NUM_14
#define PIN_I2S_DOUT     GPIO_NUM_42
#define PIN_AMP_SD       GPIO_NUM_41

#define PIN_HAPTIC       GPIO_NUM_40

#define PIN_CHARGER_PG   GPIO_NUM_16  // low = sitting on a charger
#define PIN_BATT_ADC     GPIO_NUM_1   // ADC1 channel 0, through a 1:2 divider
