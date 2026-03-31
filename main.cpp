
/*
#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "Audio.h"
#include "TAS5749M_I2C.h"



const char* ssid     = "Linksys19102";
const char* password = "lzcrcm7fas";





#define I2S_BCLK   26
#define I2S_LRCLK  25
#define I2S_DATA   27   // 🔴 FIXED
#define I2S_MCLK   3



#define SDA_PIN   21
#define SCL_PIN   22



Audio audio;
WebServer server(80);
Preferences prefs;


struct Station {
  const char* name;
  const char* url;
};

Station stations[] = {
  {"BBC Radio 4", "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_fourlw"},
  {"BBC World",  "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service"},
  {"NPR",        "https://npr-ice.streamguys1.com/live.mp3"},
  {"Jazz24",     "https://live.wostreaming.net/direct/ppm-jazz24mp3-ibc1"}
};

const int stationCount = sizeof(stations) / sizeof(stations[0]);
int currentStation = 0;
int currentVolume  = 50;



void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 48000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 3072000   // 🔴 64 × 48kHz
  };

  i2s_pin_config_t pins = {
  .mck_io_num = I2S_MCLK,
  .bck_io_num = I2S_BCLK,
  .ws_io_num = I2S_LRCLK,
  .data_out_num = I2S_DATA,
  .data_in_num = I2S_PIN_NO_CHANGE,
  
};


  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);

  Serial.println("I2S started:");
  Serial.println("  BCLK = GPIO26");
  Serial.println("  LRCLK = GPIO25");
  Serial.println("  DATA = GPIO27");
  Serial.println("  MCLK = GPIO3");
  //Serial.println("  MCLK = GPIO3 (3.072 MHz)");

}




// ---- Audio debug callbacks ----

void audio_info(const char *info) {
  Serial.print("INFO: ");
  Serial.println(info);
}

void audio_showstation(const char *info) {
  Serial.print("STATION: ");
  Serial.println(info);
}

void audio_showstreamtitle(const char *info) {
  Serial.print("NOW PLAYING: ");
  Serial.println(info);
}

void audio_bitrate(const char *info) {
  Serial.print("BITRATE: ");
  Serial.println(info);
}

void audio_error(const char *info) {
  Serial.print("ERROR: ");
  Serial.println(info);
}



//////////////////////////////////////////////




void I2C_SCAN(){
byte error;
  int found = 0;

  Serial.println();
  Serial.println("Scanning...");

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      found++;
    } else if (error == 4) {
      Serial.print("Unknown error at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (found == 0) {
    Serial.println("No I2C devices found");
  } else {
    Serial.print("Done. Found ");
    Serial.print(found);
    Serial.println(" device(s).");
  }

  delay(3000);
}





void setup() {

  Serial.begin(115200);
  delay(1000);
  Serial.println("\nESP32 Internet Radio starting...");


    Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

 delay(20);


Wire.begin(SDA_PIN, SCL_PIN);
 
  delay(20);
  I2C_SCAN();
  delay(20);
TAS5749_I2C_INT();

 // Force I2S clocking
 
  i2s_set_clk(
    I2S_NUM_0,
    48000,                 // sample rate
    I2S_BITS_PER_SAMPLE_32BIT,
    //I2S_CHANNEL_STEREO
     I2S_CHANNEL_MONO 
  );
  
 // Audio library
 audio.setPinout(I2S_BCLK, I2S_LRCLK, I2S_DATA);
  //audio.i2s_mclk_pin_select(I2S_MCLK);
  audio.setVolume(currentVolume);
 audio.connecttohost("http://vis.media-ice.musicradio.com/CapitalMP3");//("http://vis.media-ice.musicradio.com/CapitalMP3");//("http://stream.live.vc.bbcmedia.co.uk/bbc_world_service");//

}



void loop() {
  audio.loop();

 
}
*/











//////////////////////////////////////////////
////////////////////////////////////////////////


#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "TAS5749M_I2C.h"

// ============================================================
// USER PINS
// ============================================================

// -------- Sony I2S input (ESP32 reads Sony) --------
#define SONY_BCK_IN    32
#define SONY_WS_IN     33
#define SONY_DATA_IN   34   // input-only pin is okay here

// -------- TAS I2S output (ESP32 drives Samsung amp) --------
#define TAS_BCK_OUT    26
#define TAS_WS_OUT     25
#define TAS_DATA_OUT   27

// -------- TAS I2C --------
#define SDA_PIN        21
#define SCL_PIN        22

// ============================================================
// I2S PORTS
// ============================================================

static const i2s_port_t I2S_SONY_RX = I2S_NUM_0;
static const i2s_port_t I2S_TAS_TX  = I2S_NUM_1;

// ============================================================
// AUDIO FORMAT
// ============================================================

// Sony side you observed at ~48 kHz and ~3.072 MHz BCK,
// which matches 32-bit stereo slots at 48 kHz.
static const uint32_t SONY_FS = 48000;

// TAS side already works for you only when forced to
// 48 kHz / 32-bit / MONO. 
static const uint32_t TAS_FS = 48000;

// ============================================================
// BUFFERING
// ============================================================

static const size_t RX_SAMPLES_STEREO = 256;  // number of int32 stereo words, not frames
static int32_t rxBuf[RX_SAMPLES_STEREO];      // interleaved L,R,L,R...
static int32_t txBuf[RX_SAMPLES_STEREO / 2];  // mono samples

// ============================================================
// OPTIONAL TUNING
// ============================================================

