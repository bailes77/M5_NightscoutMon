#ifndef _HAL_JC3248W535_H
#define _HAL_JC3248W535_H
#ifdef DEVICE_JC3248W535

// Guition JC3248W535 (ESP32-S3, 3.5" 320x480 AXS15231B on QSPI, capacitive touch,
// microSD, NS4168 I2S speaker, no physical user buttons).
//
// The panel has no hardware rotation and some silicon batches ignore window-address
// commands, so partial updates are unreliable: the only safe mode is pushing full
// frames. That constraint is turned into the whole porting strategy: the sketch keeps
// drawing its unchanged 320x240 landscape UI into an offscreen LGFX_Sprite (M5GFX is
// the drawing library, exactly as on M5 hardware), and a background task composes it
// rotated + zoomed exactly 1.5x into a 320x480 native frame and pushes it whole.
// Arduino_GFX (moononournation, pinned 1.6.0) is used only as panel init + full-frame
// pusher underneath; every drawing feature the sketch uses (free fonts, textdatum,
// qrcode, drawJpgFile) lives in the sprite.
//
// Touch is read from the same AXS15231B over I2C and mapped back into the sketch's
// 320x240 UI space; the bottom-of-screen thirds become synthetic BtnA/B/C, mirroring
// what M5Unified does on Core2/CoreS3.

#include <Arduino.h>
#include <M5GFX.h>   // for lgfx::LGFX_Sprite + fonts; the M5GFX device class itself is not used

// ---- Pin map (vendor BSP + community-verified; SD and I2S still need on-board confirmation) ----
#define JC_LCD_QSPI_CS    45
#define JC_LCD_QSPI_CLK   47
#define JC_LCD_QSPI_D0    21
#define JC_LCD_QSPI_D1    48
#define JC_LCD_QSPI_D2    40
#define JC_LCD_QSPI_D3    39
#define JC_LCD_BL          1   // backlight PWM, active high
#define JC_TOUCH_SDA       4
#define JC_TOUCH_SCL       8   // beware: ESP32-S3 default Wire SDA is also 8 - Wire must be begun with explicit pins
#define JC_TOUCH_ADDR   0x3B
#define JC_SD_CS          10
#define JC_SD_MOSI        11
#define JC_SD_SCK         12
#define JC_SD_MISO        13
#define JC_I2S_LRCK        2
#define JC_I2S_BCLK       42
#define JC_I2S_DOUT       41

// Panel native size (portrait) and the sketch's UI size. 480x320 = exactly 1.5 * 320x240.
#define JC_PANEL_W       320
#define JC_PANEL_H       480
#define JC_UI_W          320
#define JC_UI_H          240
#define JC_UI_ZOOM      1.5f

#define JC_BL_LEDC_CHANNEL 7   // S3 has LEDC channels 0-7; sketch's vibration uses 14 (no-op on S3)

// The sketch's UI: any drawing API it uses comes straight from LGFX_Sprite.
// Only rotation, brightness and invert need device-specific handling.
class JC_Lcd : public lgfx::LGFX_Sprite {
public:
  // cfg.display_rotation carries M5-panel values 1/3/5/7 ("buttons down/up" x mirror).
  // The UI sprite is landscape-native, so those map to sprite rotations 0/2/4/6.
  void setRotation(uint_fast8_t r) { lgfx::LGFX_Sprite::setRotation(r & ~1u); }
  // Brightness is backlight PWM, not a panel register.
  void setBrightness(uint8_t b) { ledcWrite(JC_BL_LEDC_CHANNEL, b); }
  // Inversion is a panel command - forwarded to the Arduino_GFX driver in the .cpp.
  void invertDisplay(bool i);
};

class JC_Button {
public:
  void setRawState(bool pressed, uint32_t ms);
  bool wasPressed()  { bool e = _edge; _edge = false; return e; }
  bool isPressed()   { return _stable; }
private:
  bool _raw = false, _stable = false, _edge = false;
  uint32_t _lastChange = 0;
  static const uint32_t DEBOUNCE_MS = 20;
};

class JC_Touch {
public:
  bool isEnabled() { return true; }
  uint8_t getCount();
};

class JC_Speaker {
public:
  void setVolume(uint8_t v) { _volume = v; }
  void tone(uint16_t freq, uint32_t duration_ms);
  bool isPlaying();
  void mute();
private:
  uint8_t _volume = 128;
  friend class JC_M5;
};

class JC_Power {
public:
  int  getBatteryLevel()   { return -1; }  // no fuel gauge -> sketch hides the battery icon
  int  getBatteryVoltage() { return 0; }
  void powerOff();                          // deep sleep (no PMIC); reset button restarts
};

struct JC_Config {
  bool clear_display = true;
  bool internal_spk  = true;
};

class JC_M5 {
public:
  JC_Config config() { return JC_Config(); }
  void begin(const JC_Config &cfg);
  void update();                            // touch -> synthetic buttons
  int  getBoard() { return 999; }           // sketch only serial-prints this

  JC_Lcd  Lcd;
  JC_Lcd  &Display = Lcd;
  JC_Button BtnA, BtnB, BtnC;
  JC_Touch  Touch;
  JC_Speaker Speaker;
  JC_Power   Power;
};

extern JC_M5 M5;

bool halSDBegin();

#endif // DEVICE_JC3248W535
#endif // _HAL_JC3248W535_H
