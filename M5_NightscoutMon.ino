/*  M5Stack Nightscout monitor
    Copyright (C) 2018-2021 Martin Lukasek <martin@lukasek.cz>
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>. 
    
    This software uses some 3rd party libraries:
    IniFile by Steve Marple <stevemarple@googlemail.com> (GNU LGPL v2.1)
    ArduinoJson by Benoit BLANCHON (MIT License) 
    IoT Icon Set by Artur Funk (GPL v3)
    DHT12 by Bobadas (Public domain)
    Additions to the code:
    Peter Leimbach (Nightscout token)
    Sulka Haro (Nightscout API queries help)
    Ben West (AP WiFi configuration)
*/

// Official M5Stack Arduino board definitions are required, please add board from following location:
// https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
// 
// Only following boards are supported in this code:
// M5Stack Arduino / M5Stack-Core-ESP32
// M5Stack Arduino / M5Stack-Core2

#include <Arduino.h>
#include "M5NSDevice.h"  // SD.h + M5Unified, or the JC3248W535 shim (build with -DDEVICE_JC3248W535)
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "time.h"
#include "externs.h"
// #include <util/eu_dst.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel pixels(10, 15, NEO_GRB + NEO_KHZ800);

#include "Free_Fonts.h"
#include "IniFile.h"
#include "M5NSconfig.h"
#include "M5NSWebConfig.h"
#include "M5NSDexcom.h"
#include "M5NSLibre.h"

#include <Wire.h>     //The DHT12 uses I2C comunication.
#include "DHT12.h"
DHT12 dht12;

#include "SHT3X.h"
SHT3X sht30;

#include "microdot.h"
MicroDot MD;

String M5NSversion("2026072302");

#define VIBfreq 10000
#define VIBchannel 14
#define VIBresolution 10

// The UDP library class
WiFiUDP udp;
#define UDP_TX_PACKET_MAX_SIZE 2048
#define UDP_SEND_RETRIES 3
// IP address to send UDP data to
// const char * udpAddress = "192.168.1.255";
const int udpPort = 50555;

// buffers for receiving and sending UDP data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  //buffer to hold incoming packet,

// extern const unsigned char alarmSndData[];
extern const unsigned char sun_icon16x16[];
extern const unsigned char clock_icon16x16[];
extern const unsigned char timer_icon16x16[];
// extern const unsigned char powerbutton_icon16x16[];
extern const unsigned char door_icon16x16[];
extern const unsigned char warning_icon16x16[];
extern const unsigned char wifi1_icon16x16[];
extern const unsigned char wifi2_icon16x16[];
extern const unsigned char bat0_icon16x16[];
extern const unsigned char bat1_icon16x16[];
extern const unsigned char bat2_icon16x16[];
extern const unsigned char bat3_icon16x16[];
extern const unsigned char bat4_icon16x16[];
extern const unsigned char plug_icon16x16[];

Preferences preferences;
tConfig cfg;

// Starfield Services Root Certificate Authority - G2
// Valid till Dec 31 23:59:59 2037 GMT
const char* rootCACertificate = \
"-----BEGIN CERTIFICATE-----\n" \
"MIID7zCCAtegAwIBAgIBADANBgkqhkiG9w0BAQsFADCBmDELMAkGA1UEBhMCVVMx" \
"EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxJTAjBgNVBAoT" \
"HFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xOzA5BgNVBAMTMlN0YXJmaWVs" \
"ZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5" \
"MDkwMTAwMDAwMFoXDTM3MTIzMTIzNTk1OVowgZgxCzAJBgNVBAYTAlVTMRAwDgYD" \
"VQQIEwdBcml6b25hMRMwEQYDVQQHEwpTY290dHNkYWxlMSUwIwYDVQQKExxTdGFy" \
"ZmllbGQgVGVjaG5vbG9naWVzLCBJbmMuMTswOQYDVQQDEzJTdGFyZmllbGQgU2Vy" \
"dmljZXMgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIwDQYJKoZI" \
"hvcNAQEBBQADggEPADCCAQoCggEBANUMOsQq+U7i9b4Zl1+OiFOxHz/Lz58gE20p" \
"OsgPfTz3a3Y4Y9k2YKibXlwAgLIvWX/2h/klQ4bnaRtSmpDhcePYLQ1Ob/bISdm2" \
"8xpWriu2dBTrz/sm4xq6HZYuajtYlIlHVv8loJNwU4PahHQUw2eeBGg6345AWh1K" \
"Ts9DkTvnVtYAcMtS7nt9rjrnvDH5RfbCYM8TWQIrgMw0R9+53pBlbQLPLJGmpufe" \
"hRhJfGZOozptqbXuNC66DQO4M99H67FrjSXZm86B0UVGMpZwh94CDklDhbZsc7tk" \
"6mFBrMnUVN+HL8cisibMn1lUaJ/8viovxFUcdUBgF4UCVTmLfwUCAwEAAaNCMEAw" \
"DwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFJxfAN+q" \
"AdcwKziIorhtSpzyEZGDMA0GCSqGSIb3DQEBCwUAA4IBAQBLNqaEd2ndOxmfZyMI" \
"bw5hyf2E3F/YNoHN2BtBLZ9g3ccaaNnRbobhiCPPE95Dz+I0swSdHynVv/heyNXB" \
"ve6SbzJ08pGCL72CQnqtKrcgfU28elUSwhXqvfdqlS5sdJ/PHLTyxQGjhdByPq1z" \
"qwubdQxtRbeOlKyWN7Wg0I8VRw7j6IPdj/3vQQF3zCepYoUz8jcI73HPdwbeyBkd" \
"iEDPfUYd/x7H4c7/I9vG+o1VTqkC50cRRj70/b17KSa7qWFiNyi2LSr2EIZkyXCn" \
"0q23KXB56jzaYyWf/Wi3MOxw+3WKt21gZ7IeyLnp2KhvAotnDU0mV3HaIPzBSlCN" \
"sSi6" \
"-----END CERTIFICATE-----\n";

WebServer w3srv(80);
const byte DNS_PORT = 53;
DNSServer dnsServer;

const char* ntpServer = "pool.ntp.org"; // "time.nist.gov", "time.google.com"
struct tm localTimeInfo;
int MAX_TIME_RETRY = 30;
int lastSec = 61;
int lastMin = 61;
char localTimeStr[30];

struct err_log_item {
  struct tm err_time;
  int err_code;
} err_log[10];
int err_log_ptr = 0;
int err_log_count = 0;
int rcnt = 4;

int dispPage = 0;
int lastDrawnPage = -1; // lets draw_page() tell a fresh page entry from its own 15s periodic redraw
#define PAGE_ERRLOG 3
#define PAGE_WEBQR  4
#define MAX_PAGE    PAGE_WEBQR
int maxPage = MAX_PAGE;

// icon positions for the first page - WiFi/log, Snooze, Battery
int icon_xpos[3] = {144, 144+18, 144+2*18};
int icon_ypos[3] = {0, 0, 0};

// analog clock global variables
uint16_t osx=120, osy=120, omx=120, omy=120, ohx=120, ohy=120;  // Saved H, M, S x & y coords
boolean initial = 1;
boolean mDNSactive = false;

#ifndef min
  #define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

void draw_page();

WiFiMulti WiFiMultiple;

unsigned long msCount;
// unsigned long msCountLog;
unsigned long msStart;
uint8_t lcdBrightness = 10;
const char iniFilename[] = "/M5NS.INI";

DynamicJsonDocument JSONdoc(16384);
time_t lastAlarmTime = 0;
time_t snoozeUntil = 0;
int snoozeMult = 0;
unsigned long lastButtonMillis = 0;
int udpSendSnoozeRetries = 0;
bool is_task_bootstrapping = 0;

struct NSinfo ns;

void setPageIconPos(int page) {
  switch(page) {
    case 0:
      icon_xpos[0] = 144;
      icon_xpos[1] = 144+18;
      icon_xpos[2] = 144+2*18;
      icon_ypos[0] = 0;
      icon_ypos[1] = 0;
      icon_ypos[2] = 0;
      break;
    case 1:
      icon_xpos[0] = 126;
      icon_xpos[1] = 126+18;
      icon_xpos[2] = 126+18; // 320-16;
      icon_ypos[0] = 0;
      icon_ypos[1] = 0;
      icon_ypos[2] = 18; // 220;
      break;
    case 2:
      icon_xpos[0] = 0+18;
      icon_xpos[1] = 0+18;
      icon_xpos[2] = 0+18;
      icon_ypos[0] = 110-18-9;
      icon_ypos[1] = 110-18+9;
      icon_ypos[2] = 110-18+27;
      break;
    default:
      icon_xpos[0] = 266;
      icon_xpos[1] = 266+18;
      icon_xpos[2] = 266+2*18;
      icon_ypos[0] = 0;
      icon_ypos[1] = 0;
      icon_ypos[2] = 0;
      break;
  }
}

void lcdSetBrightness(uint8_t brightness) {
  if( brightness>100 )
    brightness = 100;
  // M5.Display.setBrightness handles AXP LCD voltage (Core2), backlight PWM (Basic/Fire)
  // and CoreS3 internally; scale our 0-100 range to the 0-255 it expects.
  M5.Display.setBrightness(map(brightness, 0, 100, 0, 255));
}

void addErrorLog(int code){
  if(err_log_ptr>9) {
    for(int i=0; i<9; i++) {
      err_log[i].err_time=err_log[i+1].err_time;
      err_log[i].err_code=err_log[i+1].err_code;
    }
    err_log_ptr=9;
  }
  getLocalTime(&err_log[err_log_ptr].err_time);
  err_log[err_log_ptr].err_code=code;
  err_log_ptr++;
  err_log_count++;
}

// Human-readable text for a positive error-log code, shown on the error log page.
// Codes shared between data sources (JSON parse / no data) are labelled with the
// active source, so the log doesn't e.g. blame Dexcom while LibreLinkUp is selected.
static void errorLogMessage(int code, char *out, size_t outSize) {
  const char *src = (cfg.data_source==1) ? "Dexcom" :
                    (cfg.data_source==2) ? "LibreLinkUp" : "Nightscout";
  switch(code) {
    case 1001: snprintf(out, outSize, "JSON parse failed (%s)", src); break;
    case 1002: strlcpy(out, "No data from Nightscout", outSize); break;
    case 1003: strlcpy(out, "JSON2 parse failed (Nightscout)", outSize); break;
    case 1004: snprintf(out, outSize, "No current reading from %s", src); break;
    case 1101: strlcpy(out, "Dexcom communication error", outSize); break;
    case 1102: strlcpy(out, "Dexcom bad credentials - check username/password", outSize); break;
    case 1201: strlcpy(out, "LibreLinkUp login / API error", outSize); break;
    case 1202: strlcpy(out, "LibreLinkUp bad credentials - check email/password", outSize); break;
    case 1203: strlcpy(out, "Accept new LibreLinkUp terms in the LibreLinkUp app", outSize); break;
    case 1204: strlcpy(out, "No LibreLinkUp connection - link a sensor / accept sharing in the app", outSize); break;
    default:   snprintf(out, outSize, "HTTP error %d", code); break;
  }
}

// Word-wraps `text` for the error log page. The first line starts at (firstX, y);
// any wrapped continuation lines start at contX and run to rightEdge, so long
// messages use the full screen width instead of being clipped at the edge.
// Returns the number of lines drawn (>=1), capped at maxLines. Caller sets font/colour.
static int drawWrappedString(const char *text, int firstX, int contX, int y,
                             int rightEdge, int lineH, int maxLines) {
  char line[96];
  line[0] = 0;
  int lines = 0;
  int curX = firstX;
  const char *word = text;
  while(*word) {
    while(*word==' ') word++;
    if(!*word) break;
    const char *e = word;
    while(*e && *e!=' ') e++;
    int wlen = e - word;
    char cand[96];
    if(line[0])
      snprintf(cand, sizeof(cand), "%s %.*s", line, wlen, word);
    else
      snprintf(cand, sizeof(cand), "%.*s", wlen, word);
    if(line[0] && (curX + M5.Lcd.textWidth(cand) > rightEdge)) {
      M5.Lcd.drawString(line, curX, y + lines*lineH); // flush full line, then wrap
      lines++;
      if(lines >= maxLines)
        return lines;
      curX = contX;
      snprintf(line, sizeof(line), "%.*s", wlen, word);
    } else {
      strlcpy(line, cand, sizeof(line));
    }
    word = e;
  }
  if(line[0] && lines < maxLines) {
    M5.Lcd.drawString(line, curX, y + lines*lineH);
    lines++;
  }
  return lines ? lines : 1;
}

uint16_t crc16_update(uint16_t crc, uint8_t a)
{
  int i;
  crc ^= a;
  for (i = 0; i < 8; ++i)
  {
    if (crc & 1)
    crc = (crc >> 1) ^ 0xA001;
    else
    crc = (crc >> 1);
  }
  return crc;
}

uint16_t calcCRC(char* str)
{
  uint16_t crc=0; // starting value as you like, must be the same before each calculation
  for (int i=0;i<strlen(str);i++) // for each character in the string
  {
    crc= crc16_update (crc, str[i]); // update the crc value
  }
  return crc;
}

void startupLogo() {
    // static uint8_t brightness, pre_brightness;
    lcdSetBrightness(0);
    if(!SD.exists(cfg.bootPic)) {
      // M5.Lcd.pushImage(0, 0, 320, 240, (uint16_t *)gImage_logoM5);
      M5.Lcd.clear();
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.drawString("M5 Stack", 120, 60);
      M5.Lcd.drawString("Nightscout monitor", 60, 80);
      M5.Lcd.drawString("(c) 2019-21 Martin Lukasek", 0, 120);
    } else {
      M5.Lcd.drawJpgFile(SD, cfg.bootPic);
    }
    lcdSetBrightness(100);
    M5.update();
}

void printLocalTime() {
  if(!getLocalTime(&localTimeInfo)){
    Serial.println("Failed to obtain time");
    M5.Lcd.println("Failed to obtain time");
    return;
  }
  Serial.println(&localTimeInfo, "%A, %B %d %Y %H:%M:%S");
  M5.Lcd.println(&localTimeInfo, "%A, %B %d %Y %H:%M:%S");
}

