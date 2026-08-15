#ifdef DEVICE_WS_TOUCH_LCD_35

#include "hal_ws_touchlcd35.h"

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <driver/i2s.h>
#include <esp_sleep.h>

WS_M5 M5;

// ---- ST7796 init list (upstream LovyanGFX Panel_ST7796, FreeBSD licence) ------------

const uint8_t* Panel_ST7796_WS::getInitCommands(uint8_t listno) const {
  static constexpr uint8_t CMD_INVCTR  = 0xB4;
  static constexpr uint8_t CMD_DFUNCTR = 0xB6;
  static constexpr uint8_t CMD_PWCTR2  = 0xC1;
  static constexpr uint8_t CMD_PWCTR3  = 0xC2;
  static constexpr uint8_t CMD_VMCTR   = 0xC5;
  static constexpr uint8_t CMD_GMCTRP1 = 0xE0;
  static constexpr uint8_t CMD_GMCTRN1 = 0xE1;
  static constexpr uint8_t CMD_DOCA    = 0xE8;
  static constexpr uint8_t CMD_CSCON   = 0xF0;
  static constexpr uint8_t list0[] = {
      CMD_CSCON,   1, 0xC3,  // Enable extension command 2 partI
      CMD_CSCON,   1, 0x96,  // Enable extension command 2 partII
      CMD_INVCTR,  1, 0x01,  // 1-dot inversion
      CMD_DFUNCTR, 3, 0x80, 0x22, 0x3B,
      CMD_DOCA,    8, 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33,
      CMD_PWCTR2,  1, 0x06,
      CMD_PWCTR3,  1, 0xA7,
      CMD_VMCTR,   1 + CMD_INIT_DELAY, 0x18, 120,
      CMD_GMCTRP1, 14, 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F,
                       0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B,
      CMD_GMCTRN1, 14 + CMD_INIT_DELAY,
                       0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B,
                       0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B, 120,
      CMD_CSCON,   1, 0x3C,  // Disable extension command 2 partI
      CMD_CSCON,   1, 0x69,  // Disable extension command 2 partII
      CMD_SLPOUT,  0 + CMD_INIT_DELAY, 130,
      CMD_IDMOFF,  0,
      CMD_DISPON,  0,
      0xFF, 0xFF,  // end
  };
  return listno == 0 ? list0 : nullptr;
}

// ---- Device (LovyanGFX: SPI bus + ST7796 + PWM backlight + FT6336 touch) -----------

