// Audio output: I2S class-D amp, 16 kHz mono.
//
// TODO: decode Ogg/Opus clips. Add the espressif/esp_audio_codec component
// (idf.py add-dependency "espressif/esp_audio_codec") and feed decoded PCM to
// play_pcm(). Until then every clip plays as a short two-tone beep.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "audio.h"
#include "pins.h"

#define SAMPLE_RATE 16000

typedef struct {
    char path[96];   // empty = beep
    uint16_t freq;
    uint16_t ms;
} audio_req_t;

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx;
static QueueHandle_t s_queue;
static volatile int s_volume = 70;

static void play_tone(uint16_t freq, uint16_t ms)
{
    int16_t buf[256];
    uint32_t total = (uint32_t)SAMPLE_RATE * ms / 1000;
    float amp = 12000.0f * s_volume / 100.0f;
    uint32_t n = 0;
    while (n < total) {
        size_t chunk = total - n < 256 ? total - n : 256;
        for (size_t i = 0; i < chunk; i++, n++) {
            // short fade in/out to avoid clicks
            float env = 1.0f;
            if (n < 160) env = n / 160.0f;
            if (total - n < 160) env = (total - n) / 160.0f;
            buf[i] = (int16_t)(amp * env * sinf(2.0f * (float)M_PI * freq * n / SAMPLE_RATE));
        }
        size_t written;
        i2s_channel_write(s_tx, buf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
    }
}

static void audio_task(void *arg)
{
    audio_req_t req;
    for (;;) {
        xQueueReceive(s_queue, &req, portMAX_DELAY);
        if (s_volume == 0) {
            continue;
        }
        gpio_set_level(PIN_AMP_SD, 1);
        i2s_channel_enable(s_tx);
        if (req.path[0]) {
            struct stat st;
            if (stat(req.path, &st) == 0) {
                ESP_LOGD(TAG, "clip %s (%ld bytes): Opus decode not yet implemented", req.path, (long)st.st_size);
            }
            play_tone(660, 120);
            play_tone(880, 160);
        } else {
            play_tone(req.freq, req.ms);
        }
        i2s_channel_disable(s_tx);
        gpio_set_level(PIN_AMP_SD, 0);
    }
}

void audio_init(void)
{
    gpio_config_t sd = { .pin_bit_mask = 1ULL << PIN_AMP_SD, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&sd));
    gpio_set_level(PIN_AMP_SD, 0);

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &s_tx, NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));

    s_queue = xQueueCreate(4, sizeof(audio_req_t));
    xTaskCreate(audio_task, "audio", 4096, NULL, 8, NULL);
}

void audio_set_volume(int percent)
{
    s_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

void audio_play_clip(const char *path)
{
    audio_req_t req = { 0 };
    snprintf(req.path, sizeof req.path, "%s", path ? path : "");
    if (!req.path[0]) {
        req.freq = 880;
        req.ms = 150;
    }
    xQueueSend(s_queue, &req, 0);
}

void audio_beep(uint16_t freq_hz, uint16_t ms)
{
    audio_req_t req = { .freq = freq_hz, .ms = ms };
    xQueueSend(s_queue, &req, 0);
}