// Unified audio for all boards: M5.Speaker abstracts DAC (Basic) vs I2S (Core2/CoreS3).
// The vibration motor is external hardware on a configurable pin, so it stays custom ledc.
void play_tone(uint16_t frequency, uint32_t duration, uint8_t volume) {
  bool vib = (cfg.vibration_mode != 0);
  if(vib) {
    ledcSetup(VIBchannel, VIBfreq, VIBresolution);
    ledcAttachPin(cfg.vibration_pin, VIBchannel);
    delay(10);
    ledcWrite(VIBchannel, cfg.vibration_strength);
  }
  M5.Speaker.setVolume(map(volume, 0, 100, 0, 255));
  M5.Speaker.tone(frequency, duration);
  while(M5.Speaker.isPlaying())
    delay(1);
  if(vib) {
    if(duration<180) // minimum vibration lenght is 200 ms
      delay(180-duration);
    ledcWrite(VIBchannel, 0);
  }
}

void sndAlarm() {
  for(int j=0; j<6; j++) {
    if(cfg.LED_strip_mode != 0) {
      int maxint = 255;
      maxint *= cfg.LED_strip_brightness;
      maxint /= 100;
      pixels.fill(pixels.Color(maxint, 0, 0));
      pixels.show();
    }
    if( cfg.dev_mode )
      play_tone(660, 400, 1);
    else
      play_tone(660, 400, cfg.alarm_volume);
    if(cfg.LED_strip_mode != 0) {
      pixels.clear();
      pixels.show();
    }
    delay(200);
  }
}

void sndWarning() {
  for(int j=0; j<3; j++) {
    if(cfg.LED_strip_mode != 0) {
      int maxint = 255;
      maxint *= cfg.LED_strip_brightness;
      maxint /= 100;
      int lessint = 192;
      lessint *= cfg.LED_strip_brightness;
      lessint /= 100;
      pixels.fill(pixels.Color(maxint, lessint, 0));
      pixels.show();
    }
    if( cfg.dev_mode )
      play_tone(3000, 100, 1);
    else
      play_tone(3000, 100, cfg.warning_volume);
    if(cfg.LED_strip_mode != 0) {
      pixels.clear();
      pixels.show();
    }
    delay(300);
  }
}

void drawIcon(int16_t x, int16_t y, const uint8_t *bitmap, uint16_t color) {
  int16_t w = 16;
  int16_t h = 16; 
  int32_t i, j, byteWidth = (w + 7) / 8;
  for (j = 0; j < h; j++) {
    for (i = 0; i < w; i++) {
      if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
        M5.Lcd.drawPixel(x + i, y + j, color);
      }
    }
  }
}

void waitBtnRelease() {
  // wait for any touch to be released (no-op on button-only boards)
  while(M5.Touch.getCount()) {
    M5.update();
    delay(20);
  }
}

void buttons_test() {

  // On touch boards (Core2/CoreS3) M5Unified maps the bottom-of-screen zones to BtnA/B/C,
  // so the same three-button logic works everywhere.
  bool btnA_wasPressed = M5.BtnA.wasPressed();
  bool btnB_wasPressed = M5.BtnB.wasPressed();
  bool btnC_wasPressed = M5.BtnC.wasPressed();

  if(btnA_wasPressed) {
    // M5.Lcd.printf("A");
    Serial.printf("A");
    // play_tone(1000, 10, 1);
    // sndAlarm();
    if(lcdBrightness==cfg.brightness1) 
      lcdBrightness = cfg.brightness2;
    else
      if(lcdBrightness==cfg.brightness2) 
        lcdBrightness = cfg.brightness3;
      else
        lcdBrightness = cfg.brightness1;
    lcdSetBrightness(lcdBrightness);
    M5.update();
    waitBtnRelease();
    // addErrorLog(500);
    /* UDP send test
    IPAddress broadcastIp = ~WiFi.subnetMask() | WiFi.gatewayIP();
    Serial.print("Sending broadcast to: ");
    Serial.println(broadcastIp);
    udp.beginPacket(broadcastIp, udpPort); // udpAddress
    udp.printf("Seconds since boot: %lu", millis()/1000);
    udp.endPacket();
    */
  }

  if(btnB_wasPressed) {
    // M5.Lcd.printf("B");
    Serial.printf("B");

    if(dispPage == PAGE_WEBQR) {
      // Config page: middle button takes the update draw_page() found on page entry,
      // instead of snoozing (which is meaningless on this page). Nothing to do if none
      // is available - button meaning reverts to snooze the moment you leave the page,
      // since it's derived from dispPage, not stored state.
      if(otaUpdateAvailable())
        otaRunUpdate();
      M5.update();
      waitBtnRelease();
    } else {
      /*
      play_tone(440, 100, 1);
      delay(10);
      play_tone(880, 100, 1);
      delay(10);
      play_tone(1760, 100, 1);
      */
      struct tm timeinfo;
      bool timeOK = getLocalTime(&timeinfo);
      if( (millis()-lastButtonMillis)<2000 ) {
        // snoozing just recently - will multiply snooze time
        // Serial.printf("lastButton < 2s \r\n");
        snoozeMult++;
        if(snoozeMult>4)
          snoozeMult = 0;
      } else {
        // Serial.printf("lastButton > 2s,  MULT = 1\r\n");
        // new Snooze
        snoozeMult = 1;
      }
      if(!timeOK){
        // Serial.printf("Time not OK - NO SLEEP\r\n");
        snoozeUntil = 0;
      } else {
        snoozeUntil = mktime(&timeinfo) + snoozeMult*cfg.snooze_timeout*60;
        Serial.print("snoozeUntil = "); Serial.println(snoozeUntil);
      }

      M5.Lcd.fillRect(110, 220, 100, 20, TFT_WHITE);
      M5.Lcd.setTextDatum(TL_DATUM);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSSB12);
      M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
      char tmpStr[10];
      int snoozeRemaining = 0;
      if(timeOK) {
        snoozeRemaining = difftime(snoozeUntil, mktime(&timeinfo));
        if(snoozeRemaining<0)
          snoozeRemaining = 0;
      }
      if(snoozeMult==0)
        strcpy(tmpStr, "OFF");
      else {
        sprintf(tmpStr, "%i", (snoozeRemaining+59)/60);
        if(cfg.LED_strip_mode==2) {
          pixels.clear();
          pixels.show();
        }
      }
      int txw=M5.Lcd.textWidth(tmpStr);
      Serial.print("Set SNOOZE: "); Serial.print(tmpStr); Serial.print(", snoozeUntil-now = "); Serial.println(snoozeRemaining);
      M5.Lcd.drawString(tmpStr, 159-txw/2, 220);
      if(dispPage<maxPage) {
        if(snoozeMult==0)
          M5.Lcd.fillRect(icon_xpos[1], icon_ypos[1], 16, 16, BLACK);
        else
          drawIcon(icon_xpos[1], icon_ypos[1], (uint8_t*)clock_icon16x16, TFT_RED);
      }
      udpSendSnoozeRetries = UDP_SEND_RETRIES;
      lastButtonMillis = millis();
      M5.update();
      waitBtnRelease();
    }
  }

  if(btnC_wasPressed) {
    // M5.Lcd.printf("C");
    Serial.printf("C");
    int longPress = 0;
    // long-press C to power off (physical button on Basic/Fire, held touch zone on Core2/CoreS3)
    unsigned long btnCPressTime = millis();
    long pwrOffTimeout = 4000;
    int lastDispTime = pwrOffTimeout/1000;
    char tmpstr[32];
    while(M5.BtnC.isPressed()) {
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSSB12);
      // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
      // M5.Lcd.fillRect(0, 220, 320, 20, TFT_RED);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      int timeToPwrOff = (pwrOffTimeout - (millis()-btnCPressTime))/1000;
      if((lastDispTime!=timeToPwrOff) && (millis()-btnCPressTime>800)) {
        longPress = 1;
        sprintf(tmpstr, "OFF in %1d   ", timeToPwrOff);
        M5.Lcd.drawString(tmpstr, 210, 220);
        lastDispTime=timeToPwrOff;
      }
      if(timeToPwrOff<=0) {
        // play_tone(3000, 100, 1);
        M5.Power.powerOff();
      }
      M5.update();
    }
    if(longPress) {
      M5.Lcd.fillRect(210, 220, 110, 20, TFT_BLACK);
      drawIcon(246, 220, (uint8_t*)door_icon16x16, TFT_LIGHTGREY);
    } else {
      dispPage++;
      if(dispPage>maxPage)
        dispPage = 0;
      setPageIconPos(dispPage);
      M5.Lcd.clear(BLACK);
      // msCount = millis()-16000;
      draw_page();
      // play_tone(440, 100, 1);
    }
    M5.update();
    waitBtnRelease();
  }
}


const char * CONSONANTS = "bcdfghjklmnpqrstvwxyz";
const char * VOWELS = "aeiouy";
int passLen = 8;

void generate_ssid_passphrase (char * buffer) {
  char passphrase[passLen+1];
  int max_consonants;
  int max_vowels;
  max_consonants = strlen(CONSONANTS);
  max_vowels = strlen(VOWELS);
  randomSeed(analogRead(0));
  passphrase[0] = CONSONANTS[random(0, max_consonants)];
  passphrase[1] = VOWELS[random(0, max_vowels)];
  passphrase[2] = VOWELS[random(0, max_vowels)];
  passphrase[3] = CONSONANTS[random(0, max_consonants)];
  passphrase[4] = VOWELS[random(0, max_vowels)];
  passphrase[5] = VOWELS[random(0, max_vowels)];
  passphrase[6] = CONSONANTS[random(0, max_consonants)];
  passphrase[7] = VOWELS[random(0, max_vowels)];
  passphrase[8] = VOWELS[random(0, max_vowels)];
  passphrase[9] = '\0';
  strcpy(buffer, passphrase);
}

void wifi_connect() {
  char ssid_passphrase[80];
  if (is_task_bootstrapping) {
    // offer access point to join for configuration purposes
    WiFi.enableAP(true);
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 0, 1);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    Serial.print("Setting soft-AP configuration ... ");
    Serial.println(WiFi.softAPConfig(ip, gateway, subnet) ? "Ready" : "Failed!");
    // set random password
    generate_ssid_passphrase(ssid_passphrase);
    Serial.print("Setting soft-AP ... ");
    Serial.println(WiFi.softAP(cfg.deviceName, ssid_passphrase) ? "Ready" : "Failed!");
    delay(250);
    Serial.print("Soft-AP IP address = ");
    Serial.println(WiFi.softAPIP());
    // WiFi.begin( );
    dnsServer.setTTL(300);
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
    dnsServer.start(DNS_PORT, "*", ip);    
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cfg.deviceName);
    WiFi.disconnect();
    delay(100);

    Serial.println("WiFi connect start");
    M5.Lcd.println("WiFi connect start");

    // We start by connecting to a WiFi network
    for(int i=0; i<=9; i++) {
      if(cfg.wlanssid[i][0]!=0) {
        if(cfg.wlanpass[i][0]==0) {
          // no or empty password -> send NULL
          WiFiMultiple.addAP(cfg.wlanssid[i], NULL);
        } else {
          WiFiMultiple.addAP(cfg.wlanssid[i], cfg.wlanpass[i]);
        }
      }
    }

    Serial.println();
    M5.Lcd.println("");
    Serial.print("Wait for WiFi... ");
    M5.Lcd.print("Wait for WiFi... ");

    while(WiFiMultiple.run() != WL_CONNECTED) {
        Serial.print(".");
        M5.Lcd.print(".");
        delay(500);
    }
  }

  Serial.println("");
  M5.Lcd.println("");
  Serial.print("WiFi connected to SSID ");
  if (is_task_bootstrapping) {
    Serial.println(cfg.deviceName);
    Serial.print("SSID Passphrase: "); Serial.println(ssid_passphrase);

    // Two-column layout: text on the left, a big QR code on the right. Columns don't
    // overlap horizontally, so text line count/spacing can't collide with the QR
    // regardless of vertical position - a leftover cursor position from earlier boot
    // messages was the cause of the old overlap, so start from a clean, fully-positioned
    // screen instead of continuing whatever line the cursor was left on.
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(4, 4);
    M5.Lcd.println("SSID:");
    M5.Lcd.setCursor(4, 24);
    M5.Lcd.println(cfg.deviceName);
    M5.Lcd.setCursor(4, 48);
    M5.Lcd.println("Password:");
    M5.Lcd.setCursor(4, 68);
    M5.Lcd.println(ssid_passphrase);

    // ".local" mDNS can't resolve before the phone has even joined this AP, and is
    // unreliable from phones even after - the wildcard captive-portal DNS below
    // (dnsServer.start(DNS_PORT, "*", ip)) answers every lookup with the device's own
    // IP, so most phones auto-prompt to open the config page once joined; the IP is
    // the reliable fallback if that prompt doesn't appear.
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(4, 96);
    M5.Lcd.println("Then open");
    M5.Lcd.setCursor(4, 116);
    M5.Lcd.println("your browser");

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 142);
    M5.Lcd.println("If not prompted,");
    M5.Lcd.setCursor(4, 156);
    M5.Lcd.print("visit ");
    M5.Lcd.println(WiFi.softAPIP());

    M5.Lcd.setCursor(165, 26);
    M5.Lcd.println("Scan to join:");

    // Most phone camera apps recognize this format natively and offer to join with one tap.
    char wifiQR[96];
    snprintf(wifiQR, sizeof(wifiQR), "WIFI:S:%s;T:WPA;P:%s;;", cfg.deviceName, ssid_passphrase);
    M5.Lcd.qrcode(wifiQR, 165, 45, 150, 3);
  } else {
    Serial.println(WiFi.SSID());
    M5.Lcd.print("WiFi SSID: "); M5.Lcd.println(WiFi.SSID());
    Serial.print("IP address: ");
    M5.Lcd.print("IP address: ");
    Serial.println(WiFi.localIP());
    M5.Lcd.println(WiFi.localIP());

    configTime(cfg.timeZone, cfg.dst, ntpServer, "time.nist.gov", "time.google.com");
    delay(1000);
    Serial.print("Waiting for time.");
    int i = 0;
    while(!getLocalTime(&localTimeInfo)) {
      Serial.print(".");
      delay(1000);
      i++;
      if (i > MAX_TIME_RETRY) {
        Serial.print("Gave up waiting for time to have a valid value.");
        break;
      }
    }
    Serial.println();
    printLocalTime();

    Serial.println("Connection done");
    M5.Lcd.println("Connection done");
  }

}

int8_t getBatteryLevel()
{
  // M5.Power.getBatteryLevel() works across AXP192 (Core2), AXP2101 (CoreS3) and IP5306 (Basic/Fire)
  int8_t bl = M5.Power.getBatteryLevel();
  Serial.printf("Battery %d mV = %d%%\r\n", M5.Power.getBatteryVoltage(), bl);
  return bl;
}