// If channels are swapped or polarity sounds weird, tweak these.
static bool useLeftOnly   = false;
static bool useRightOnly  = false;
static bool averageLR     = true;

// If audio sounds distorted or too loud, attenuate after mixing.
static int outputShift = 1;   // 1 = divide by 2, 2 = divide by 4, 0 = no shift

// ============================================================
// HELPERS
// ============================================================

static inline int32_t sat_add32(int32_t a, int32_t b) {
  int64_t s = (int64_t)a + (int64_t)b;
  if (s >  2147483647LL) return  2147483647;
  if (s < -2147483648LL) return -2147483648LL;
  return (int32_t)s;
}

static void print_i2s_rates() {
  Serial.println("Expected formats:");
  Serial.println("  Sony RX : 48k, 32-bit stereo, slave");
  Serial.println("  TAS  TX : 48k, 32-bit mono, master");
  Serial.println("Expected clocks:");
  Serial.println("  Sony BCK should be around 3.072 MHz");
  Serial.println("  Sony LRCK should be 48 kHz");
  Serial.println("  TAS  BCK should be around 1.536 MHz");
  Serial.println("  TAS  LRCK should be 48 kHz");
}

// ============================================================
// I2S INIT
// ============================================================

static void init_sony_rx() {
  // Sony provides clocks -> ESP32 is slave RX
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_RX);
  cfg.sample_rate = SONY_FS;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 128;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = SONY_BCK_IN;
  pins.ws_io_num    = SONY_WS_IN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = SONY_DATA_IN;
#if ESP_IDF_VERSION_MAJOR >= 4
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
#endif

  i2s_driver_install(I2S_SONY_RX, &cfg, 0, NULL);
  i2s_set_pin(I2S_SONY_RX, &pins);
  i2s_zero_dma_buffer(I2S_SONY_RX);

  Serial.println("Sony RX I2S ready");
}

static void init_tas_tx() {
  // ESP32 generates TAS clocks -> master TX
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = TAS_FS;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;

  // Start as stereo/right-left, then force MONO with i2s_set_clk
  // because that is the mode you found actually works on TAS5749M. 
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;//I2S_CHANNEL_FMT_ONLY_RIGHT;
  cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 128;
  cfg.use_apll = true;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = TAS_BCK_OUT;
  pins.ws_io_num    = TAS_WS_OUT;
  pins.data_out_num = TAS_DATA_OUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
#if ESP_IDF_VERSION_MAJOR >= 4
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
#endif

  i2s_driver_install(I2S_TAS_TX, &cfg, 0, NULL);
  i2s_set_pin(I2S_TAS_TX, &pins);
  i2s_zero_dma_buffer(I2S_TAS_TX);

  // This is the key: match the TAS mode you already proved works
  i2s_set_clk(
    I2S_TAS_TX,
    48000,
    I2S_BITS_PER_SAMPLE_32BIT,
    I2S_CHANNEL_MONO
  );

  Serial.println("TAS TX I2S ready");
}

// ============================================================
// CONVERSION
// ============================================================

// Sony RX buffer is interleaved stereo 32-bit:
//   rx[0]=L0, rx[1]=R0, rx[2]=L1, rx[3]=R1, ...
//
// TAS TX wants mono 32-bit stream.
// We can feed one mono sample at a time.
static size_t stereo32_to_mono32(const int32_t* in, size_t inWords, int32_t* out) {
  size_t frames = inWords / 2;

  for (size_t i = 0; i < frames; i++) {
    int32_t L = in[2 * i + 0];
    int32_t R = in[2 * i + 1];
    int32_t M = 0;

    if (useLeftOnly) {
      M = L;
    } else if (useRightOnly) {
      M = R;
    } else if (averageLR) {
      M = sat_add32(L >> 1, R >> 1);
    } else {
      M = L;
    }

    if (outputShift > 0) {
      M >>= outputShift;
    }

    out[i] = M;
  }

  return frames;
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=====================================");
  Serial.println("ESP32 Sony I2S -> TAS5749M converter");
  Serial.println("=====================================");

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(20);

  // TAS5749M init from your uploaded header
  TAS5749_I2C_INT();
  delay(50);

  init_sony_rx();
  init_tas_tx();

  print_i2s_rates();

  Serial.println("Converter started.");
  Serial.println("If silent, try:");
  Serial.println("  1) useLeftOnly=true");
  Serial.println("  2) useRightOnly=true");
  Serial.println("  3) averageLR=true");
  Serial.println("  4) change communication format to left-justified test");
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  size_t bytesRead = 0;
  size_t bytesWritten = 0;

  // Read stereo 32-bit data from Sony
  esp_err_t erx = i2s_read(
    I2S_SONY_RX,
    (void*)rxBuf,
    sizeof(rxBuf),
    &bytesRead,
    portMAX_DELAY
  );

  if (erx != ESP_OK || bytesRead == 0) {
    Serial.printf("RX fail erx=%d bytes=%u\n", (int)erx, (unsigned)bytesRead);
    delay(10);
    return;
  }

  size_t inWords = bytesRead / sizeof(int32_t);
  size_t monoSamples = stereo32_to_mono32(rxBuf, inWords, txBuf);

  // Write mono 32-bit data to TAS side
  esp_err_t etx = i2s_write(
    I2S_TAS_TX,
    (const void*)txBuf,
    monoSamples * sizeof(int32_t),
    &bytesWritten,
    portMAX_DELAY
  );

  if (etx != ESP_OK) {
    Serial.printf("TX fail etx=%d\n", (int)etx);
    delay(10);
    return;
  }
} 