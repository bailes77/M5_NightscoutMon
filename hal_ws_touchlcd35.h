#ifndef _HAL_WS_TOUCHLCD35_H
#define _HAL_WS_TOUCHLCD35_H
#ifdef DEVICE_WS_TOUCH_LCD_35

// Waveshare ESP32-Touch-LCD-3.5 (classic ESP32-D0WDR2, 16MB flash, 2MB in-package PSRAM,
// 3.5" 320x480 ST7796S IPS on 4-wire SPI, FT6336 capacitive touch, TCA9554 IO expander,
// AXP2101 PMIC, PCF85063 RTC, ES8311 I2S codec + speaker header, microSD, BOOT/PWR buttons).
// https://docs.waveshare.com/ESP32-Touch-LCD-3.5
//
// Same porting strategy as the JC3248W535 shim (hal_jc3248w535.h): the sketch keeps
// drawing its unchanged 320x240 landscape UI into an offscreen LGFX_Sprite and a
// background task composes it zoomed exactly 1.5x into a 480x320 frame that is pushed
// whole to the panel at 10 Hz. Unlike the JC, every chip here is one M5GFX/LovyanGFX
// already drives natively (Bus_SPI + Panel_LCD/ST7796 + Light_PWM + Touch_FT5x06), so no
// extra display library is needed and the panel does rotation in hardware.
//
// The AXP2101 gives a real battery level and a real power-off (PWR button turns the
// board back on); the PCF85063 RTC is not used (the sketch never touches M5.Rtc).

#include <Arduino.h>
#include <M5GFX.h>                        // lgfx::LGFX_Sprite/LGFX_Device + fonts + Bus_SPI/Light_PWM/Touch_FT5x06
#include <lgfx/v1/panel/Panel_LCD.hpp>    // base for the ST7796 panel class below
#include <lgfx/v1/touch/Touch_FT5x06.hpp>  // FT6336 is FT5x06-register compatible (as on Core2)

// ---- Pin map (Waveshare Arduino examples, github.com/waveshareteam/ESP32-Touch-LCD-3.5) ----
#define WS_I2C_SDA        21   // shared: FT6336 touch, TCA9554, AXP2101, ES8311, PCF85063
#define WS_I2C_SCL        22
#define WS_LCD_SCK        18   // VSPI, shared with the microSD
#define WS_LCD_MOSI       23
#define WS_LCD_MISO       19
#define WS_LCD_CS          5
#define WS_LCD_DC         27
#define WS_LCD_BL         25   // backlight PWM, active high
#define WS_SD_CS          15
#define WS_I2S_BCLK        2
#define WS_I2S_LRCK        4
#define WS_I2S_DOUT       12
#define WS_TOUCH_ADDR   0x38   // FT6336
#define WS_TCA_ADDR     0x20   // TCA9554: EXIO0 = LCD reset, EXIO1 = touch reset, EXIO2 = codec/PA enable
#define WS_AXP_ADDR     0x34   // AXP2101
#define WS_ES8311_ADDR  0x18   // ES8311

// Panel native size (portrait) and the sketch's UI size. 480x320 = exactly 1.5 * 320x240.
#define WS_PANEL_W       320
#define WS_PANEL_H       480
#define WS_UI_W          320
#define WS_UI_H          240
#define WS_UI_ZOOM      1.5f

#define WS_BL_LEDC_CHANNEL 7   // sketch's vibration uses LEDC channel 14 - no clash

// ST7796 panel: M5GFX 0.2.x ships Panel_LCD/ST7789 but not ST7796, so this carries the
// ST7796 init list from upstream LovyanGFX (lovyan03, FreeBSD licence - same as M5GFX).
struct Panel_ST7796_WS : public lgfx::Panel_LCD {
  Panel_ST7796_WS(void) {
    _cfg.panel_width  = _cfg.memory_width  = WS_PANEL_W;
    _cfg.panel_height = _cfg.memory_height = WS_PANEL_H;
    _cfg.dummy_read_pixel = 8;
  }
protected:
  const uint8_t* getInitCommands(uint8_t listno) const override;
  void setColorDepth_impl(lgfx::color_depth_t depth) override {
    _write_depth = ((int)depth & lgfx::color_depth_t::bit_mask) > 16 ? lgfx::rgb888_3Byte : lgfx::rgb565_2Byte;
    _read_depth = _write_depth;
  }
};

// The sketch's UI: any drawing API it uses comes straight from LGFX_Sprite.
// Only rotation, brightness and invert need device-specific handling.
class WS_Lcd : public lgfx::LGFX_Sprite {
public:
  // cfg.display_rotation carries M5-panel values 1/3/5/7 ("buttons down/up" x mirror).
  // The UI sprite is landscape-native, so those map to sprite rotations 0/2/4/6.
  void setRotation(uint_fast8_t r) { lgfx::LGFX_Sprite::setRotation(r & ~1u); }
  void setBrightness(uint8_t b);      // backlight PWM via the LGFX device
  void invertDisplay(bool i);         // panel command, forwarded to the device under the gfx mutex
};

class WS_Button {
public:
  void setRawState(bool pressed, uint32_t ms);
  bool wasPressed()  { bool e = _edge; _edge = false; return e; }
  bool isPressed()   { return _stable; }
private:
  bool _raw = false, _stable = false, _edge = false;
  uint32_t _lastChange = 0;
  static const uint32_t DEBOUNCE_MS = 20;
};

class WS_Touch {
public:
  bool isEnabled() { return true; }
  uint8_t getCount();
};

class WS_Speaker {
public:
  void setVolume(uint8_t v) { _volume = v; }
  void tone(uint16_t freq, uint32_t duration_ms);
  bool isPlaying();
  void mute();
private:
  uint8_t _volume = 128;
  friend class WS_M5;
};

class WS_Power {
public:
  int  getBatteryLevel();      // AXP2101 fuel gauge %, -1 if no PMIC/battery (sketch hides the icon)
  int  getBatteryVoltage();    // mV
  void powerOff();             // AXP2101 soft power-off; PWR button turns the board back on
};

struct WS_Config {
  bool clear_display = true;
  bool internal_spk  = true;
};

class WS_M5 {
public:
  WS_Config config() { return WS_Config(); }
  void begin(const WS_Config &cfg);
  void update();                            // touch -> synthetic buttons
  int  getBoard() { return 998; }           // sketch only serial-prints this

  WS_Lcd  Lcd;
  WS_Lcd  &Display = Lcd;
  WS_Button BtnA, BtnB, BtnC;
  WS_Touch  Touch;
  WS_Speaker Speaker;
  WS_Power   Power;
};

extern WS_M5 M5;

bool halSDBegin();

#endif // DEVICE_WS_TOUCH_LCD_35
#endif // _HAL_WS_TOUCHLCD35_H
