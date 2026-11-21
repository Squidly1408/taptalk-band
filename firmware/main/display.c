// SSD1681 e-paper driver (e.g. Good Display GDEY0154D67 / Waveshare 1.54 in V2).
// Command values follow the SSD1681 datasheet. Verify the update-control values
// (0xF7 full, 0xFF partial) and orientation against the exact panel you buy.
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "display.h"
#include "pins.h"

static const char *TAG = "display";
static uint8_t s_fb[DISPLAY_W * DISPLAY_H / 8];
static spi_device_handle_t s_spi;
static bool s_awake;

static void wait_busy(void)
{
    for (int i = 0; gpio_get_level(PIN_EPD_BUSY) == 1; i++) {
        if (i > 1000) {  // 5 s
            ESP_LOGW(TAG, "busy timeout");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void send(int dc, const uint8_t *buf, size_t len)
{
    if (len == 0) {
        return;
    }
    gpio_set_level(PIN_EPD_DC, dc);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = buf };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void cmd(uint8_t c) { send(0, &c, 1); }
static void dat(const uint8_t *d, size_t n) { send(1, d, n); }
static void dat1(uint8_t v) { send(1, &v, 1); }

static void set_cursor(void)
{
    cmd(0x4E);  // RAM X address counter
    dat1(0x00);
    cmd(0x4F);  // RAM Y address counter
    const uint8_t y[] = { 0x00, 0x00 };
    dat(y, sizeof y);
}

static void write_ram(uint8_t reg)
{
    set_cursor();
    cmd(reg);
    dat(s_fb, sizeof s_fb);
}

static void panel_init(void)
{
    gpio_set_level(PIN_EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();

    cmd(0x12);  // software reset
    wait_busy();

    cmd(0x01);  // driver output control: 200 gates
    const uint8_t drv[] = { 0xC7, 0x00, 0x00 };
    dat(drv, sizeof drv);

    cmd(0x11);  // data entry mode: X+, Y+
    dat1(0x03);

    cmd(0x44);  // RAM X range: bytes 0..24
    const uint8_t xr[] = { 0x00, 0x18 };
    dat(xr, sizeof xr);

    cmd(0x45);  // RAM Y range: 0..199
    const uint8_t yr[] = { 0x00, 0x00, 0xC7, 0x00 };
    dat(yr, sizeof yr);

    cmd(0x3C);  // border waveform
    dat1(0x05);

    cmd(0x18);  // internal temperature sensor
    dat1(0x80);

    set_cursor();
    wait_busy();
    s_awake = true;
}

static void ensure_awake(void)
{
    if (!s_awake) {
        panel_init();
    }
}

void display_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_EPD_DC) | (1ULL << PIN_EPD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_config_t in = {
        .pin_bit_mask = 1ULL << PIN_EPD_BUSY,
        .mode = GPIO_MODE_INPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_EPD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_EPD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof s_fb + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_EPD_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    memset(s_fb, 0xFF, sizeof s_fb);
    s_awake = false;
}

static inline void set_px(int x, int y, bool white)
{
    if (x < 0 || y < 0 || x >= DISPLAY_W || y >= DISPLAY_H) {
        return;
    }
    uint32_t idx = (uint32_t)(y * DISPLAY_W + x) >> 3;
    uint8_t mask = 0x80 >> (x & 7);
    if (white) {
        s_fb[idx] |= mask;
    } else {
        s_fb[idx] &= ~mask;
    }
}

void display_clear(bool white)
{
    memset(s_fb, white ? 0xFF : 0x00, sizeof s_fb);
}

void display_fill_rect(int x, int y, int w, int h, bool white)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            set_px(i, j, white);
        }
    }
}

void display_invert_rect(int x, int y, int w, int h)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i < 0 || j < 0 || i >= DISPLAY_W || j >= DISPLAY_H) {
                continue;
            }
            uint32_t idx = (uint32_t)(j * DISPLAY_W + i) >> 3;
            s_fb[idx] ^= 0x80 >> (i & 7);
        }
    }
}

void display_blit(int x, int y, int w, int h, const uint8_t *bits)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            uint32_t bit = (uint32_t)(j * w + i);
            bool white = bits[bit >> 3] & (0x80 >> (bit & 7));
            set_px(x + i, y + j, white);
        }
    }
}

void display_update_full(void)
{
    ensure_awake();
    cmd(0x3C);
    dat1(0x05);
    write_ram(0x24);  // new image
    write_ram(0x26);  // base image for later partial updates
    cmd(0x22);
    dat1(0xF7);       // full update with temperature-compensated waveform
    cmd(0x20);
    wait_busy();
}

void display_update_partial(void)
{
    ensure_awake();
    cmd(0x3C);
    dat1(0x80);
    write_ram(0x24);
    cmd(0x22);
    dat1(0xFF);       // partial update (display mode 2)
    cmd(0x20);
    wait_busy();
    write_ram(0x26);  // keep the base in step with what is shown
}

void display_sync_ram(void)
{
    ensure_awake();
    write_ram(0x24);
    write_ram(0x26);
}

void display_sleep(void)
{
    if (!s_awake) {
        return;
    }
    cmd(0x10);  // deep sleep mode 1 (RAM retained)
    dat1(0x01);
    s_awake = false;
}