void drawArrow(int x, int y, int asize, int aangle, int pwidth, int plength, uint16_t color){
  float dx = (asize-10)*cos(aangle-90)*PI/180+x; // calculate X position  
  float dy = (asize-10)*sin(aangle-90)*PI/180+y; // calculate Y position  
  float x1 = 0;         float y1 = plength;
  float x2 = pwidth/2;  float y2 = pwidth/2;
  float x3 = -pwidth/2; float y3 = pwidth/2;
  float angle = aangle*PI/180-135;
  float xx1 = x1*cos(angle)-y1*sin(angle)+dx;
  float yy1 = y1*cos(angle)+x1*sin(angle)+dy;
  float xx2 = x2*cos(angle)-y2*sin(angle)+dx;
  float yy2 = y2*cos(angle)+x2*sin(angle)+dy;
  float xx3 = x3*cos(angle)-y3*sin(angle)+dx;
  float yy3 = y3*cos(angle)+x3*sin(angle)+dy;
  M5.Lcd.fillTriangle(xx1,yy1,xx3,yy3,xx2,yy2, color);
  M5.Lcd.drawLine(x, y, xx1, yy1, color);
  M5.Lcd.drawLine(x+1, y, xx1+1, yy1, color);
  M5.Lcd.drawLine(x, y+1, xx1, yy1+1, color);
  M5.Lcd.drawLine(x-1, y, xx1-1, yy1, color);
  M5.Lcd.drawLine(x, y-1, xx1, yy1-1, color);
  M5.Lcd.drawLine(x+2, y, xx1+2, yy1, color);
  M5.Lcd.drawLine(x, y+2, xx1, yy1+2, color);
  M5.Lcd.drawLine(x-2, y, xx1-2, yy1, color);
  M5.Lcd.drawLine(x, y-2, xx1, yy1-2, color);
}

void drawMiniGraph(struct NSinfo *ns){
  /*
  // draw help lines
  for(int i=0; i<320; i+=40) {
    M5.Lcd.drawLine(i, 0, i, 240, TFT_DARKGREY);
  }
  for(int i=0; i<240; i+=30) {
    M5.Lcd.drawLine(0, i, 320, i, TFT_DARKGREY);
  }
  M5.Lcd.drawLine(0, 120, 320, 120, TFT_LIGHTGREY);
  M5.Lcd.drawLine(160, 0, 160, 240, TFT_LIGHTGREY);
  */
  int i;
  float glk;
  uint16_t sgvColor;
  // M5.Lcd.drawLine(231, 110, 319, 110, TFT_DARKGREY);
  // M5.Lcd.drawLine(231, 110, 231, 207, TFT_DARKGREY);
  // M5.Lcd.drawLine(231, 207, 319, 207, TFT_DARKGREY);
  // M5.Lcd.drawLine(319, 110, 319, 207, TFT_DARKGREY);
  M5.Lcd.drawLine(231, 113, 319, 113, TFT_LIGHTGREY);
  M5.Lcd.drawLine(231, 203, 319, 203, TFT_LIGHTGREY);
  M5.Lcd.drawLine(231, 200-(4-3)*10+3, 319, 200-(4-3)*10+3, TFT_LIGHTGREY);
  M5.Lcd.drawLine(231, 200-(9-3)*10+3, 319, 200-(9-3)*10+3, TFT_LIGHTGREY);
  Serial.print("Last 10 values: ");
  for(i=9; i>=0; i--) {
    sgvColor = TFT_GREEN;
    glk = *(ns->last10sgv+9-i);
    if(glk>12) {
      glk = 12;
    } else {
      if(glk<3) {
        glk = 3;
      }
    }
    if(glk<cfg.red_low || glk>cfg.red_high) {
      sgvColor = TFT_RED;
    } else {
      if(glk<cfg.yellow_low || glk>cfg.yellow_high) {
        sgvColor = TFT_YELLOW;
      }
    }
    Serial.print(*(ns->last10sgv+i)); Serial.print(" ");
    if(*(ns->last10sgv+9-i)!=0)
      M5.Lcd.fillCircle(234+i*9, 203-(glk-3.0)*10.0, 3, sgvColor);
  }
  Serial.println();
}

// Dispatches to the active glucose data source, selected by cfg.data_source.
int readDataSource() {
  switch(cfg.data_source) {
    case 1:
      return readDexcom(&cfg, &ns);
    case 2:
      return readLibre(&cfg, &ns);
    default:
      return readNightscout(cfg.url, cfg.token, &ns);
  }
}

int readNightscout(char *url, char *token, struct NSinfo *ns) {
  HTTPClient http;
  WiFiClientSecure sclient;
  sclient.setCACert(rootCACertificate);

  char NSurl[128];
  int err=0;
  char tmpstr[64];
  bool is_https_Heroku = false;

  // http.setReuse(false);
  // if(client) {
  // sclient.setCACert(rootCACertificate);
  //  Serial.println("Client OK, setCACert OK");
  //}
  
  if((WiFiMultiple.run() == WL_CONNECTED)) {
    // configure target server and url
    if(strncmp(url, "http", 4))
      strcpy(NSurl,"https://");
    else
      strcpy(NSurl,"");
    strcat(NSurl,url);
    if(strlen(NSurl)>0) {
      if(NSurl[strlen(NSurl)-1]=='/') {
        NSurl[strlen(NSurl)-1]=0;
      }
    }
    is_https_Heroku = (strstr(NSurl,"https://") != NULL) && (strstr(NSurl,"herokuapp.com") != NULL);
    Serial.print("is_https_Heroku "); Serial.println(is_https_Heroku);
    if(cfg.sgv_only) {
      strcat(NSurl,"/api/v1/entries.json?find[type][$eq]=sgv&count=10");
    } else {
      strcat(NSurl,"/api/v1/entries.json?count=10");
    }
    if ((token!=NULL) && (strlen(token)>0)) {
      strcat(NSurl,"&token=");
      strcat(NSurl,token);
    }

    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)wifi2_icon16x16, TFT_BLUE);
    
    Serial.print("JSON query NSurl = \'");Serial.print(NSurl);Serial.print("\'\r\n");
    // http.begin(NSurl, "94:FC:F6:23:6C:37:D5:E7:92:78:3C:0B:5F:AD:0C:E4:9E:FD:9E:A8"); //HTTP
    if( is_https_Heroku ) {
      if(http.begin(sclient, NSurl)) {
        Serial.println("http.begin HTTPS Heroku OK");
      } else {
        Serial.println("http.begin HTTPS Heroku FAILED");
      }
    } else {
      if(http.begin(NSurl)) {
        Serial.println("http.begin OK");
      } else {
        Serial.println("http.begin FAILED");
      }
    }
    // http.connect();
    // Serial.print("Connected "); Serial.println(http.connected()?"YES":"NO");
    // http.setConnectTimeout(15000);
    // http.setTimeout(15000);
    
    // Serial.print("[HTTP] GET...\r\n");
    // start connection and send HTTP header
    
    const char * headerKeys[] = {"date", "server", "location"};
    const size_t numberOfHeaders = 3;
    http.collectHeaders(headerKeys, numberOfHeaders);

    unsigned long timeInGET;
    timeInGET = millis();
    int httpCode = http.GET();
    timeInGET = millis()-timeInGET;
    
    // httpCode will be negative on error
    if(httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d after %lu ms\r\n", httpCode, timeInGET);

      // file found at server
      if(httpCode == HTTP_CODE_OK) {
        String json = http.getString();
        Serial.printf("GET() response JSON length = %d\r\n", json.length());
        // remove any non text characters (just for sure)
        for(int i=0; i<json.length(); i++) {
          // Serial.print(json.charAt(i), DEC); Serial.print(" = "); Serial.println(json.charAt(i));
          if(json.charAt(i)<32 /* || json.charAt(i)=='\\' */) {
            json.setCharAt(i, 32);
          }
        }
        // json.replace("\\n"," ");
        // invalid Unicode character defined by Ascensia Diabetes Care Bluetooth Glucose Meter or Medtronic
        // ArduinoJSON does not accept any unicode surrogate pairs like \u0032 or \u0000
        json.replace("\\u0000"," ");
        json.replace("\\u000b"," ");
        json.replace("\\u0032"," ");
        // Serial.println(json);
        // const size_t capacity = JSON_ARRAY_SIZE(10) + 10*JSON_OBJECT_SIZE(19) + 3840;
        // Serial.print("JSON size needed= "); Serial.print(capacity); 
        int ndx=0;
        int sr=json.indexOf("\"date\":", ndx);
        while(sr!=-1) {
          ndx=sr+1;
          if(sr+20<json.length()) {
            // Serial.printf("Found date at position %d with char '%c' at +20\r\n", sr, json.charAt(sr+20));
            if(json.charAt(sr+20)=='.') {
              Serial.printf("Deleting at postion %d char '%c'\r\n", sr+20, json.charAt(sr+20));
              json.remove(sr+20,1);
              while(sr+20<json.length() && json.charAt(sr+20)>='0' && json.charAt(sr+20)<='9') {
                Serial.printf("Cyclus deleting at postition %d char '%c'\r\n", sr+20, json.charAt(sr+20));
                json.remove(sr+20,1);
              }
            }
          }
          sr=json.indexOf("\"date\":", ndx);
        }
        // Serial.println(json);
        Serial.print("Free Heap = "); Serial.println(ESP.getFreeHeap());
        DeserializationError JSONerr = deserializeJson(JSONdoc, json);
        if(JSONerr) {
          Serial.printf("JSON DeserializationError %s\r\n", JSONerr.c_str());
        } else {
          Serial.println("JSON deserialized OK");
        }
        JsonArray arr=JSONdoc.as<JsonArray>();
        Serial.print("JSON array size = "); Serial.println(arr.size());
        if (JSONerr || arr.size()==0) {   //Check for errors in parsing
          if(JSONerr) {
            err=1001; // "JSON parsing failed"
            // Serial.println("JSON parsing failed");
          } else {
            err=1002; // "No data from Nightscout"
            // Serial.println("No data from Nightscout");
          }
          addErrorLog(err);
        } else {
          JsonObject obj;
          int sgvindex = 0;
          do {
            obj=JSONdoc[sgvindex].as<JsonObject>();
            sgvindex++;
          } while ((!obj.containsKey("sgv")) && (sgvindex<(arr.size()-1)));
          sgvindex--;
          if(sgvindex<0 || sgvindex>(arr.size()-1))
            sgvindex=0;
          strlcpy(ns->sensDev, JSONdoc[sgvindex]["device"] | "N/A", 64);
          ns->is_xDrip = obj.containsKey("xDrip_raw");
          /*
          JsonVariant answer = JSONdoc[sgvindex]["date"];
          const char* s = answer.as<char*>();
          if(s!=NULL)
            strlcpy(tmpstr, s, 32);
          else
            tmpstr[0]=0;
          Serial.printf("DATE string: %s\r\n", tmpstr);
          double LD=answer;
          Serial.printf("DATE double: %lf\r\n", LD);
          */
          ns->rawtime = JSONdoc[sgvindex]["date"].as<long long>(); // sensTime is time in milliseconds since 1970, something like 1555229938118
          ns->sensTime = ns->rawtime / 1000; // no milliseconds, since 2000 would be - 946684800, but ok
          strlcpy(ns->sensDir, JSONdoc[sgvindex]["direction"] | "N/A", 32);
          if(strcmp(ns->sensDir, "N/A")==0) { // Railway uses "trend" instead of "direction"
            strlcpy(ns->sensDir, JSONdoc[sgvindex]["trend"] | "N/A", 32);
          }
          ns->sensSgv = JSONdoc[sgvindex]["sgv"]; // get value of sensor measurement
          for(int i=0; i<=9; i++) {
            ns->last10sgv[i]=JSONdoc[i]["sgv"];
            ns->last10sgv[i]/=18.0;
          }
          ns->sensSgvMgDl = ns->sensSgv;
          // internally we work in mmol/L
          ns->sensSgv/=18.0;
          
          localtime_r(&ns->sensTime, &ns->sensTm);
          
          ns->arrowAngle = directionToArrowAngle(ns->sensDir);

          Serial.print("sensDev = ");
          Serial.println(ns->sensDev);
          Serial.print("sensTime = ");
          Serial.print(ns->sensTime);
          sprintf(tmpstr, " (JSON %lld)", (long long) ns->rawtime);
          Serial.print(tmpstr);
          sprintf(tmpstr, " = %s\r", ctime(&ns->sensTime)); // The ctime() string is followed by a new-line character ('\n')
          Serial.print(tmpstr);
          Serial.print("sensSgv = ");
          Serial.println(ns->sensSgv);
          Serial.print("sensDir = ");
          Serial.println(ns->sensDir);
          // Serial.print(ns->sensTm.tm_year+1900); Serial.print(" / "); Serial.print(ns->sensTm.tm_mon+1); Serial.print(" / "); Serial.println(ns->sensTm.tm_mday);
          Serial.print("Sensor time: "); Serial.print(ns->sensTm.tm_hour); Serial.print(":"); Serial.print(ns->sensTm.tm_min); Serial.print(":"); Serial.print(ns->sensTm.tm_sec); Serial.print(" DST "); Serial.println(ns->sensTm.tm_isdst);
        } 
      } else {
        // httpCode containts http response error
        addErrorLog(httpCode);
        err=httpCode;
        /*
        for(int i = 0; i< http.headers(); i++){
          Serial.print("Header: "); Serial.println(http.header(i));
        } */
        if(err==301 || err==302) {
          if(http.header("location").length()>0) {
            strncpy(cfg.url, http.header("location").c_str(), 127);
            Serial.printf("HTTP error %d, redirecting to \"%s\"\r\n", err, cfg.url);
            rcnt = 4; // retry the get asap
          }
        }
      }
    } else {
      // httpCode < 0 = internal http error
      addErrorLog(httpCode);
      err=httpCode;
    }
    http.end();

    if(err!=0) {
      Serial.printf("Returning with error %d after %lu ms :-(\r\n", err, timeInGET);
      Serial.println(http.errorToString(err));
      return err;
    }
      

    // the second query
    if(strncmp(url, "http", 4))
      strcpy(NSurl,"https://");
    else
      strcpy(NSurl,"");
    strcat(NSurl,url);
    if(strlen(NSurl)>0) {
      if(NSurl[strlen(NSurl)-1]=='/') {
        NSurl[strlen(NSurl)-1]=0;
      }
    }
    switch(cfg.info_line) {
      case 2:
        strcat(NSurl,"/api/v2/properties/iob,cob,delta,loop,basal");
        break;
      case 3:
        strcat(NSurl,"/api/v2/properties/iob,cob,delta,openaps,basal");
        break;
      default:
        strcat(NSurl,"/api/v2/properties/iob,cob,delta,basal");
    }
    
    if (strlen(token) > 0){
      strcat(NSurl,"?token=");
      strcat(NSurl,token);
    }
    
    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)wifi1_icon16x16, TFT_BLUE);

    Serial.print("Properties query NSurl = \'");Serial.print(NSurl);Serial.print("\'\r\n");
    // http.begin(NSurl, "94:FC:F6:23:6C:37:D5:E7:92:78:3C:0B:5F:AD:0C:E4:9E:FD:9E:A8"); //HTTP
    if( is_https_Heroku ) {
      if(http.begin(sclient, NSurl)) {
        Serial.println("http.begin propertires HTTPS Heroku OK");
      } else {
        Serial.println("http.begin propertires HTTPS Heroku FAILED");
      }
    } else {
      if(http.begin(NSurl)) {
        Serial.println("http.begin propertires OK");
      } else {
        Serial.println("http.begin propertires FAILED");
      }
    }
    // Serial.print("[HTTP] GET properties...\r\n");
    timeInGET = millis();
    httpCode = http.GET();
    timeInGET = millis()-timeInGET;
    if(httpCode > 0) {
      Serial.printf("[HTTP] GET properties... code: %d after %lu ms\r\n", httpCode, timeInGET);
      if(httpCode == HTTP_CODE_OK) {
        // const char* propjson = "{\"iob\":{\"iob\":0,\"activity\":0,\"source\":\"OpenAPS\",\"device\":\"openaps://Spike iPhone 8 Plus\",\"mills\":1557613521000,\"display\":\"0\",\"displayLine\":\"IOB: 0U\"},\"cob\":{\"cob\":0,\"source\":\"OpenAPS\",\"device\":\"openaps://Spike iPhone 8 Plus\",\"mills\":1557613521000,\"treatmentCOB\":{\"decayedBy\":\"2019-05-11T23:05:00.000Z\",\"isDecaying\":0,\"carbs_hr\":20,\"rawCarbImpact\":0,\"cob\":7,\"lastCarbs\":{\"_id\":\"5cd74c26156712edb4b32455\",\"enteredBy\":\"Martin\",\"eventType\":\"Carb Correction\",\"reason\":\"\",\"carbs\":7,\"duration\":0,\"created_at\":\"2019-05-11T22:24:00.000Z\",\"mills\":1557613440000,\"mgdl\":67}},\"display\":0,\"displayLine\":\"COB: 0g\"},\"delta\":{\"absolute\":-4,\"elapsedMins\":4.999483333333333,\"interpolated\":false,\"mean5MinsAgo\":69,\"mgdl\":-4,\"scaled\":-0.2,\"display\":\"-0.2\",\"previous\":{\"mean\":69,\"last\":69,\"mills\":1557613221946,\"sgvs\":[{\"mgdl\":69,\"mills\":1557613221946,\"device\":\"MIAOMIAO\",\"direction\":\"Flat\",\"filtered\":92588,\"unfiltered\":92588,\"noise\":1,\"rssi\":100}]}}}";
        String propjson = http.getString();
        Serial.printf("GET() properties response JSON length = %d\r\n", propjson.length());
        // remove any non text characters (just for sure)
        for(int i=0; i<propjson.length(); i++) {
          // Serial.print(propjson.charAt(i), DEC); Serial.print(" = "); Serial.println(propjson.charAt(i));
          if(propjson.charAt(i)<32 /* || propjson.charAt(i)=='\\' */) {
            propjson.setCharAt(i, 32);
          }
        }
        // propjson.replace("\\n"," ");
        // invalid Unicode character defined by Ascensia Diabetes Care Bluetooth Glucose Meter
        // ArduinoJSON does not accept any unicode surrogate pairs like \u0032 or \u0000
        propjson.replace("\\u0000"," ");
        propjson.replace("\\u000b"," ");
        propjson.replace("\\u0032"," ");
        DeserializationError propJSONerr = deserializeJson(JSONdoc, propjson);
        if(propJSONerr) {
          err=1003; // "JSON2 parsing failed"
          Serial.println("JSON parsing failed");
          addErrorLog(err);
        } else {
          Serial.println("Deserialized the second JSON and OK");
          JsonObject iob = JSONdoc["iob"];
          ns->iob = iob["iob"]; // 0
          strncpy(ns->iob_display, iob["display"] | "N/A", 16); // 0
          strncpy(ns->iob_displayLine, iob["displayLine"] | "IOB: N/A", 16); // "IOB: 0U"
          // Serial.println("IOB OK");
          
          JsonObject cob = JSONdoc["cob"];
          ns->cob = cob["cob"]; // 0
          strncpy(ns->cob_display, cob["display"] | "N/A", 16); // 0
          strncpy(ns->cob_displayLine, cob["displayLine"] | "COB: N/A", 16); // "COB: 0g"
          // Serial.println("COB OK");
          
          JsonObject delta = JSONdoc["delta"];
          ns->delta_absolute = delta["absolute"]; // -4
          ns->delta_elapsedMins = delta["elapsedMins"]; // 4.999483333333333
          ns->delta_interpolated = delta["interpolated"]; // false
          ns->delta_mean5MinsAgo = delta["mean5MinsAgo"]; // 69
          ns->delta_mgdl = delta["mgdl"]; // -4
          ns->delta_scaled = ns->delta_mgdl/18.0;
            if(cfg.show_mgdl) {
              sprintf(ns->delta_display, "%+d", ns->delta_mgdl);
            } else {
              sprintf(ns->delta_display, "%+.1f", ns->delta_scaled);
            }
            
          // Serial.println("DELTA OK");
          
          JsonObject loop_obj;
          JsonObject loop_display;
          if(cfg.info_line==3) {
            loop_obj = JSONdoc["openaps"];
            loop_display = loop_obj["status"];
          } else {
            loop_obj = JSONdoc["loop"];
            loop_display = loop_obj["display"];
          }
          strncpy(tmpstr, loop_display["symbol"] | "?", 4); // "⌁"
          ns->loop_display_symbol = tmpstr[0];
          strncpy(ns->loop_display_code, loop_display["code"] | "N/A", 16); // "enacted"
          strncpy(ns->loop_display_label, loop_display["label"] | "N/A", 16); // "Enacted"
          // strncpy(ns->loop_display_label, "Error", 16); // "Enacted"
          // Serial.println("LOOP OK");

          JsonObject basal = JSONdoc["basal"];
          strncpy(ns->basal_display, basal["display"] | "N/A", 16); // "T: 0.950U"      
          // Serial.println("BASAL OK");
          
          JsonObject basal_current_doc = JSONdoc["basal"]["current"];
          ns->basal_current = basal_current_doc["basal"]; // 0.1
          ns->basal_tempbasal = basal_current_doc["tempbasal"]; // 0.1
          ns->basal_combobolusbasal = basal_current_doc["combobolusbasal"]; // 0
          ns->basal_totalbasal = basal_current_doc["totalbasal"]; // 0.1
          // Serial.println("LOOP OK");
        } 
      } else {
        addErrorLog(httpCode);
        err=httpCode;
      }
    } else {
      Serial.printf("Returning with error %d after %lu ms :-(\r\n", httpCode, timeInGET);
      Serial.println(http.errorToString(httpCode));
      addErrorLog(httpCode);
      err=httpCode;
    }
    http.end();
  } else {
    // WiFi not connected
    ESP.restart();
  }

  M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);

  return err;
}

