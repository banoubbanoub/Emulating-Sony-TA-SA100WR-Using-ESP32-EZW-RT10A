#include <Arduino.h>
#include <driver/i2s.h>

static const int I2S_PORT = I2S_NUM_0;

// Pick pins (BCK/LRCK/DOUT not needed if you only want MCLK)
static const int PIN_I2S_BCK  = -1;
static const int PIN_I2S_WS   = -1;
static const int PIN_I2S_DOUT = -1;
static const int PIN_I2S_DIN  = -1;

// Try GPIO0 for MCLK first on many ESP32 setups.
// If GPIO0 is used for boot mode, be careful with external loads.
static const int PIN_I2S_MCLK = 0;

static bool i2s_start_mclk_12288k() {
  // Use APLL for cleaner clock if available
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = 48000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 64;
  cfg.use_apll = true;                 // important for stable MCLK
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 12288000;           // force MCLK = 12.288 MHz

  if (i2s_driver_install((i2s_port_t)I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("i2s_driver_install failed");
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_I2S_BCK;
  pins.ws_io_num = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num = PIN_I2S_DIN;
  // In ESP-IDF, MCLK routing is separate on some versions.
  if (i2s_set_pin((i2s_port_t)I2S_PORT, &pins) != ESP_OK) {
    Serial.println("i2s_set_pin failed");
    return false;
  }

  // Try to enable MCLK output (API availability depends on core/IDF version)
  // Some Arduino cores support i2s_set_clk only; others have i2s_set_pin + fixed_mclk.
  if (i2s_set_clk((i2s_port_t)I2S_PORT, 48000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
    Serial.println("i2s_set_clk failed");
    return false;
  }

  // Route MCLK to a pin if supported by your core:
  // NOTE: On some cores, MCLK automatically appears on GPIO0 when fixed_mclk is set.
  // On others, you need: i2s_set_pin + i2s_set_gpio (not always exposed).

  // Start TX with silence so clock runs
  uint32_t zero = 0;
  size_t wr = 0;
  for (int i = 0; i < 20; i++) {
    i2s_write((i2s_port_t)I2S_PORT, &zero, sizeof(zero), &wr, portMAX_DELAY);
  }

  Serial.println("I2S MCLK should be running (scope the MCLK pin).");
  return true;
}