class LGFX_WS : public lgfx::LGFX_Device {
  Panel_ST7796_WS    _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
  lgfx::Touch_FT5x06 _touch;
public:
  LGFX_WS(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_host    = VSPI_HOST;     // shared with the microSD (CS 15), like M5Stack Basic
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;      // ST7796 tolerates 80 MHz - raise once validated
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = WS_LCD_SCK;
      cfg.pin_mosi    = WS_LCD_MOSI;
      cfg.pin_miso    = WS_LCD_MISO;
      cfg.pin_dc      = WS_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs           = WS_LCD_CS;
      cfg.pin_rst          = -1;       // reset is on the TCA9554 (EXIO0), pulsed before init()
      cfg.pin_busy         = -1;
      cfg.memory_width     = WS_PANEL_W;
      cfg.memory_height    = WS_PANEL_H;
      cfg.panel_width      = WS_PANEL_W;
      cfg.panel_height     = WS_PANEL_H;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;
      cfg.invert           = true;     // IPS panel (vendor inits Arduino_ST7796 with ips=true); flip if colours come out negative
      cfg.rgb_order        = false;    // flip if red/blue are swapped
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;     // SD on the same SPI bus
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl      = WS_LCD_BL;
      cfg.invert      = false;
      cfg.freq        = 5000;
      cfg.pwm_channel = WS_BL_LEDC_CHANNEL;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;  cfg.x_max = WS_PANEL_W - 1;
      cfg.y_min = 0;  cfg.y_max = WS_PANEL_H - 1;
      cfg.pin_int         = -1;        // polled
      cfg.bus_shared      = true;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;                // LovyanGFX rides on Wire for I2C on Arduino-ESP32
      cfg.i2c_addr = WS_TOUCH_ADDR;
      cfg.pin_sda  = WS_I2C_SDA;
      cfg.pin_scl  = WS_I2C_SCL;
      cfg.freq     = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX_WS s_dev;

// Landscape direction relative to the USB-C port: 1 or 3. If the picture comes up
// upside-down at bring-up, flip this - touch follows the panel rotation automatically.
#define WS_LAND_ROT 1

// ---- Small I2C helpers (TCA9554 / AXP2101 / ES8311 registers over Wire) ------------

static bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cRead8(uint8_t addr, uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
  val = Wire.read();
  return true;
}

// TCA9554: reg 0x01 = output port, 0x03 = configuration (0 = output).
// EXIO0 = LCD reset, EXIO1 = touch reset, EXIO2 = codec/PA enable (vendor examples).
static void tcaResetPeripherals() {
  if (!i2cWrite8(WS_TCA_ADDR, 0x03, 0xF8)) {         // EXIO0..2 outputs
    Serial.println("WS-LCD35: TCA9554 not found");
    return;
  }
  i2cWrite8(WS_TCA_ADDR, 0x01, 0x07);  delay(10);   // all high
  i2cWrite8(WS_TCA_ADDR, 0x01, 0x04);  delay(10);   // LCD + touch reset low, codec enable stays high
  i2cWrite8(WS_TCA_ADDR, 0x01, 0x07);  delay(200);  // release
}

// ---- Compose: UI sprite (320x240) -> landscape frame (480x320) -> panel -------------

static lgfx::LGFX_Sprite s_comp;
static SemaphoreHandle_t s_gfxMutex = nullptr;
static volatile bool s_displayReady = false;

static void composeAndPush() {
  // UI sprite center lands on the landscape center; exact 1.5x maps 320x240 -> 480x320.
  M5.Lcd.pushRotateZoom(&s_comp, (WS_UI_W * WS_UI_ZOOM) / 2, (WS_UI_H * WS_UI_ZOOM) / 2,
                        0.0f, WS_UI_ZOOM, WS_UI_ZOOM);
  // The panel shares VSPI with the SD card. The Arduino SD driver serialises its own
  // accesses on the SPIClass transaction mutex, so taking that same mutex around the
  // frame push keeps the core-1 compositor and main-thread SD reads off the bus at once.
  SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  s_comp.pushSprite(&s_dev, 0, 0);
  SPI.endTransaction();
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

void WS_Lcd::setBrightness(uint8_t b) { s_dev.setBrightness(b); }

void WS_Lcd::invertDisplay(bool i) {
  if (s_gfxMutex) xSemaphoreTake(s_gfxMutex, portMAX_DELAY);
  s_dev.invertDisplay(i);
  if (s_gfxMutex) xSemaphoreGive(s_gfxMutex);
}

// ---- Touch (FT6336 via LovyanGFX, already in landscape panel coordinates) ------------

static uint8_t  s_touchCount = 0;
static int16_t  s_touchX = 0, s_touchY = 0;   // in UI (sketch) coordinates

// Landscape panel coords -> current UI coordinates (inverse of the zoom + UI rotation).
static void touchToUI(int32_t lx, int32_t ly, int16_t &ux, int16_t &uy) {
  ux = (int16_t)(lx / WS_UI_ZOOM);
  uy = (int16_t)(ly / WS_UI_ZOOM);
  uint_fast8_t r = M5.Lcd.getRotation();
  if (r & 2) { ux = (WS_UI_W - 1) - ux; uy = (WS_UI_H - 1) - uy; }   // 180
  if (r & 4) { ux = (WS_UI_W - 1) - ux; }                             // mirrored
}

uint8_t WS_Touch::getCount() { return s_touchCount; }

void WS_Button::setRawState(bool pressed, uint32_t ms) {
  if (pressed != _raw) { _raw = pressed; _lastChange = ms; }
  if (ms - _lastChange >= DEBOUNCE_MS && _raw != _stable) {
    _stable = _raw;
    if (_stable) _edge = true;
  }
}

// ---- Speaker (ES8311 codec on I2S, tone synth in a background task) -----------------
//
// Codec setup is a condensed, fixed-configuration version of Espressif's es8311 driver
// (esp-bsp / Waveshare example, Apache-2.0): slave mode, I2S format, 16 kHz, 32-bit
// slots, MCLK derived from BCLK (the board's MCLK pin is GPIO3 = UART0 RX, and the
// vendor examples do not drive it). With MCLK = BCLK = 16000 * 32 * 2 = 1.024 MHz the
// driver's coefficient table entry {1024000, 16000} gives the divider values below.

#define WS_I2S_PORT       I2S_NUM_0
#define WS_I2S_RATE       16000
#define WS_TONE_CHUNK     160     // 10 ms of frames

static volatile bool     s_spkEnabled = false;
static volatile bool     s_tonePlaying = false;
static volatile uint32_t s_toneFreq = 440;
static volatile uint32_t s_toneEnd = 0;
static volatile uint8_t  s_volume = 128;

static bool es8311Init() {
  uint8_t v;
  if (!i2cRead8(WS_ES8311_ADDR, 0xFD, v)) return false;   // chip ID1 (0x83) - presence check
  // Reset + power-on
  i2cWrite8(WS_ES8311_ADDR, 0x00, 0x1F); delay(20);
  i2cWrite8(WS_ES8311_ADDR, 0x00, 0x00);
  i2cWrite8(WS_ES8311_ADDR, 0x00, 0x80);
  // Clock manager: all clocks on, MCLK taken from the BCLK pin (bit7)
  i2cWrite8(WS_ES8311_ADDR, 0x01, 0xBF);
  // Dividers for {mclk 1024000, rate 16000}: pre_div 1, pre_multi 4x, adc/dac_div 1,
  // single speed, lrck 0x00FF, bclk_div 4, adc/dac osr 0x10
  if (i2cRead8(WS_ES8311_ADDR, 0x02, v)) i2cWrite8(WS_ES8311_ADDR, 0x02, (v & 0x07) | (0 << 5) | (2 << 3));
  i2cWrite8(WS_ES8311_ADDR, 0x03, 0x10);
  i2cWrite8(WS_ES8311_ADDR, 0x04, 0x10);
  i2cWrite8(WS_ES8311_ADDR, 0x05, 0x00);
  if (i2cRead8(WS_ES8311_ADDR, 0x06, v)) i2cWrite8(WS_ES8311_ADDR, 0x06, (v & 0xC0) | (4 - 1));  // SCLK not inverted, bclk_div 4
  if (i2cRead8(WS_ES8311_ADDR, 0x07, v)) i2cWrite8(WS_ES8311_ADDR, 0x07, (v & 0xC0) | 0x00);
  i2cWrite8(WS_ES8311_ADDR, 0x08, 0xFF);
  // Format: slave serial port, I2S, 32-bit in/out
  if (i2cRead8(WS_ES8311_ADDR, 0x00, v)) i2cWrite8(WS_ES8311_ADDR, 0x00, v & 0xBF);
  i2cWrite8(WS_ES8311_ADDR, 0x09, 4 << 2);
  i2cWrite8(WS_ES8311_ADDR, 0x0A, 4 << 2);
  // Analog power-up, DAC on, HP drive on, EQ bypass
  i2cWrite8(WS_ES8311_ADDR, 0x0D, 0x01);
  i2cWrite8(WS_ES8311_ADDR, 0x0E, 0x02);
  i2cWrite8(WS_ES8311_ADDR, 0x12, 0x00);
  i2cWrite8(WS_ES8311_ADDR, 0x13, 0x10);
  i2cWrite8(WS_ES8311_ADDR, 0x1C, 0x6A);
  i2cWrite8(WS_ES8311_ADDR, 0x37, 0x08);
  // DAC volume ~80 % (sketch volume scales the samples); unmute
  i2cWrite8(WS_ES8311_ADDR, 0x32, 0xCC);
  if (i2cRead8(WS_ES8311_ADDR, 0x31, v)) i2cWrite8(WS_ES8311_ADDR, 0x31, v & ~0x60);
  return true;
}

static void speakerTask(void *) {
  float phase = 0.0f;
  int32_t buf[WS_TONE_CHUNK * 2];   // stereo, 32-bit slots
  for (;;) {
    if (s_tonePlaying) {
      if ((int32_t)(millis() - s_toneEnd) >= 0) {
        s_tonePlaying = false;
        i2s_zero_dma_buffer(WS_I2S_PORT);
        continue;
      }
      float step = 2.0f * PI * (float)s_toneFreq / (float)WS_I2S_RATE;
      int32_t amp = (int32_t)s_volume * 128;   // 0..32640 in the int16 domain
      for (int i = 0; i < WS_TONE_CHUNK; ++i) {
        int32_t s = (int32_t)(sinf(phase) * amp) << 16;
        buf[2 * i] = s;
        buf[2 * i + 1] = s;
        phase += step;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      size_t written = 0;
      i2s_write(WS_I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
    } else {
      phase = 0.0f;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

static void speakerInit() {
  if (!es8311Init()) {
    Serial.println("WS-LCD35: ES8311 codec not found, speaker disabled");
    return;
  }
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = WS_I2S_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.tx_desc_auto_clear = true;
  cfg.use_apll = false;
  if (i2s_driver_install(WS_I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("WS-LCD35: I2S install failed, speaker disabled");
    return;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = WS_I2S_BCLK;
  pins.ws_io_num = WS_I2S_LRCK;
  pins.data_out_num = WS_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(WS_I2S_PORT, &pins);
  i2s_zero_dma_buffer(WS_I2S_PORT);
  s_spkEnabled = true;
  xTaskCreatePinnedToCore(speakerTask, "ws_spk", 3072, nullptr, 2, nullptr, 1);
}

void WS_Speaker::tone(uint16_t freq, uint32_t duration_ms) {
  if (!s_spkEnabled || freq == 0 || duration_ms == 0) return;
  s_volume = _volume;
  s_toneFreq = freq;
  s_toneEnd = millis() + duration_ms;
  s_tonePlaying = true;
}

bool WS_Speaker::isPlaying() { return s_tonePlaying; }

void WS_Speaker::mute() {
  s_tonePlaying = false;
  if (s_spkEnabled) i2s_zero_dma_buffer(WS_I2S_PORT);
}

// ---- Power (AXP2101, direct register access) ---------------------------------------

static bool s_axpPresent = false;

static void axpInit() {
  uint8_t id;
  s_axpPresent = i2cRead8(WS_AXP_ADDR, 0x03, id) && ((id & 0xCF) == 0x4A);   // AXP2101 chip ID
  if (!s_axpPresent) { Serial.println("WS-LCD35: AXP2101 not found"); return; }
  uint8_t v;
  if (i2cRead8(WS_AXP_ADDR, 0x30, v)) i2cWrite8(WS_AXP_ADDR, 0x30, v | 0x01);   // VBAT ADC on
  if (i2cRead8(WS_AXP_ADDR, 0x68, v)) i2cWrite8(WS_AXP_ADDR, 0x68, v | 0x01);   // battery detection on
}

int WS_Power::getBatteryLevel() {
  if (!s_axpPresent) return -1;
  uint8_t st, pct;
  if (!i2cRead8(WS_AXP_ADDR, 0x00, st) || !(st & 0x08)) return -1;   // no battery connected
  if (!i2cRead8(WS_AXP_ADDR, 0xA4, pct)) return -1;
  return pct > 100 ? 100 : pct;
}

int WS_Power::getBatteryVoltage() {
  if (!s_axpPresent) return 0;
  uint8_t h, l;
  if (!i2cRead8(WS_AXP_ADDR, 0x34, h) || !i2cRead8(WS_AXP_ADDR, 0x35, l)) return 0;
  return ((h & 0x3F) << 8) | l;   // mV
}

void WS_Power::powerOff() {
  M5.Speaker.mute();
  s_dev.setBrightness(0);
  if (s_gfxMutex) xSemaphoreTake(s_gfxMutex, portMAX_DELAY);
  s_dev.sleep();
  if (s_axpPresent) {
    uint8_t v;
    if (i2cRead8(WS_AXP_ADDR, 0x10, v)) i2cWrite8(WS_AXP_ADDR, 0x10, v | 0x01);   // soft power-off
    delay(500);
  }
  esp_deep_sleep_start();   // fallback if the PMIC did not cut power
}

// ---- SD (same VSPI bus as the panel, own CS) ---------------------------------------

bool halSDBegin() {
  SPI.begin(WS_LCD_SCK, WS_LCD_MISO, WS_LCD_MOSI, -1);
  return SD.begin(WS_SD_CS, SPI);
}

// ---- M5 facade ------------------------------------------------------------------------

void WS_M5::begin(const WS_Config &cfg) {
  // Everything I2C hangs off one bus; claim it first (the sketch's later bare
  // Wire.begin() is a no-op on core 2.x once the bus is up).
  Wire.begin(WS_I2C_SDA, WS_I2C_SCL, 400000);

  axpInit();
  tcaResetPeripherals();      // LCD + touch reset, codec enable - before the panel init

  if (!s_dev.init()) {
    Serial.println("WS-LCD35: panel init FAILED");
  }
  s_dev.setRotation(WS_LAND_ROT);
  s_dev.fillScreen(TFT_BLACK);
  s_dev.setBrightness(255);

  Lcd.setPsram(true);
  Lcd.setColorDepth(16);
  if (!Lcd.createSprite(WS_UI_W, WS_UI_H)) Serial.println("WS-LCD35: UI sprite alloc FAILED");
  Lcd.setPivot(WS_UI_W / 2.0f - 0.5f, WS_UI_H / 2.0f - 0.5f);
  if (cfg.clear_display) Lcd.fillScreen(TFT_BLACK);

  s_comp.setPsram(true);
  s_comp.setColorDepth(16);
  if (!s_comp.createSprite(WS_PANEL_H, WS_PANEL_W)) Serial.println("WS-LCD35: frame sprite alloc FAILED");
  s_comp.fillScreen(TFT_BLACK);

  s_gfxMutex = xSemaphoreCreateMutex();
  s_displayReady = true;
  xTaskCreatePinnedToCore(compositorTask, "ws_disp", 4096, nullptr, 1, nullptr, 1);

  if (cfg.internal_spk) speakerInit();
}

void WS_M5::update() {
  // Called from tight loops (web/OTA waits) - keep the I2C traffic to ~100 Hz.
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll >= 10) {
    lastPoll = now;
    int32_t lx, ly;
    if (s_dev.getTouch(&lx, &ly)) {
      touchToUI(lx, ly, s_touchX, s_touchY);
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

#endif // DEVICE_WS_TOUCH_LCD_35