void drawBatteryStatus(int16_t x, int16_t y) {
  int8_t battLevel = getBatteryLevel();
  pixels.show();
  // Serial.print("Battery level: "); Serial.println(battLevel);
  M5.Lcd.fillRect(x, y, 16, 17, TFT_BLACK);
  if(battLevel!=-1) {
    switch(battLevel) {
      case 0:
        drawIcon(x, y+1, (uint8_t*)bat0_icon16x16, TFT_RED);
        break;
      case 25:
        drawIcon(x, y+1, (uint8_t*)bat1_icon16x16, TFT_YELLOW);
        break;
      case 50:
        drawIcon(x, y+1, (uint8_t*)bat2_icon16x16, TFT_WHITE);
        break;
      case 75:
        drawIcon(x, y+1, (uint8_t*)bat3_icon16x16, TFT_LIGHTGREY);
        break;
      case 100:
        drawIcon(x, y+0, (uint8_t*)plug_icon16x16, TFT_LIGHTGREY);
        break;
    }
  }
}

void handleAlarmsInfoLine(struct NSinfo *ns) {
  struct tm timeinfo;

  // calculate sensor time difference
  // calculate last alarm time difference
  int sensorDifSec=24*60*60; // too much
  int alarmDifSec=24*60*60; // too much
  int snoozeRemaining = 0;
  bool timeOK = getLocalTime(&timeinfo);
  if(timeOK){
    sensorDifSec=difftime(mktime(&timeinfo), ns->sensTime);
    alarmDifSec=difftime(mktime(&timeinfo), lastAlarmTime);
    if(timeOK) {
      snoozeRemaining = difftime(snoozeUntil, mktime(&timeinfo));
      if(snoozeRemaining < 0)
        snoozeRemaining = 0;
    }
  }
  unsigned int sensorDifMin = (sensorDifSec+30)/60;
  
  Serial.print("Alarm time difference = "); Serial.print(alarmDifSec); Serial.println(" sec");
  Serial.print("Snooze time remaining = "); Serial.print(snoozeRemaining); Serial.print(" sec, Snooze until "); Serial.println(snoozeUntil);
  char tmpStr[10];
  // Bottom info row: anchor to the screen bottom (BL/bottom-left). M5GFX places
  // free-font text lower than the old TFT_eSPI (it reserves the full-font ascent+
  // descender), so TL_DATUM at y=220 pushed the baseline past the 240 px edge onto
  // the button area. Bottom-anchoring at y=240 keeps the whole row on screen.
  M5.Lcd.setTextDatum(BL_DATUM);
  if( snoozeRemaining>0 ) {
    sprintf(tmpStr, "%i", (snoozeRemaining+59)/60);
    if(dispPage<maxPage)
      drawIcon(icon_xpos[1], icon_ypos[1], (uint8_t*)clock_icon16x16, TFT_RED);
  } else {
    strcpy(tmpStr, "Snooze");
    if(dispPage<maxPage)
      M5.Lcd.fillRect(icon_xpos[1], icon_ypos[1], 16, 16, BLACK);
  }
  M5.Lcd.setTextSize(1);
  M5.Lcd.setFreeFont(FSSB12);
  // prapare intensity variables for LED strip brightness
  int maxint = 255;
  maxint *= cfg.LED_strip_brightness;
  maxint /= 100;
  int lessint = 192;
  lessint *= cfg.LED_strip_brightness;
  lessint /= 100;
  // Serial.print("sensSgv="); Serial.print(sensSgv); Serial.print(", cfg.snd_alarm="); Serial.println(cfg.snd_alarm); 
  if((ns->sensSgv<=cfg.snd_alarm) && (ns->sensSgv>=0.1)) {
    // red alarm state
    // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
    Serial.println("ALARM LOW");
    M5.Lcd.fillRect(0, 220, 320, 20, TFT_RED);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_RED);
    int stw=M5.Lcd.textWidth(tmpStr);
    M5.Lcd.drawString(tmpStr, 159-stw/2, 240);
    if( (alarmDifSec>cfg.alarm_repeat*60) && (snoozeRemaining<=0) ) {
        sndAlarm();
        lastAlarmTime = mktime(&timeinfo);
    }
    if(cfg.LED_strip_mode>=2) {
      pixels.fill(pixels.Color(maxint, 0, 0));
      pixels.show();
    } else {
      if(cfg.LED_strip_mode==1) {
        pixels.clear();
        pixels.show();
      }
    }     
  } else {
    if((ns->sensSgv<=cfg.snd_warning) && (ns->sensSgv>=0.1)) {
      // yellow warning state
      // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
      Serial.println("WARNING LOW");
      M5.Lcd.fillRect(0, 220, 320, 20, TFT_YELLOW);
      M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
      int stw=M5.Lcd.textWidth(tmpStr);
      M5.Lcd.drawString(tmpStr, 159-stw/2, 240);
      if( (alarmDifSec>cfg.alarm_repeat*60) && (snoozeRemaining<=0) ) {
        sndWarning();
        lastAlarmTime = mktime(&timeinfo);
      }
      if(cfg.LED_strip_mode>=2) {
        pixels.fill(pixels.Color(maxint, lessint, 0));
        pixels.show();
      } else {
        if(cfg.LED_strip_mode==1) {
          pixels.clear();
          pixels.show();
        }
      }     
    } else {
      if( ns->sensSgv>=cfg.snd_alarm_high ) {
        // red alarm state
        // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
        Serial.println("ALARM HIGH");
        M5.Lcd.fillRect(0, 220, 320, 20, TFT_RED);
        M5.Lcd.setTextColor(TFT_BLACK, TFT_RED);
        int stw=M5.Lcd.textWidth(tmpStr);
        M5.Lcd.drawString(tmpStr, 159-stw/2, 240);
        if( (alarmDifSec>cfg.alarm_repeat*60) && (snoozeRemaining<=0) ) {
          sndAlarm();
          lastAlarmTime = mktime(&timeinfo);
        }
        if(cfg.LED_strip_mode>=2) {
          pixels.fill(pixels.Color(maxint, 0, 0));
          pixels.show();
        } else {
          if(cfg.LED_strip_mode==1) {
            pixels.clear();
            pixels.show();
          }
        }     
      } else {
        if( ns->sensSgv>=cfg.snd_warning_high ) {
          // yellow warning state
          // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
          Serial.println("WARNING HIGH");
          M5.Lcd.fillRect(0, 220, 320, 20, TFT_YELLOW);
          M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
          int stw=M5.Lcd.textWidth(tmpStr);
          M5.Lcd.drawString(tmpStr, 159-stw/2, 240);
          if( (alarmDifSec>cfg.alarm_repeat*60) && (snoozeRemaining<=0) ) {
            sndWarning();
            lastAlarmTime = mktime(&timeinfo);
          }
          if(cfg.LED_strip_mode>=2) {
            pixels.fill(pixels.Color(maxint, lessint, 0));
            pixels.show();
          } else {
            if(cfg.LED_strip_mode==1) {
              pixels.clear();
              pixels.show();
            }
          }     
        } else {
          if( sensorDifMin>=cfg.snd_no_readings ) {
            // LONG TIME NO READINGS -> yellow warning state
            // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
            Serial.println("WARNING NO READINGS");
            M5.Lcd.fillRect(0, 220, 320, 20, TFT_YELLOW);
            M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
            int stw=M5.Lcd.textWidth(tmpStr);
            M5.Lcd.drawString(tmpStr, 159-stw/2, 240);
            if( (alarmDifSec>cfg.alarm_repeat*60) && (snoozeRemaining<=0) ) {
              sndWarning();
              lastAlarmTime = mktime(&timeinfo);
            }
            if(cfg.LED_strip_mode>=2) {
              pixels.fill(pixels.Color(maxint, lessint, 0));
              pixels.show();
            } else {
              if(cfg.LED_strip_mode==1) {
                pixels.clear();
                pixels.show();
              }
            }     
          } else {
            if( strstr(ns->loop_display_label,"Err" )>0 ) {
              // LOOP ERROR -> red alarm state
              // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
              Serial.println("LOOP ERROR");
              M5.Lcd.fillRect(0, 220, 320, 20, TFT_RED);
              M5.Lcd.setTextColor(TFT_BLACK, TFT_RED);
              int stw=M5.Lcd.textWidth(tmpStr);
              M5.Lcd.drawString(tmpStr, 159-stw/2, 240);
              M5.Lcd.drawString("LOOP", 2, 240);
              M5.Lcd.drawString("ERR", 267, 240);
              if( (alarmDifSec>cfg.alarm_repeat*60) && (snoozeRemaining<=0) ) {
                sndAlarm();
                lastAlarmTime = mktime(&timeinfo);
              }
              if(cfg.LED_strip_mode>=2) {
                pixels.fill(pixels.Color(maxint, 0, 0));
                pixels.show();
              } else {
                if(cfg.LED_strip_mode==1) {
                  pixels.clear();
                  pixels.show();
                }
              }     
            } else {
              // normal glycemia state
              M5.Lcd.fillRect(0, 220, 320, 20, TFT_BLACK);
              M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
              if(cfg.LED_strip_mode==3) {
                pixels.fill(pixels.Color(0, maxint, 0));
                pixels.show();
              } else {
                if(cfg.LED_strip_mode==1 || cfg.LED_strip_mode==2) {
                  pixels.clear();
                  pixels.show();
                }
              }
              // draw info line
              char infoStr[64];
              if(dispPage >= PAGE_ERRLOG) {
                // Log/config pages: loop/basal/sensor info is noise here (row was already
                // cleared above). On the config page, show UPDATE over the middle button
                // when otaCheckLatest() found a newer release - that's what BtnB now does
                // on this page instead of snoozing.
                if(dispPage == PAGE_WEBQR && otaUpdateAvailable()) {
                  M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
                  int stw = M5.Lcd.textWidth("UPDATE");
                  M5.Lcd.drawString("UPDATE", 159-stw/2, 240);
                }
              } else {
                switch( cfg.info_line ) {
                  case 0: // sensor information
                    strcpy(infoStr, ns->sensDev);
                    if(strcmp(infoStr,"MIAOMIAO")==0) {
                      if(ns->is_xDrip) {
                        strcpy(infoStr,"xDrip MiaoMiao + Libre");
                      } else {
                        strcpy(infoStr,"Spike MiaoMiao + Libre");
                      }
                    }
                    if(strcmp(infoStr,"Tomato")==0)
                      strcat(infoStr," MiaoMiao + Libre");
                    M5.Lcd.drawString(infoStr, 0, 240);
                    break;
                  case 1: // button function icons
                    // touch boards (Core2/CoreS3) centre icons over the 3 touch zones;
                    // physical-button boards (Basic/Fire) align them under the 3 buttons
                    if(M5.Touch.isEnabled()) {
                      drawIcon(45, 220, (uint8_t*)sun_icon16x16, TFT_LIGHTGREY);
                      drawIcon(150, 220, (uint8_t*)clock_icon16x16, TFT_LIGHTGREY);
                      // drawIcon(153, 220, (uint8_t*)timer_icon16x16, TFT_LIGHTGREY);
                      drawIcon(256, 220, (uint8_t*)door_icon16x16, TFT_LIGHTGREY);
                    } else {
                      drawIcon(58, 220, (uint8_t*)sun_icon16x16, TFT_LIGHTGREY);
                      drawIcon(153, 220, (uint8_t*)clock_icon16x16, TFT_LIGHTGREY);
                      // drawIcon(153, 220, (uint8_t*)timer_icon16x16, TFT_LIGHTGREY);
                      drawIcon(246, 220, (uint8_t*)door_icon16x16, TFT_LIGHTGREY);
                    }
                    break;
                  case 2: // loop + basal information
                  case 3: // openaps + basal information
                    strcpy(infoStr, "L: ");
                    strlcat(infoStr, ns->loop_display_label, 64);
                    M5.Lcd.drawString(infoStr, 0, 240);
                    strcpy(infoStr, "B: ");
                    strlcat(infoStr, ns->basal_display, 64);
                    M5.Lcd.drawString(infoStr, 160, 240);
                    break;
                }
              }
            }
          }
        }
      }
    }
  }

  if(cfg.micro_dot_pHAT != 0) {
    // update Micro Dot pHAT display
    if(cfg.show_mgdl==1) {
      sprintf(tmpStr, "%3.0f", ns->sensSgv*18);
      MD.writeDigit(1, tmpStr[0]);
      MD.writeDigit(2, tmpStr[1]);
      MD.writeDigit(3, tmpStr[2]);
      if(ns->delta_scaled*18>99) {
        MD.writeDigit(4, '+');
        MD.writeDigit(5, '9');
        MD.writeDigit(6, '9');
      } else {
        sprintf(tmpStr, "%+3.0f", ns->delta_scaled*18);
        MD.writeDigit(4, tmpStr[0]);
        MD.writeDigit(5, tmpStr[1]);
        MD.writeDigit(6, tmpStr[2]);
      }
    } else {
      sprintf(tmpStr, "%4.1f", ns->sensSgv);
      MD.writeDigit(1, tmpStr[0]);
      MD.writeDigit(2, tmpStr[1]);
      MD.writeDigit(3, tmpStr[3] | 0x80);
      if(ns->delta_scaled>9.9) {
        MD.writeDigit(4, '+');
        MD.writeDigit(5, '9');
        MD.writeDigit(6, '9' | 0x80);
      } else {
        sprintf(tmpStr, "%+4.1f", ns->delta_scaled);
        MD.writeDigit(4, tmpStr[0]);
        MD.writeDigit(5, tmpStr[1]);
        MD.writeDigit(6, tmpStr[3] | 0x80);
      }
    }
  }

  // update snooze on other M5Stacks in local network
  if(udpSendSnoozeRetries>0) {
    udpSendSnoozeRetries--;
    IPAddress broadcastIp = ~WiFi.subnetMask() | WiFi.gatewayIP(); // broadcast to local subnet
    Serial.print("Sending UDP with Snooze infobroadcast to: ");
    Serial.println(broadcastIp);
    udp.beginPacket(broadcastIp, udpPort); // udpAddress
    int urlCRC = calcCRC(cfg.url);
    unsigned long snzUntl = snoozeUntil;
    udp.printf("M5_Nightscout SNOOZE: USR=%d, SnoozeUntil=%lu", urlCRC, snzUntl);
    udp.write('\0');
    udp.endPacket();
  }
  M5.Lcd.setTextDatum(TL_DATUM); // restore default datum for the rest of the drawing code
}

