#ifdef DEVICE_JC3248W535

#include "hal_jc3248w535.h"

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>   // pinned 1.6.0 (1.6.1 reported broken on this panel)
#include <driver/i2s.h>
#include <esp_sleep.h>

JC_M5 M5;

// ---- Panel (Arduino_GFX = init + full-frame pusher only) -----------------------

static Arduino_ESP32QSPI s_bus(JC_LCD_QSPI_CS, JC_LCD_QSPI_CLK,
                               JC_LCD_QSPI_D0, JC_LCD_QSPI_D1,
                               JC_LCD_QSPI_D2, JC_LCD_QSPI_D3);
static Arduino_AXS15231B s_panel(&s_bus, GFX_NOT_DEFINED /*no reset pin*/, 0 /*rotation*/,
                                 false /*ips*/, JC_PANEL_W, JC_PANEL_H);

// Landscape direction relative to the USB-C port: 1 or 3. If the picture comes up
// upside-down at bring-up, flip this - the touch mapping follows it automatically.
#define JC_COMP_ROT 3

// Some panel batches mount/configure the AXS15231B touch sensor 180 deg from the
// orientation the original port assumed (this unit does - calibrated from raw touch
// dumps). Set to 1 if touch lands point-mirrored relative to the display.
#define JC_TOUCH_FLIP180 1

// Full-frame composition buffer in panel-native portrait orientation. Its logical
// rotation presents it as 480x320 landscape, so the compose below is rotation-free.
static lgfx::LGFX_Sprite s_comp;
static SemaphoreHandle_t s_gfxMutex = nullptr;
static volatile bool s_displayReady = false;

static void composeAndPush() {
  // UI sprite center lands on the landscape center; 1.5x wide / 1.333x tall fills 480x320.
  M5.Lcd.pushRotateZoom(&s_comp, (JC_UI_W * JC_UI_ZOOM_X) / 2, (JC_UI_H * JC_UI_ZOOM_Y) / 2,
                        0.0f, JC_UI_ZOOM_X, JC_UI_ZOOM_Y);
  // LovyanGFX 16-bit sprite buffers hold panel-order (big-endian) rgb565, hence the
  // "Be" variant. If colors ever come out wrong, the fix is draw16bitRGBBitmap.
  s_panel.draw16bitBeRGBBitmap(0, 0, (uint16_t *)s_comp.getBuffer(), JC_PANEL_W, JC_PANEL_H);
}

static void compositorTask(void *) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    if (!s_displayReady) continue;
    xSemaphoreTake(s_gfxMutex, portMAX_DELAY);
    composeAndPush();
    xSemaphoreGive(s_gfxMutex);
  }
}

void JC_Lcd::invertDisplay(bool i) {
  if (s_gfxMutex) xSemaphoreTake(s_gfxMutex, portMAX_DELAY);
  s_panel.invertDisplay(i);
  if (s_gfxMutex) xSemaphoreGive(s_gfxMutex);
}

// ---- Touch (AXS15231B over I2C, portrait-native coordinates) -------------------

static uint8_t  s_touchCount = 0;
static int16_t  s_touchX = 0, s_touchY = 0;   // in UI (sketch) coordinates

static bool touchReadNative(uint16_t &nx, uint16_t &ny) {
  // Vendor read sequence (esp_lcd_touch_axs15231b): 11-byte command, 8-byte reply,
  // FT-style point record. Reply: [1]=points, [2..3]=x, [4..5]=y, event in [2]>>4.
  static const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08,
                                  0x00, 0x00, 0x00};
  Wire.beginTransmission(JC_TOUCH_ADDR);
  Wire.write(cmd, sizeof(cmd));
  if (Wire.endTransmission() != 0) return false;
  uint8_t buf[8];
  if (Wire.requestFrom((uint8_t)JC_TOUCH_ADDR, (uint8_t)sizeof(buf)) != sizeof(buf)) return false;
  for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = Wire.read();

  uint8_t points = buf[1];
  uint8_t event  = (buf[2] >> 4) & 0x0F;
  if (points == 0 || points > 2) return false;   // >2 = noise/garbage frame
  if (event == 1) return false;                  // lift-off
  nx = ((buf[2] & 0x0F) << 8) | buf[3];
  ny = ((buf[4] & 0x0F) << 8) | buf[5];
  return nx < JC_PANEL_W && ny < JC_PANEL_H;
}