void drawLogWarningIcon() {
  if(err_log_ptr>5)
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)warning_icon16x16, TFT_YELLOW);
  else
    if(err_log_ptr>0)
      drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)warning_icon16x16, TFT_LIGHTGREY);
    else
      M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
}

void drawSegment(int x, int y, int r1, int r2, float a, int col)
{
  a = (a / 57.2958) - 1.57; 
  float a1 = a-1.57,
      a2 = a+1.57,
      x1 = x + (cos(a1) * r1),
      y1 = y + (sin(a1) * r1),
      x2 = x + (cos(a2) * r1),
      y2 = y + (sin(a2) * r1),
      x3 = x + (cos(a) * r2),
      y3 = y + (sin(a) * r2);
      
  M5.Lcd.fillTriangle(x1,y1,x2,y2,x3,y3,col);
}

void draw_page() {
  char tmpstr[255];

  bool pageJustOpened = (dispPage != lastDrawnPage);
  lastDrawnPage = dispPage;

  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);

  switch(dispPage) {
    case 0: {
      // if there was an error, then clear whole screen, otherwise only graphic updated part
      // M5.Lcd.fillScreen(BLACK);
      // M5.Lcd.fillRect(230, 110, 90, 100, TFT_BLACK);
      
      // readNightscout(cfg.url, cfg.token, &ns);
  
      M5.Lcd.setFreeFont(FSSB12);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      // char dateStr[30];
      // sprintf(dateStr, "%d.%d.%04d", sensTm.tm_mday, sensTm.tm_mon+1, sensTm.tm_year+1900);
      // M5.Lcd.drawString(dateStr, 0, 48);
      // char timeStr[30];
      // sprintf(timeStr, "%02d:%02d:%02d", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_sec);
      // M5.Lcd.drawString(timeStr, 0, 72);
      char dateStr[16];
      char timeStr[16];
      char datetimeStr[32];
      struct tm timeinfo;
      if(cfg.show_current_time) {
        if(getLocalTime(&timeinfo)) {
          // sprintf(datetimeStr, "%02d:%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon+1);  
          switch(cfg.time_format) {
            case 1:
              // sprintf(datetimeStr, "%02d:%02d  %d/%d  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mon+1, timeinfo.tm_mday);
              strftime(timeStr, 15, "%I:%M%p", &timeinfo);
              timeStr[strlen(timeStr)-1] = 0;
              strcat(timeStr, " ");
              break;
            default:
              // sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mday, timeinfo.tm_mon+1);
              strftime(timeStr, 15, "%H:%M ", &timeinfo);
          }
          switch(cfg.date_format) {
            case 1:
              // sprintf(datetimeStr, "%02d:%02d  %d/%d  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mon+1, timeinfo.tm_mday);
              // timeinfo.tm_mday=25;
              strftime(dateStr, 15, "%m/%d  ", &timeinfo);
              break;
            default:
              // sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mday, timeinfo.tm_mon+1);
              // timeinfo.tm_mday=25;
              strftime(dateStr, 15, "%d.%m.  ", &timeinfo);
          }
          strcpy(datetimeStr, timeStr);
          strcat(datetimeStr, dateStr);
        } else {
          // strcpy(datetimeStr, "??:??:??");
          strcpy(datetimeStr, "??:??");
        }
      } else {
        // sprintf(datetimeStr, "%02d:%02d:%02d  %02d.%02d.  ", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_sec, sensTm.tm_mday, sensTm.tm_mon+1);
          switch(cfg.date_format) {
            case 1:
              sprintf(datetimeStr, "%02d:%02d  %02d/%02d.  ", ns.sensTm.tm_hour, ns.sensTm.tm_min, ns.sensTm.tm_mon+1, ns.sensTm.tm_mday);
              break;
            default:
              sprintf(datetimeStr, "%02d:%02d  %02d.%02d.  ", ns.sensTm.tm_hour, ns.sensTm.tm_min, ns.sensTm.tm_mday, ns.sensTm.tm_mon+1);
          }
      }
      M5.Lcd.drawString(datetimeStr, 0, 0);

      drawBatteryStatus(icon_xpos[2], icon_ypos[2]);

      if(err_log_ptr>0) {
        M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
        if(err_log_ptr>5)
          drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)warning_icon16x16, TFT_YELLOW);
        else
          drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)warning_icon16x16, TFT_LIGHTGREY);
      }
              
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.drawString(cfg.userName, 0, 24);
      
      if(cfg.show_COB_IOB) {
        M5.Lcd.setFreeFont(FSSB12);
        M5.Lcd.setTextSize(1);
        // show small delta right from name
        /*
        M5.Lcd.setFreeFont(FSSB12);
        M5.Lcd.setTextColor(WHITE, BLACK);
        M5.Lcd.setTextSize(1);
        M5.Lcd.fillRect(130,24,69,23,TFT_BLACK);
        M5.Lcd.drawString(ns.delta_display, 130, 24);
        */
        
        M5.Lcd.fillRect(0,48,199,47,TFT_BLACK);
        if(ns.iob>0)
          M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        else
          M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        // Serial.print("ns.iob_displayLine=\""); Serial.print(ns.iob_displayLine); Serial.println("\"");
        strncpy(tmpstr, ns.iob_displayLine, 16);
        if(strncmp(tmpstr, "IOB:", 4)==0) {
          strcpy(tmpstr,"I");
          strcat(tmpstr, &ns.iob_displayLine[3]);
        }
        M5.Lcd.drawString(tmpstr, 0, 48);
        if(ns.cob>0)
          M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        else
          M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        // Serial.print("ns.cob_displayLine=\""); Serial.print(ns.cob_displayLine); Serial.println("\"");
        strncpy(tmpstr, ns.cob_displayLine, 16);
        if(strncmp(tmpstr, "COB:", 4)==0) {
          strcpy(tmpstr,"C");
          strcat(tmpstr, &ns.cob_displayLine[3]);
        }
        M5.Lcd.drawString(tmpstr, 0, 72);

        // show BIG delta below the name
        M5.Lcd.setFreeFont(FSSB24);
        // strcpy(ns.delta_display, "+8.9");
        if(ns.delta_mgdl>7)
          M5.Lcd.setTextColor(TFT_WHITE, BLACK);
        else
          M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
        M5.Lcd.drawString(ns.delta_display, 103, 48);
        M5.Lcd.setFreeFont(FSSB12);

      } else {
        // show BIG delta below the name
        M5.Lcd.setFreeFont(FSSB24);
        M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
        M5.Lcd.setTextSize(1);
        M5.Lcd.fillRect(0,48+10,199,47,TFT_BLACK);
        M5.Lcd.drawString(ns.delta_display, 0, 48+10);
        M5.Lcd.setFreeFont(FSSB12);
      }

      // calculate sensor time difference
      int sensorDifSec=0;
      if(!getLocalTime(&timeinfo)){
        sensorDifSec=24*60*60; // too much
      } else {
        Serial.print("Local time: "); Serial.print(timeinfo.tm_hour); Serial.print(":"); Serial.print(timeinfo.tm_min); Serial.print(":"); Serial.print(timeinfo.tm_sec); Serial.print(" DST "); Serial.println(timeinfo.tm_isdst);
        sensorDifSec=difftime(mktime(&timeinfo), ns.sensTime);
      }
      Serial.print("Sensor time difference = "); Serial.print(sensorDifSec); Serial.println(" sec");
      unsigned int sensorDifMin = (sensorDifSec+30)/60;
      uint16_t tdColor = TFT_LIGHTGREY;
      if(sensorDifMin>5) {
        tdColor = TFT_WHITE;
        if(sensorDifMin>15) {
          tdColor = TFT_RED;
        }
      }

      M5.Lcd.fillRoundRect(200,0,120,90,15,tdColor);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSSB24);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setTextColor(TFT_BLACK, tdColor);
      if(sensorDifMin>99) {
        M5.Lcd.drawString("Err", 260, 32);
      } else {
        M5.Lcd.drawNumber(sensorDifMin, 260, 32);
      }
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSSB12);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setTextColor(TFT_BLACK, tdColor);
      M5.Lcd.drawString("min", 260, 70);
      
      uint16_t glColor = TFT_GREEN;
      if(ns.sensSgv<cfg.yellow_low || ns.sensSgv>cfg.yellow_high) {
        glColor=TFT_YELLOW; // warning is YELLOW
      }
      if(ns.sensSgv<cfg.red_low || ns.sensSgv>cfg.red_high) {
        glColor=TFT_RED; // alert is RED
      }
    
      sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
      Serial.println(tmpstr);
      
      M5.Lcd.fillRect(0, 110, 320, 114, TFT_BLACK);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextDatum(TL_DATUM);
      M5.Lcd.setTextColor(glColor, TFT_BLACK);
      char sensSgvStr[30];
      int smaller_font = 0;
      if( cfg.show_mgdl ) {
        sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
      } else {
        sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
        if( sensSgvStr[0]!=' ' )
          smaller_font = 1;
      }
      // Serial.print("SGV string length = "); Serial.print(strlen(sensSgvStr));
      // Serial.print(", smaller_font = "); Serial.println(smaller_font);
      if( smaller_font ) {
        M5.Lcd.setFreeFont(FSSB18);
        M5.Lcd.drawString(sensSgvStr, 0, 130);
      } else {
        M5.Lcd.setFreeFont(FSSB24);
        M5.Lcd.drawString(sensSgvStr, 0, 120);
      }
      int tw=M5.Lcd.textWidth(sensSgvStr);
      // int th=M5.Lcd.fontHeight(GFXFF);
      // Serial.print("textWidth="); Serial.println(tw);
      // Serial.print("textHeight="); Serial.println(th);
    
      if(ns.arrowAngle!=180)
        drawArrow(0+tw+25, 120+40, 10, ns.arrowAngle+85, 40, 40, glColor);
    
      /*
      // draw help lines
      for(int i=0; i<320; i+=40) {
        M5.Lcd.drawLine(i, 0, i, 240, TFT_DARKGREY);
      }
      for(int i=0; i<240; i+=30) {
        M5.Lcd.drawLine(0, i, 320, i, TFT_DARKGREY);
      }
      M5.Lcd.drawLine(0, 120, 320, 120, TFT_LIGHTGREY);
      M5.Lcd.drawLine(160, 0, 160, 240, TFT_LIGHTGREY);
      */

      drawMiniGraph(&ns);
      handleAlarmsInfoLine(&ns);
      drawLogWarningIcon();
    }
    break;
    
    case 1: {
      // readNightscout(cfg.url, cfg.token, &ns);
      
      uint16_t glColor = TFT_GREEN;
      if(ns.sensSgv<cfg.yellow_low || ns.sensSgv>cfg.yellow_high) {
        glColor=TFT_YELLOW; // warning is YELLOW
      }
      if(ns.sensSgv<cfg.red_low || ns.sensSgv>cfg.red_high) {
        glColor=TFT_RED; // alert is RED
      }
    
      sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
      Serial.println(tmpstr);
      
      M5.Lcd.fillRect(0, 40, 320, 180, TFT_BLACK);
      M5.Lcd.setTextSize(4);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setTextColor(glColor, TFT_BLACK);
      char sensSgvStr[30];
      // MC_DATUM centres M5GFX's full font cell (ascent+descender) on numYpos, but the
      // BG value is digits only (no descender), so the glyphs sit too high. Shift the
      // anchor down by descender*textSize/2 to visually centre the digits at y=120
      // (FSSB24@4: 12*4/2=24 -> 144 ; FSSB18@4: 8*4/2=16 -> 136), matching pre-M5GFX look.
      int numYpos = 144;
      if( cfg.show_mgdl ) {
        if(ns.sensSgvMgDl<100) {
          sprintf(sensSgvStr, "%2.0f", ns.sensSgvMgDl);
          M5.Lcd.setFreeFont(FSSB24);
        } else {
          sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
          M5.Lcd.setFreeFont(FSSB24);
        }
      } else {
        if(ns.sensSgv<10) {
          sprintf(sensSgvStr, "%3.1f", ns.sensSgv);
          M5.Lcd.setFreeFont(FSSB24);
        } else {
          sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
          M5.Lcd.setFreeFont(FSSB18);
          numYpos = 136;
        }
      }
      M5.Lcd.drawString(sensSgvStr, 160, numYpos);
    
      M5.Lcd.fillRect(0, 0, 320, 40, TFT_BLACK);
      M5.Lcd.setFreeFont(FSSB24);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextDatum(TL_DATUM);
      M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

      char datetimeStr[30];
      struct tm timeinfo;
      if(cfg.show_current_time) {
        if(getLocalTime(&timeinfo)) {
          sprintf(datetimeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);  
        } else {
          strcpy(datetimeStr, "??:??");
        }
      } else {
        sprintf(datetimeStr, "%02d:%02d", ns.sensTm.tm_hour, ns.sensTm.tm_min);
      }
      M5.Lcd.drawString(datetimeStr, 0, 0);
      
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.drawString(ns.delta_display, 180, 0);

      int ay=0;

      // for(ns.arrowAngle=-90; ns.arrowAngle<=90; ns.arrowAngle+=15)

      if(ns.arrowAngle>=45)
        ay=4;
      else
        if(ns.arrowAngle>-45)
          ay=18;
        else
          ay=30;
      
      if(ns.arrowAngle!=180)
        drawArrow(280, ay, 10, ns.arrowAngle+85, 28, 28, glColor);
 
      handleAlarmsInfoLine(&ns);
      drawBatteryStatus(icon_xpos[2], icon_ypos[2]);
      drawLogWarningIcon();
    }
    break;
   
    case 2: {
      // calculate SGV color
      uint16_t glColor = TFT_GREEN;
      if(ns.sensSgv<cfg.yellow_low || ns.sensSgv>cfg.yellow_high) {
        glColor=TFT_YELLOW; // warning is YELLOW
      }
      if(ns.sensSgv<cfg.red_low || ns.sensSgv>cfg.red_high) {
        glColor=TFT_RED; // alert is RED
      }

      // display SGV
      sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
      Serial.println(tmpstr);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextDatum(TL_DATUM);
      M5.Lcd.setTextColor(glColor, TFT_BLACK);
      char sensSgvStr[30];
      // int smaller_font = 0;
      if( cfg.show_mgdl ) {
        if(ns.sensSgvMgDl<100) {
          sprintf(sensSgvStr, "%2.0f", ns.sensSgvMgDl);
          M5.Lcd.setFreeFont(FSSB24);
        } else {
          sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
          M5.Lcd.setFreeFont(FSSB24);
        }
      } else {
        if(ns.sensSgv<10) {
          sprintf(sensSgvStr, "%3.1f", ns.sensSgv);
          M5.Lcd.setFreeFont(FSSB24);
        } else {
          sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
          M5.Lcd.setFreeFont(FSSB24); //18
        }
      }
      M5.Lcd.fillRect(0, 0, 100, 40, TFT_BLACK);
      M5.Lcd.drawString(sensSgvStr, 0, 0);

      // display DELTA
      M5.Lcd.setTextDatum(TR_DATUM);
      M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      M5.Lcd.fillRect(220, 0, 100, 40, TFT_BLACK);
      M5.Lcd.drawString(ns.delta_display, 319, 0);

      // get time - need update
      char datetimeStr[30];
      struct tm timeinfo;
      if(cfg.show_current_time) {
        if(getLocalTime(&timeinfo)) {
          sprintf(datetimeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);  
        } else {
          strcpy(datetimeStr, "??:??");
        }
      } else {
        sprintf(datetimeStr, "%02d:%02d", ns.sensTm.tm_hour, ns.sensTm.tm_min);
      }
          
      // calculate sensor time difference
      int sensorDifSec=0;
      if(!getLocalTime(&timeinfo)){
        sensorDifSec=24*60*60; // too much
      } else {
        Serial.print("Local time: "); Serial.print(timeinfo.tm_hour); Serial.print(":"); Serial.print(timeinfo.tm_min); Serial.print(":"); Serial.print(timeinfo.tm_sec); Serial.print(" DST "); Serial.println(timeinfo.tm_isdst);
        sensorDifSec=difftime(mktime(&timeinfo), ns.sensTime);
      }
      Serial.print("Sensor time difference = "); Serial.print(sensorDifSec); Serial.println(" sec");
      unsigned int sensorDifMin = (sensorDifSec+30)/60;
      uint16_t tdColor = TFT_LIGHTGREY;
      if(sensorDifMin>5) {
        tdColor = TFT_WHITE;
        if(sensorDifMin>15) {
          tdColor = TFT_RED;
        }
      }
      // display time since last valid data
      M5.Lcd.fillRoundRect(0, 44, 68, 22, 7, tdColor);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSS9);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setTextColor(TFT_BLACK, tdColor);
      if(sensorDifMin>99) {
        M5.Lcd.drawString("Err min", 34, 53);
      } else {
        M5.Lcd.drawString(String(sensorDifMin)+" min", 34, 53);
      }

      {
        // Optional external DHT12/SHT30 temp+humidity sensor (I2C on the M-Bus).
        // If absent, reads return error sentinels and nothing is drawn - safe on any board.
        // get temperature and humidity
        float tmprc=dht12.readTemperature(cfg.temperature_unit);
        float humid=dht12.readHumidity();
        if(tmprc==float(0.01) || tmprc==float(0.02) || tmprc==float(0.03)) { // dht12 error, lets try sht30
          if(sht30.get()==0){
            tmprc = sht30.cTemp;
            humid = sht30.humidity;
            switch(cfg.temperature_unit) {
              case 2: //K
                tmprc += 273.15;
                break;
              case 3: //F
                tmprc = tmprc * 1.8 + 32.0;
                break;
            }
          } else {
            tmprc = float(0.04);
            humid = float(0.04);
          }
        }
        // display temperature
        // Serial.print("tmprc="); Serial.println(tmprc);
        M5.Lcd.fillRect(0, 180, 88, 30, TFT_BLACK);
        if(tmprc!=float(0.01) && tmprc!=float(0.02) && tmprc!=float(0.03) && tmprc!=float(0.04)) { // not an error
          M5.Lcd.setTextDatum(BL_DATUM);
          M5.Lcd.setFreeFont(FSS12); // CF_RT24
          M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          String tmprcStr=String(tmprc, 1);
          int tw=M5.Lcd.textWidth(tmprcStr);
          M5.Lcd.drawString(tmprcStr, 7, 210);
          M5.Lcd.setFreeFont(FSS9);
          int ow=M5.Lcd.textWidth("o");
          M5.Lcd.drawString("o", 7+tw+2, 199);
          M5.Lcd.setFreeFont(FSS12);
          switch(cfg.temperature_unit) {
            case 1:
              M5.Lcd.drawString("C", 7+tw+ow+4, 210);
              break;
            case 2:
              M5.Lcd.drawString("K", 7+tw+ow+4, 210);
              break;
            case 3:
              M5.Lcd.drawString("F", 7+tw+ow+4, 210);
              break;
          }
        }
        
        // display humidity
        // Serial.print("humid="); Serial.println(humid);
        M5.Lcd.fillRect(250, 185, 70, 25, TFT_BLACK);
        if(humid!=float(0.01) && humid!=float(0.02) && humid!=float(0.03) && humid!=float(0.04)) { // not an error
          M5.Lcd.setTextDatum(BR_DATUM);
          M5.Lcd.setFreeFont(FSS12);
          M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          String humidStr=String(humid, 0);
          humidStr += "%";
          M5.Lcd.drawString(humidStr, 310, 210);
        }
      }

      // draw clock
      float sx = 0, sy = 1, mx = 1, my = 0, hx = -1, hy = 0;    // Saved H, M, S x & y multipliers
      float sdeg=0, mdeg=0, hdeg=0;
      uint16_t x0=0, x1=0, yy0=0, yy1=0;
    
      uint8_t hh=timeinfo.tm_hour, mm=timeinfo.tm_min, ss=timeinfo.tm_sec;  // Get current time

      // Draw clock face
      M5.Lcd.fillCircle(160, 110, 98, glColor);
      M5.Lcd.fillCircle(160, 110, 92, TFT_BLACK);
    
      // Draw 12 lines
      for(int i = 0; i<360; i+= 30) {
        sx = cos((i-90)*0.0174532925);
        sy = sin((i-90)*0.0174532925);
        x0 = sx*94+160;
        yy0 = sy*94+110;
        x1 = sx*80+160;
        yy1 = sy*80+110;
    
        M5.Lcd.drawLine(x0, yy0, x1, yy1, glColor);
      }
      
      // Draw 60 dots
      for(int i = 0; i<360; i+= 6) {
        sx = cos((i-90)*0.0174532925);
        sy = sin((i-90)*0.0174532925);
        x0 = sx*82+160;
        yy0 = sy*82+110;
        // Draw minute markers
        M5.Lcd.drawPixel(x0, yy0, TFT_WHITE);
        
        // Draw main quadrant dots
        if(i==0 || i==180) M5.Lcd.fillCircle(x0, yy0, 2, TFT_WHITE);
        if(i==90 || i==270) M5.Lcd.fillCircle(x0, yy0, 2, TFT_WHITE);
      }
    
      M5.Lcd.fillCircle(160, 110, 3, TFT_WHITE);

      // draw day
      M5.Lcd.drawRoundRect(182, 97, 36, 26, 7, TFT_LIGHTGREY);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      M5.Lcd.setFreeFont(FSSB9);
      M5.Lcd.drawString(String(timeinfo.tm_mday), 200, 108);
    
      // draw name
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setFreeFont(FSSB9);
      M5.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      M5.Lcd.drawString(cfg.userName, 160, 145);
  
      // Pre-compute hand degrees, x & y coords for a fast screen update
      sdeg = ss*6;                  // 0-59 -> 0-354
      mdeg = mm*6+sdeg*0.01666667;  // 0-59 -> 0-360 - includes seconds
      hdeg = hh*30+mdeg*0.0833333;  // 0-11 -> 0-360 - includes minutes and seconds
      hx = cos((hdeg-90)*0.0174532925);    
      hy = sin((hdeg-90)*0.0174532925);
      mx = cos((mdeg-90)*0.0174532925);    
      my = sin((mdeg-90)*0.0174532925);
      sx = cos((sdeg-90)*0.0174532925);    
      sy = sin((sdeg-90)*0.0174532925);

      if (ss==0 || initial) {
        initial = 0;
        // Erase hour and minute hand positions every minute
        M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_BLACK);
        M5.Lcd.drawLine(ohx+1, ohy, 161, 110, TFT_BLACK);
        M5.Lcd.drawLine(ohx-1, ohy, 159, 110, TFT_BLACK);
        M5.Lcd.drawLine(ohx, ohy-1, 160, 109, TFT_BLACK);
        M5.Lcd.drawLine(ohx, ohy+1, 160, 111, TFT_BLACK);
        ohx = hx*52+160;    
        ohy = hy*52+110;
        M5.Lcd.drawLine(omx, omy, 160, 110, TFT_BLACK);
        omx = mx*74+160;    
        omy = my*74+110;
      }
  
      // Redraw new hand positions, hour and minute hands not erased here to avoid flicker
      M5.Lcd.drawLine(osx, osy, 160, 110, TFT_BLACK);
      osx = sx*78+160;    
      osy = sy*78+110;
      M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_WHITE);
      M5.Lcd.drawLine(ohx+1, ohy, 161, 110, TFT_WHITE);
      M5.Lcd.drawLine(ohx-1, ohy, 159, 110, TFT_WHITE);
      M5.Lcd.drawLine(ohx, ohy-1, 160, 109, TFT_WHITE);
      M5.Lcd.drawLine(ohx, ohy+1, 160, 111, TFT_WHITE);
      M5.Lcd.drawLine(omx, omy, 160, 110, TFT_WHITE);
      M5.Lcd.drawLine(osx, osy, 160, 110, TFT_RED);
  
      M5.Lcd.fillCircle(160, 110, 3, TFT_RED);      

      // draw angle arrow  
      int ay=0;
      if(ns.arrowAngle>=45)
        ay=4;
      else
        if(ns.arrowAngle>-45)
          ay=18;
        else
          ay=30;
      M5.Lcd.fillRect(262, 80, 58, 60, TFT_BLACK);
      if(ns.arrowAngle!=180)
        drawArrow(280, ay+90, 10, ns.arrowAngle+85, 28, 28, glColor);
        
      handleAlarmsInfoLine(&ns);
      drawBatteryStatus(icon_xpos[2], icon_ypos[2]);
      drawLogWarningIcon();
    }
    break;
    
    case PAGE_ERRLOG: {
      // display error log
      char tmpStr[64];
      HTTPClient http;
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 18);
      M5.Lcd.setTextDatum(TL_DATUM);
      M5.Lcd.setFreeFont(FMB9);
      M5.Lcd.setTextSize(1); 
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.drawString("Date  Time  Error Log", 0, 0);
      // M5.Lcd.drawString("Error", 143, 0);
      M5.Lcd.setFreeFont(FM9);
      if(err_log_ptr==0) {
        M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        M5.Lcd.drawString("no errors in log", 0, 20);
      } else {
        // Each entry: timestamp at x=0, message starting at x=132 and word-wrapping
        // onto full-width continuation lines so nothing is clipped at the screen edge.
        int lineH = 18;
        int rightEdge = M5.Lcd.width() - 2;
        int y = 20;
        int lineBudget = 6; // message rows available before the "Total errors" line at row 6
        int linesUsed = 0;
        char msg[96];
        for(int i=0; i<err_log_ptr && linesUsed<lineBudget; i++) {
          M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
          sprintf(tmpStr, "%02d.%02d.%02d:%02d", err_log[i].err_time.tm_mday, err_log[i].err_time.tm_mon+1, err_log[i].err_time.tm_hour, err_log[i].err_time.tm_min);
          M5.Lcd.drawString(tmpStr, 0, y);
          if(err_log[i].err_code<0) {
            M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
            strlcpy(msg, http.errorToString(err_log[i].err_code).c_str(), sizeof(msg));
          } else {
            M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
            errorLogMessage(err_log[i].err_code, msg, sizeof(msg));
          }
          int used = drawWrappedString(msg, 132, 0, y, rightEdge, lineH, lineBudget-linesUsed);
          y += used*lineH;
          linesUsed += used;
        }
        M5.Lcd.setFreeFont(FMB9);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        sprintf(tmpStr, "Total errors %d", err_log_count);
        M5.Lcd.drawString(tmpStr, 0, 20+6*18);
      }
      sprintf(tmpStr, "Free Heap = %u", ESP.getFreeHeap());
      M5.Lcd.drawString(tmpStr, 0, 20+7*18);
      long n = millis() / 1000;
      int updays = n / (24 * 3600);
      int pom = n % (24 * 3600);
      int uphours = pom / 3600;
      pom %= 3600;
      int upminutes = pom / 60 ;
      pom %= 60;
      int upseconds = pom;
      sprintf(tmpStr, "Up time = %02dd %02d:%02d:%02d", updays, uphours, upminutes, upseconds);
      M5.Lcd.drawString(tmpStr, 0, 20+8*18);
      IPAddress ip = WiFi.localIP();
      if(mDNSactive)
        sprintf(tmpStr, "%u.%u.%u.%u=%s.local", ip[0], ip[1], ip[2], ip[3], cfg.deviceName);
      else
        sprintf(tmpStr, "IP Address: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      M5.Lcd.drawString(tmpStr, 0, 20+9*18);
      sprintf(tmpStr, "Version: %s", M5NSversion.c_str());
      M5.Lcd.drawString(tmpStr, 0, 20+10*18);
      handleAlarmsInfoLine(&ns);
      drawBatteryStatus(icon_xpos[2], icon_ypos[2]);
      drawLogWarningIcon();
    }
    break;

    case PAGE_WEBQR: {
      // QR code to the device's web config page - scan with a phone camera to open it directly
      char url[40];
      char hostLine[48];
      const char *msg = NULL;
      IPAddress ip = WiFi.localIP();

      if(cfg.disable_web_server)
        msg = "Web server off";
      else if((WiFi.status()!=WL_CONNECTED) || ((uint32_t)ip == 0))
        msg = "No WiFi";
      else {
        snprintf(url, sizeof(url), "http://%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        if(mDNSactive)
          snprintf(hostLine, sizeof(hostLine), "%u.%u.%u.%u=%s.local", ip[0], ip[1], ip[2], ip[3], cfg.deviceName);
        else
          strncpy(hostLine, url, sizeof(hostLine));
      }

      M5.Lcd.fillRect(0, 0, 264, 18, TFT_BLACK);
      M5.Lcd.setFreeFont(FMB9);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.drawString("Scan to open config", 0, 0);

      // second header row: hostname/IP shown above the QR too, not just below it
      M5.Lcd.fillRect(0, 18, 320, 18, TFT_BLACK);
      M5.Lcd.setFreeFont(FSS9);
      M5.Lcd.setTextDatum(TC_DATUM);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.drawString(msg ? "" : hostLine, 160, 18);

      // opaque white card is self-covering, so this redraws flicker-free every 15s
      // (border shrunk from 16px to 8px to make room for the row above)
      M5.Lcd.fillRect(80, 36, 160, 160, TFT_WHITE);
      M5.Lcd.fillRect(0, 196, 320, 20, TFT_BLACK);
      if(msg) {
        M5.Lcd.setTextDatum(MC_DATUM);
        M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
        M5.Lcd.drawString(msg, 160, 116);
      } else {
        M5.Lcd.qrcode(url, 88, 44, 144, 1);
        M5.Lcd.setTextDatum(TC_DATUM);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Lcd.drawString(url, 160, 196);
      }

      // Only check on page entry, not on the 15s periodic redraw - this is a blocking
      // GitHub fetch (up to ~5s). Page paints first, then the UPDATE label (if any)
      // appears once the fetch returns. otaCheckLatest() itself is a no-op without WiFi,
      // and is independent of the internal web server, so no need to gate on "msg" here.
      if(pageJustOpened)
        otaCheckLatest();

      handleAlarmsInfoLine(&ns);
      drawBatteryStatus(icon_xpos[2], icon_ypos[2]);
      drawLogWarningIcon();
    }
    break;
  }
}

// the setup routine runs once when M5Stack starts up
void setup() {
    // initialize the M5Stack object (M5Unified auto-detects Basic/Fire/Core2/CoreS3)
    auto M5cfg = M5.config();
    M5cfg.clear_display = true;
    M5cfg.internal_spk  = true;   // enable board speaker (DAC on Basic, I2S on Core2/CoreS3)
    M5.begin(M5cfg);

    // prevent button A "ghost" random presses on older versions
    Wire.begin();
    
    halSDBegin();
    // M5.Speaker.mute();

    // Lcd display
    lcdSetBrightness(100);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextSize(2);
    yield();

    Serial.printf("M5NightscoutMon starting on board type %d\r\n", (int)M5.getBoard());

    Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());

    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
      Serial.println("No SD card attached");
      M5.Lcd.println("No SD card attached");
    } else {
      Serial.print("SD Card Type: ");
      M5.Lcd.print("SD Card Type: ");
      if(cardType == CARD_MMC){
          Serial.println("MMC");
          M5.Lcd.println("MMC");
      } else if(cardType == CARD_SD){
          Serial.println("SDSC");
          M5.Lcd.println("SDSC");
      } else if(cardType == CARD_SDHC){
          Serial.println("SDHC");
          M5.Lcd.println("SDHC");
      } else {
          Serial.println("UNKNOWN");
          M5.Lcd.println("UNKNOWN");
      }
      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      Serial.printf("SD Card Size: %llu MB\r\n", cardSize);
      M5.Lcd.printf("SD Card Size: %llu MB\r\n", cardSize);
    }

    readConfiguration(iniFilename, &cfg);
    // strcpy(cfg.url, "user.herokuapp.com");
    // cfg.dev_mode = 0;

    // Vibration motor, LED strip and Micro Dot pHAT are optional external add-ons on
    // configurable pins - governed by M5NS.INI on every board rather than forced per-board.

    if(cfg.vibration_mode != 0) {
      ledcSetup(VIBchannel, VIBfreq, VIBresolution);
      ledcAttachPin(cfg.vibration_pin, VIBchannel);
    }

    if(cfg.micro_dot_pHAT != 0) {
      MD.begin();
      MD.writeString("*M5NS*");
    }

    pixels.begin();
    pixels.clear(); // clear M5Stack Fire internal LEDs
    pixels.show();
    if(cfg.LED_strip_mode != 0) { 
      pixels.setPin(cfg.LED_strip_pin);
      pixels.updateLength(cfg.LED_strip_count);
      pixels.clear();
      pixels.show();
    }
    
    if(cfg.invert_display != -1) {
      M5.Lcd.invertDisplay(cfg.invert_display);
      Serial.print("Calling M5.Lcd.invertDisplay("); Serial.print(cfg.invert_display); Serial.println(")");
    } else {
      Serial.println("No invert_display defined in INI.");
    }
    M5.Lcd.setRotation(cfg.display_rotation);
    
    lcdBrightness = cfg.brightness1;
    lcdSetBrightness(lcdBrightness);

    bool btnA_pressed = false;
    if(M5.Touch.isEnabled()) {
      // touch boards (Core2/CoreS3): show a CONFIG button and give a ~2s window to tap it
      M5.Lcd.fillRect(0, 200, 110, 40, TFT_LIGHTGREY);
      M5.Lcd.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
      M5.Lcd.drawString("CONFIG", 18, 212);
      int i=0;
      while((i<20) && !btnA_pressed) {
        M5.update();
        btnA_pressed = M5.BtnA.isPressed(); // BtnA maps to the bottom-left touch zone
        M5.Lcd.drawLine(12+i*4, 234, 12+i*4+3, 234, TFT_BLACK);
        M5.Lcd.drawLine(12+i*4, 235, 12+i*4+3, 235, TFT_BLACK);
        i++;
        delay(100);
      }
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Lcd.fillRect(0, 200, 110, 40, TFT_BLACK);
    } else {
      // physical-button boards (Basic/Fire): hold BtnA at boot
      M5.update();
      btnA_pressed = M5.BtnA.isPressed();
    }
    
    if (btnA_pressed) {
      cfg.is_task_bootstrapping = 1;
    }

    if (!cfg.is_task_bootstrapping) {
      startupLogo();
    }
    yield();

    // cfg.is_task_bootstrapping
    // M5.Button.BtnA.isPressed( )
    // preferences.getBool("NeedsBootstrap", false)
    // recall from memory
    preferences.begin("M5StackNS", false);
    // preferences.putBool("NeedsBootstrap", cfg.is_task_bootstrapping);
    // is_task_bootstrapping = preferences.getBool("NeedsBootstrap", false);
    is_task_bootstrapping = cfg.is_task_bootstrapping;
    if(preferences.getBool("SoftReset", false)) {
      // no startup sound after soft reset and remove the SoftReset key
      snoozeUntil=preferences.getLong64("SnoozeUntil", 0);
      preferences.remove("SoftReset");
      preferences.remove("SnoozeUntil");
    } else {
      // normal startup so decide by M5NS.INI if to play startup sound
      if(cfg.snd_warning_at_startup) {
        play_tone(3000, 100, cfg.warning_volume);
        delay(500);
      }
      if(cfg.snd_alarm_at_startup) {
        play_tone(660, 400, cfg.alarm_volume);
        delay(500);
      }
    }
    preferences.end();

    if(cfg.LED_strip_mode != 0) {
      int maxint = 255;
      maxint *= cfg.LED_strip_brightness;
      maxint /= 100;
      int lessint = 192;
      lessint *= cfg.LED_strip_brightness;
      lessint /= 100;
      if(cfg.LED_strip_count>2) {
        for(int i=0; i<cfg.LED_strip_count-3; i++) {
          pixels.setPixelColor(i, pixels.Color(maxint, 0, 0));   
          pixels.setPixelColor(i+1, pixels.Color(maxint, lessint, 0));   
          pixels.setPixelColor(i+2, pixels.Color(0, maxint, 0));
          pixels.setPixelColor(i+3, pixels.Color(0, 0, maxint));
          pixels.show();
          delay(1500/(cfg.LED_strip_count-3));
          pixels.setPixelColor(i, pixels.Color(0, 0, 0));
        }
      }
      pixels.fill(pixels.Color(maxint, lessint, 0));
      pixels.show();
      delay(300);
      pixels.clear();
      pixels.show();
    } else {
      if (!is_task_bootstrapping) {
        delay(1000);
      }
    }
    
    M5.Lcd.fillScreen(BLACK);
    lcdSetBrightness(lcdBrightness);

    wifi_connect();

    yield();

    // sclient.setCACert(rootCACertificate);
    // http.setReuse(false);
    
    if (!is_task_bootstrapping) {
      lcdSetBrightness(lcdBrightness);
      M5.Lcd.fillScreen(BLACK);
    }
    
    // fill dummy values to error log
    /*
    for(int i=0; i<10; i++) {
      getLocalTime(&err_log[i].err_time);
      err_log[i].err_code=404;
    }
    getLocalTime(&err_log[6].err_time);
    err_log[6].err_code=HTTPC_ERROR_CONNECTION_REFUSED;
    err_log_ptr=10;
    err_log_count=23;
    */

    // start MDNS service and the internal web server
    if (MDNS.begin(cfg.deviceName)) {
      Serial.println("MDNS responder started OK.");
      mDNSactive = true;
    } else {
      Serial.println("ERROR: Could not startMDNS responder.");
      mDNSactive = false;
    }
    // Bootstrap (SoftAP) mode ALWAYS serves the config page, even when the user disabled the
    // web server. Otherwise disabling it is a one-way door: the page you would use to re-enable
    // it is the page that just disappeared, and on a device with no SD card (no M5NS.INI to
    // hand-edit) the only way back would be a USB reflash. Holding BtnA at boot is therefore the
    // universal recovery path. This also fixes loop() calling w3srv.handleClient() while
    // bootstrapping against a server that was never begun.
    if(cfg.disable_web_server==0 || is_task_bootstrapping) {
      w3srv.on("/", handleRoot);
      w3srv.on("/fwcheck", handleFwCheck);
      w3srv.on("/update", handleUpdate);
      w3srv.on("/savecfg", handleSaveConfig);
      w3srv.on("/switch", handleSwitchConfig);
      w3srv.on("/edititem", handleEditConfigItem);
      w3srv.on("/getedititem", handleGetEditConfigItem);
      w3srv.on("/clearconfigflash", handleClearConfigFlash);
      w3srv.on("/inline", []() {
        w3srv.send(200, "text/plain", "this is inline and works as well");
      });
      w3srv.onNotFound(handleNotFound);
      w3srv.begin();
    }

    if (!is_task_bootstrapping) {
      udp.begin(WiFi.localIP(),udpPort);
    }
    
    // test file with time stamps
    // msCountLog = millis()-6000;

    dispPage = cfg.default_page;
    setPageIconPos(dispPage);

    // stat startup time
    msStart = millis();

    // update glycemia now
    msCount = msStart-16000;
}