// Native portrait -> current UI coordinates (inverse of the compose + UI rotation).
static void touchToUI(uint16_t nx, uint16_t ny, int16_t &ux, int16_t &uy) {
  int16_t lx, ly;   // landscape coords in the composed 480x320 frame
#if JC_COMP_ROT == 1
  lx = (JC_PANEL_H - 1) - ny;
  ly = nx;
#else
  lx = ny;
  ly = (JC_PANEL_W - 1) - nx;
#endif
#if JC_TOUCH_FLIP180
  lx = (JC_PANEL_H - 1) - lx;
  ly = (JC_PANEL_W - 1) - ly;
#endif
  ux = (int16_t)(lx / JC_UI_ZOOM_X);
  uy = (int16_t)(ly / JC_UI_ZOOM_Y);
  uint_fast8_t r = M5.Lcd.getRotation();
  if (r & 2) { ux = (JC_UI_W - 1) - ux; uy = (JC_UI_H - 1) - uy; }   // 180
  if (r & 4) { ux = (JC_UI_W - 1) - ux; }                             // mirrored
}

uint8_t JC_Touch::getCount() { return s_touchCount; }

void JC_Button::setRawState(bool pressed, uint32_t ms) {
  if (pressed != _raw) { _raw = pressed; _lastChange = ms; }
  if (ms - _lastChange >= DEBOUNCE_MS && _raw != _stable) {
    _stable = _raw;
    if (_stable) _edge = true;
  }
}

// ---- Speaker (NS4168 I2S mono, tone synth in a background task) ----------------

#define JC_I2S_PORT       I2S_NUM_1
#define JC_I2S_RATE       16000
#define JC_TONE_CHUNK     160     // 10 ms of samples

static volatile bool     s_spkEnabled = false;
static volatile bool     s_tonePlaying = false;
static volatile uint32_t s_toneFreq = 440;
static volatile uint32_t s_toneEnd = 0;
static volatile uint8_t  s_volume = 128;