// the loop routine runs over and over again forever
void loop() {
  if(!cfg.disable_web_server || is_task_bootstrapping) {
    dnsServer.processNextRequest();
    w3srv.handleClient();
  }
  delay(20); // was 10
  if (!is_task_bootstrapping) {
    buttons_test();

    // update glycemia every 15s, fetch new data and draw page
    struct tm timeinfo;
    int sensorDifSec=24*60*60; // too much
    bool timeOK = getLocalTime(&timeinfo);
    if(timeOK){
      sensorDifSec=difftime(mktime(&timeinfo), ns.sensTime);
    }
    // Serial.printf("sensorDifSec = %d\r\n", sensorDifSec);
    if(millis()-msCount>15000) {
      /* if(dispPage==2)
        M5.Lcd.drawLine(osx, osy, 160, 111, TFT_BLACK); // erase seconds hand while updating data
      */
      if((sensorDifSec>305) && (rcnt>3)) {
        rcnt = 0;
        readDataSource();
        if(rcnt==4) {
          M5.Lcd.fillScreen(BLACK);
          M5.Lcd.setTextColor(WHITE);
          M5.Lcd.setCursor(0, 140);
          M5.Lcd.setTextSize(2);
          M5.Lcd.println("HTTP 30x Redirect");
          M5.Lcd.println("Check you URL in config!");
          M5.Lcd.println("Redirecting...");
          // delay(500);
          // M5.Lcd.fillScreen(BLACK);
          return;
        }
      }
      rcnt++;
      draw_page();
      msCount = millis();  
      // Serial.print("msCount = "); Serial.println(msCount);
      Serial.print("cfg.LED_strip_mode = "); Serial.println(cfg.LED_strip_mode);
      if(cfg.LED_strip_mode != 0) { 
        Serial.println("SHOW");
        pixels.show();
      }
    } else {
      // error handling, update clocks
      if((cfg.restart_at_logged_errors>0) && (err_log_count>=cfg.restart_at_logged_errors)) {
        Serial.println("Restarting on number of logged errors...");
        delay(500);
        preferences.begin("M5StackNS", false);
        preferences.putBool("SoftReset", true);
        preferences.putLong64("SnoozeUntil", snoozeUntil);
        preferences.end();
        ESP.restart();
      }
      char lastResetTime[10];
      strcpy(lastResetTime, "Unknown");
      if(getLocalTime(&localTimeInfo)) {
        sprintf(localTimeStr, "%02d:%02d", localTimeInfo.tm_hour, localTimeInfo.tm_min);
        // no soft restart less than a minute from last restart to prevent several restarts in the same minute
        if((millis()-msStart>60000) && (strcmp(cfg.restart_at_time, localTimeStr)==0)) {
          Serial.println("Restarting on preset time...");
          delay(500);
          preferences.begin("M5StackNS", false);
          preferences.putBool("SoftReset", true);
          preferences.putLong64("SnoozeUntil", snoozeUntil);
          preferences.end();
          ESP.restart();
        }
      }
      if((dispPage==0) && cfg.show_current_time) {
        // update current time on display
        M5.Lcd.setFreeFont(FSSB12);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        if(getLocalTime(&localTimeInfo)) {
            char timeStr[16];
            char dateStr[16];
            switch(cfg.time_format) {
              case 1:
                // sprintf(datetimeStr, "%02d:%02d  %d/%d  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mon+1, timeinfo.tm_mday);
                strftime(timeStr, 15, "%I:%M%p", &localTimeInfo);
                timeStr[strlen(timeStr)-1] = 0;
                strcat(timeStr, " ");
                break;
              default:
                // sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mday, timeinfo.tm_mon+1);
                strftime(timeStr, 15, "%H:%M ", &localTimeInfo);
            }
            switch(cfg.date_format) {
              case 1:
                // sprintf(datetimeStr, "%02d:%02d  %d/%d  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mon+1, timeinfo.tm_mday);
               // localTimeInfo.tm_mday=25;
               strftime(dateStr, 15, "%m/%d  ", &localTimeInfo);
                break;
              default:
                // localTimeInfo.tm_mday=25;
                // sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mday, timeinfo.tm_mon+1);
                strftime(dateStr, 15, "%d.%m.  ", &localTimeInfo);
            }
            strcpy(localTimeStr, timeStr);
            strcat(localTimeStr, dateStr);
        } else {
          strcpy(localTimeStr, "??:??");
          lastMin = 61;
        }
        if(lastMin!=localTimeInfo.tm_min) {
          lastSec=localTimeInfo.tm_sec;
          lastMin=localTimeInfo.tm_min;
          M5.Lcd.drawString(localTimeStr, 0, 0);
        }
      }
      if(dispPage==2) {
        // update analog clock
        if(getLocalTime(&localTimeInfo)) {
          // sprintf(localTimeStr, "%02d:%02d:%02d", localTimeInfo.tm_hour, localTimeInfo.tm_min, localTimeInfo.tm_sec);
        } else {
          lastMin = 61;
          lastSec = 61;
        }
        if(lastMin!=localTimeInfo.tm_min || lastSec!=localTimeInfo.tm_sec) {
          lastSec=localTimeInfo.tm_sec;
          lastMin=localTimeInfo.tm_min;
          
          float sx = 0, sy = 1, mx = 1, my = 0, hx = -1, hy = 0;    // Saved H, M, S x & y multipliers
          float sdeg=0, mdeg=0, hdeg=0;
        
          uint8_t hh=localTimeInfo.tm_hour, mm=localTimeInfo.tm_min, ss=localTimeInfo.tm_sec;  // Get current time
          
          // Pre-compute hand degrees, x & y coords for a fast screen update
          sdeg = ss*6;                  // 0-59 -> 0-354
          mdeg = mm*6+sdeg*0.01666667;  // 0-59 -> 0-360 - includes seconds
          hdeg = hh*30+mdeg*0.0833333;  // 0-11 -> 0-360 - includes minutes and seconds
          hx = cos((hdeg-90)*0.0174532925);    
          hy = sin((hdeg-90)*0.0174532925);
          mx = cos((mdeg-90)*0.0174532925);    
          my = sin((mdeg-90)*0.0174532925);
          sx = cos((sdeg-90)*0.0174532925);    
          sy = sin((sdeg-90)*0.0174532925);
    
          if (ss==0 || initial) {
            initial = 0;
            // Erase hour and minute hand positions every minute
            M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_BLACK);
            M5.Lcd.drawLine(ohx+1, ohy, 161, 110, TFT_BLACK);
            M5.Lcd.drawLine(ohx-1, ohy, 159, 110, TFT_BLACK);
            M5.Lcd.drawLine(ohx, ohy-1, 160, 109, TFT_BLACK);
            M5.Lcd.drawLine(ohx, ohy+1, 160, 111, TFT_BLACK);
            ohx = hx*52+160;    
            ohy = hy*52+110;
            M5.Lcd.drawLine(omx, omy, 160, 110, TFT_BLACK);
            omx = mx*74+160;    
            omy = my*74+110;
          }

          // erase old seconds hand position
          M5.Lcd.drawLine(osx, osy, 160, 110, TFT_BLACK);
      
          // draw day
          M5.Lcd.drawRoundRect(182, 97, 36, 26, 7, TFT_LIGHTGREY);
          M5.Lcd.setTextDatum(MC_DATUM);
          M5.Lcd.setFreeFont(FSSB9);
          M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          M5.Lcd.drawString(String(localTimeInfo.tm_mday), 200, 108);
       
          // draw name
          M5.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
          M5.Lcd.drawString(cfg.userName, 160, 145);
      
          // draw digital time
          // M5.Lcd.drawString(localTimeStr, 160, 75);
          
          // Redraw new hand positions, hour and minute hands not erased here to avoid flicker
          osx = sx*78+160;    
          osy = sy*78+110;
          // M5.Lcd.drawLine(osx, osy, 160, 110, TFT_RED);
          M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_WHITE);
          M5.Lcd.drawLine(ohx+1, ohy, 161, 110, TFT_WHITE);
          M5.Lcd.drawLine(ohx-1, ohy, 159, 110, TFT_WHITE);
          M5.Lcd.drawLine(ohx, ohy-1, 160, 109, TFT_WHITE);
          M5.Lcd.drawLine(ohx, ohy+1, 160, 111, TFT_WHITE);
          M5.Lcd.drawLine(omx, omy, 160, 110, TFT_WHITE);
          M5.Lcd.drawLine(osx, osy, 160, 110, TFT_RED);
      
          M5.Lcd.fillCircle(160, 110, 3, TFT_RED);
        }
        
      }
    }
  }

  /*
  if(millis()-msCountLog>5000) {
    File fileLog = SD.open("/logfile.txt", FILE_WRITE);    
    if(!fileLog) {
      Serial.println("Cannot write to logfile.txt");
    } else {
      int pos = fileLog.seek(fileLog.size());
      struct tm timeinfo;
      getLocalTime(&timeinfo);
      fileLog.println(asctime(&timeinfo));
      fileLog.close();
      Serial.print("Log file written: "); Serial.print(asctime(&timeinfo));
    }
    msCountLog = millis();  
  }  
  */

  // handle UDP task
  if (!is_task_bootstrapping) {
    // if there's data available, read a packet
    int packetSize = udp.parsePacket();
    if(packetSize)
    {
      Serial.print("Received UDP packet of size ");
      Serial.println(packetSize);
      // read the packet into packetBufffer
      udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
      if(packetSize<UDP_TX_PACKET_MAX_SIZE)
        packetBuffer[packetSize]=0;
      /*
      Serial.println("Contents:");
      Serial.println(packetBuffer);
      Serial.print("Remote IP: ");
      Serial.print(udp.remoteIP());
      Serial.print(":");
      Serial.println(udp.remotePort());
      Serial.print("Local IP: ");
      Serial.println(WiFi.localIP());
      */
     
      // send a reply, to the IP address and port that sent us the packet we received
      /*
      if((udp.remoteIP() != WiFi.localIP()) && (strncmp(packetBuffer, "Hello, M5NS here", 16)!=0)) {
        Serial.println("Sending hello");
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.print("Hello, M5NS here");
        udp.endPacket();
      }
      */
      if((WiFi.localIP()!=udp.remoteIP()) && (strncmp(packetBuffer, "M5_Nightscout SNOOZE: USR=", 26)==0)) {
        //Serial.println("UDP SNOOZE packet, lets check CRC and info");
        int urlCRC = calcCRC(cfg.url);
        int urlCRC_rcvd = 0;
        unsigned long snzUntl;
        int sr = sscanf(packetBuffer, "M5_Nightscout SNOOZE: USR=%d, SnoozeUntil=%lu", &urlCRC_rcvd, &snzUntl);
        //Serial.printf("scanf res = %d\r\n", sr);
        //Serial.printf("urlCRC = %d, urlCRC reveived = %d\r\n", urlCRC, urlCRC_rcvd);
        if((sr==2) && (urlCRC==urlCRC_rcvd)) {
          Serial.printf("Correct UDP SNOOZE received, we should SNOOZE until %lu\r\n", snzUntl);
          snoozeUntil = snzUntl;
          handleAlarmsInfoLine(&ns);
        }
      }
    }
  }
  
  // Serial.println("M5.update() and loop again");
  M5.update();
}