static void speakerTask(void *) {
  float phase = 0.0f;
  int16_t buf[JC_TONE_CHUNK];
  for (;;) {
    if (s_tonePlaying) {
      if ((int32_t)(millis() - s_toneEnd) >= 0) {
        s_tonePlaying = false;
        i2s_zero_dma_buffer(JC_I2S_PORT);
        continue;
      }
      float step = 2.0f * PI * (float)s_toneFreq / (float)JC_I2S_RATE;
      // NS4168 is known to run quiet on this board (missing pull-up on U4) - use the
      // full int16 range at max volume rather than headroom. Square wave to match
      // M5Unified's Speaker.tone() timbre (a sine reads much softer at equal volume).
      int32_t amp = (int32_t)s_volume * 128;
      for (int i = 0; i < JC_TONE_CHUNK; ++i) {
        buf[i] = (phase < PI) ? (int16_t)amp : (int16_t)(-amp);
        phase += step;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      size_t written = 0;
      i2s_write(JC_I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
    } else {
      phase = 0.0f;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

static void speakerInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = JC_I2S_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(JC_I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("JC3248W535: I2S install failed, speaker disabled");
    return;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = JC_I2S_BCLK;
  pins.ws_io_num = JC_I2S_LRCK;
  pins.data_out_num = JC_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(JC_I2S_PORT, &pins);
  i2s_zero_dma_buffer(JC_I2S_PORT);
  s_spkEnabled = true;
  xTaskCreatePinnedToCore(speakerTask, "jc_spk", 3072, nullptr, 2, nullptr, 1);
}

void JC_Speaker::tone(uint16_t freq, uint32_t duration_ms) {
  if (!s_spkEnabled || freq == 0 || duration_ms == 0) return;
  s_volume = _volume;
  s_toneFreq = freq;
  s_toneEnd = millis() + duration_ms;
  s_tonePlaying = true;
}

bool JC_Speaker::isPlaying() { return s_tonePlaying; }

void JC_Speaker::mute() {
  s_tonePlaying = false;
  if (s_spkEnabled) i2s_zero_dma_buffer(JC_I2S_PORT);
}

// ---- Power ---------------------------------------------------------------------

void JC_Power::powerOff() {
  // No PMIC: "off" = backlight + panel off, then deep sleep. Reset button restarts.
  M5.Speaker.mute();
  ledcWrite(JC_BL_LEDC_CHANNEL, 0);
  if (s_gfxMutex) xSemaphoreTake(s_gfxMutex, portMAX_DELAY);
  s_panel.displayOff();
  esp_deep_sleep_start();
}

// ---- SD (own SPI bus - the display owns SPI2, so the card gets SPI3/HSPI) ------

bool halSDBegin() {
  static SPIClass s_sdSpi(HSPI);
  s_sdSpi.begin(JC_SD_SCK, JC_SD_MISO, JC_SD_MOSI, JC_SD_CS);
  return SD.begin(JC_SD_CS, s_sdSpi);
}

// ---- M5 facade -----------------------------------------------------------------

void JC_M5::begin(const JC_Config &cfg) {
  Serial.begin(115200);       // M5Unified does this inside M5.begin(); the sketch relies on it

  // Touch I2C first: the sketch later calls Wire.begin() with no pins, which on
  // ESP32-S3 would default to SDA=8/SCL=9 and clobber the touch bus. A parameterless
  // begin() after this one is a no-op on core 2.x, so claiming the pins here wins.
  // 100 kHz: this batch's AXS15231B touch NAKs at 400 kHz (confirmed by scan - it
  // only ACKs at 100 kHz). A poll is ~19 bytes, so 100 kHz still costs only ~2 ms.
  Wire.begin(JC_TOUCH_SDA, JC_TOUCH_SCL, 100000);

  if (!s_panel.begin(40000000L)) {
    Serial.println("JC3248W535: panel init FAILED");
  }
  s_panel.fillScreen(BLACK);

  // Bring up the AXS15231B touch controller: on this batch it needs all of this,
  // reproduced from the (accidentally working) debug bus-scan sequence -
  //  1. the panel must be initialized first (its I2C engine is down before that),
  //  2. the I2C bus must be re-initialized after panel init,
  //  3. it only starts answering after it has ACKed a bare address probe.
  {
    delay(200);
    Wire.end();
    Wire.begin(JC_TOUCH_SDA, JC_TOUCH_SCL, 100000);
    delay(50);
    bool touchUp = false;
    for (int i = 0; i < 10 && !touchUp; ++i) {
      Wire.beginTransmission(JC_TOUCH_ADDR);
      touchUp = (Wire.endTransmission() == 0);
      if (!touchUp) delay(50);
    }
    if (!touchUp) Serial.println("JC3248W535: touch controller not responding");
  }

  ledcSetup(JC_BL_LEDC_CHANNEL, 5000, 8);
  ledcAttachPin(JC_LCD_BL, JC_BL_LEDC_CHANNEL);
  ledcWrite(JC_BL_LEDC_CHANNEL, 255);

  Lcd.setPsram(true);
  Lcd.setColorDepth(16);
  if (!Lcd.createSprite(JC_UI_W, JC_UI_H)) Serial.println("JC3248W535: UI sprite alloc FAILED");
  Lcd.setPivot(JC_UI_W / 2.0f - 0.5f, JC_UI_H / 2.0f - 0.5f);
  if (cfg.clear_display) Lcd.fillScreen(TFT_BLACK);

  s_comp.setPsram(true);
  s_comp.setColorDepth(16);
  if (!s_comp.createSprite(JC_PANEL_W, JC_PANEL_H)) Serial.println("JC3248W535: frame sprite alloc FAILED");
  s_comp.setRotation(JC_COMP_ROT);
  s_comp.fillScreen(TFT_BLACK);

  s_gfxMutex = xSemaphoreCreateMutex();
  s_displayReady = true;
  xTaskCreatePinnedToCore(compositorTask, "jc_disp", 4096, nullptr, 1, nullptr, 1);

  if (cfg.internal_spk) speakerInit();
}

void JC_M5::update() {
  // Called from tight loops (web/OTA waits) - keep the I2C traffic to ~100 Hz.
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll >= 10) {
    lastPoll = now;
    uint16_t nx, ny;
    if (touchReadNative(nx, ny)) {
      touchToUI(nx, ny, s_touchX, s_touchY);
      s_touchCount = 1;
    } else {
      s_touchCount = 0;
    }
  }

  // Bottom-of-screen thirds -> A/B/C, matching the sketch's on-screen affordances
  // (CONFIG button 0-110, snooze label center, OFF right) and M5Unified's touch zones.
  bool touching = s_touchCount > 0;
  bool inBar = touching && s_touchY >= 200;
  M5.BtnA.setRawState(inBar && s_touchX < 110, now);
  M5.BtnB.setRawState(inBar && s_touchX >= 110 && s_touchX <= 210, now);
  M5.BtnC.setRawState(inBar && s_touchX > 210, now);
}

#endif // DEVICE_JC3248W535
