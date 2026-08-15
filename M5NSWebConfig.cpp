#include "M5NSDevice.h"  // SD.h + M5Unified, or a non-M5 board shim (-DDEVICE_JC3248W535 / -DDEVICE_WS_TOUCH_LCD_35)

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "time.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include "Free_Fonts.h"
#include "IniFile.h"
#include "M5NSconfig.h"
#include "M5NSWebConfig.h"
#include "M5NSDexcom.h"
#include "M5NSLibre.h"
#include "externs.h"

// OTA firmware is served straight from this repo (raw.githubusercontent.com, master
// branch, Binaries/ folder). Variant selection mirrors the three images actually
// produced by Scripts/build.ps1 (see Binaries/firmware.json): Basic_4MB (min_spiffs
// partitions, 4MB flash), ESP32_16MB (default partitions, 16MB flash - covers Basic
// 16MB, Fire and all Core2), and CoreS3 (ESP32-S3). Selection is by chip + flash size,
// not board model, since a "basic-looking" board can have either partition layout.
static const char* updateVariantPath() {
#if defined(DEVICE_JC3248W535)
  // Must come before the generic S3 branch: a JC board pulling the CoreS3 image
  // would flash firmware for the wrong panel/pins.
  return "JC3248W535";
#elif defined(DEVICE_WS_TOUCH_LCD_35)
  // Classic-ESP32 16MB board: must come before the flash-size fallback, which would
  // otherwise route it to the ESP32_16MB (M5 panel/pins) image.
  return "WS_TouchLCD35";
#elif CONFIG_IDF_TARGET_ESP32S3
  return "CoreS3";
#else
  return (ESP.getFlashChipSize() >= 16 * 1024 * 1024) ? "ESP32_16MB" : "Basic_4MB";
#endif
}
static String updateURL(const char* file) {
  return String("https://raw.githubusercontent.com/psonnera/M5_NightscoutMon/master/Binaries/")
         + updateVariantPath() + "/" + file;
}

// ---- Shared page chrome + compact row renderers --------------------------------
// Settings are grouped into collapsible <details> sections (native HTML, no JS
// framework) and rendered via these row helpers instead of one-line-per-setting
// string concatenation, so the generated HTML stays small and reserve() up front
// avoids the repeated String reallocation the old ~230-concatenation build had.

// Set when an edited item needs a device restart to take effect (device name, time zone/DST,
// WiFi networks); read by handleRoot() to show a warning banner and by handleSaveConfig() to
// restart automatically after writing M5NS.INI. Lives only in RAM - a real restart clears it,
// and that's the only way it needs to be cleared.
static bool restartPending = false;

// Defined further down (shared by handleSaveConfig() and handleGetEditConfigItem()).
static void persistConfigToDisk();

static void pageHeadOpen(String& m, const char* extraMeta) {
  m += "<!DOCTYPE HTML>\r\n<html>\r\n<head>\r\n";
  m += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\r\n";
  if (extraMeta && extraMeta[0]) m += extraMeta;
  m += "<style>\r\n";
  m += "body{font-family:-apple-system,\"Segoe UI\",Roboto,Arial,sans-serif;font-size:15px;margin:0;padding:10px;max-width:480px}\r\n";
  m += "h1{font-size:19px;margin:4px 0 12px}\r\n";
  m += "details{border:1px solid #ccc;border-radius:6px;margin-bottom:6px;background:#fff}\r\n";
  m += "summary{font-weight:600;padding:10px;cursor:pointer}\r\n";
  m += ".row{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:6px;padding:7px 10px;border-top:1px solid #eee}\r\n";
  m += ".row>span:first-child{color:#555;margin-right:8px}\r\n";
  m += "select,input[type=text],input[type=password]{font-size:15px;padding:5px}\r\n";
  m += "a{color:#06c;text-decoration:none}\r\n";
  m += ".y{color:#BB9900}.r{color:#c00}.t{color:teal}\r\n";
  m += ".btn{display:inline-block;background:#4CAF50;color:#fff !important;padding:10px 18px;border-radius:4px;font-weight:600;margin:6px 0;border:none;font-size:15px}\r\n";
  m += ".btn2{display:inline-block;background:#999;color:#fff !important;padding:10px 18px;border-radius:4px;font-weight:600;margin:6px 0 6px 6px;border:none;font-size:15px}\r\n";
  m += ".sw{display:inline-block;min-width:40px;text-align:center;padding:5px 10px;border-radius:12px;background:#bbb;color:#fff !important;font-size:12px;font-weight:600}\r\n";
  m += ".sw.on{background:#4CAF50}\r\n";
  m += ".seg{display:inline-flex;border-radius:6px;overflow:hidden;border:1px solid #ccc}\r\n";
  m += ".seg a{padding:5px 10px;background:#f2f2f2;color:#333 !important;font-size:13px}\r\n";
  m += ".seg a.act{background:#4CAF50;color:#fff !important;font-weight:600}\r\n";
  m += ".warn{color:#c00}\r\n";
  m += ".note{color:#777;font-size:13px;margin-top:2px;flex-basis:100%}\r\n";
  m += "</style>\r\n";
  m += "<title>M5 Nightscout - "; m += cfg.userName; m += "</title>\r\n";
  m += "</head>\r\n<body>\r\n";
  m += "<h1>M5Stack Nightscout monitor for "; m += cfg.userName; m += "!</h1>\r\n";
}

static void detailsOpen(String& m, const char* id, const char* label, const String& sec) {
  m += "<details"; if (sec.equals(id)) m += " open"; m += "><summary>"; m += label; m += "</summary>\r\n";
}

static void rowText(String& m, const char* label, const String& value) {
  m += "<div class=\"row\"><span>"; m += label; m += "</span><span><b>"; m += value; m += "</b></span></div>\r\n";
}

static void rowEdit(String& m, const char* label, const String& value, const char* editParam, const char* sec, const char* cls = NULL) {
  m += "<div class=\"row\"><span>"; m += label; m += "</span><span>";
  m += "<b"; if (cls) { m += " class=\""; m += cls; m += "\""; } m += ">";
  m += value; m += "</b> <a href=\"edititem?param="; m += editParam; m += "&s="; m += sec; m += "\">edit</a></span></div>\r\n";
}

static void rowToggle(String& m, const char* label, bool value, const char* param, const char* sec, const char* onLabel = "YES", const char* offLabel = "NO") {
  m += "<div class=\"row\"><span>"; m += label; m += "</span><span><a class=\"sw";
  if (value) m += " on";
  m += "\" href=\"switch?param="; m += param; m += "&s="; m += sec; m += "\">";
  m += (value ? onLabel : offLabel); m += "</a></span></div>\r\n";
}

// Two-value choice rendered as two side-by-side buttons (the active one highlighted) instead
// of a single value+link - both options are visible so there's nothing to "cycle" through.
static void rowSeg(String& m, const char* label, bool isOn, const char* param, const char* sec, const char* onLabel, const char* offLabel) {
  m += "<div class=\"row\"><span>"; m += label; m += "</span><span class=\"seg\">";
  m += "<a"; if (isOn) m += " class=\"act\""; m += " href=\"switch?param="; m += param; m += "&v=1&s="; m += sec; m += "\">"; m += onLabel; m += "</a>";
  m += "<a"; if (!isOn) m += " class=\"act\""; m += " href=\"switch?param="; m += param; m += "&v=0&s="; m += sec; m += "\">"; m += offLabel; m += "</a>";
  m += "</span></div>\r\n";
}

static void rowSelect(String& m, const char* label, const char* param, const char* sec, const char* const opts[], const int vals[], int n, int current) {
  m += "<div class=\"row\"><span>"; m += label; m += "</span><span><select onchange=\"location='switch?param=";
  m += param; m += "&v='+this.value+'&s="; m += sec; m += "'\">\r\n";
  for (int i = 0; i < n; i++) {
    m += "<option value=\""; m += vals[i]; m += "\"";
    if (vals[i] == current) m += " selected";
    m += ">"; m += opts[i]; m += "</option>";
  }
  m += "</select></span></div>\r\n";
}

// Row for the /edititem forms: label + input on one line, optional note text wrapping to
// its own line below (via .note{flex-basis:100%}), optional color class on the label.
static void editRow(String& m, const char* label, const String& inputHtml, const char* note = NULL, const char* cls = NULL) {
  m += "<div class=\"row\"><span"; if (cls) { m += " class=\""; m += cls; m += "\""; } m += ">";
  m += label; m += "</span><span>"; m += inputHtml; m += "</span>";
  if (note) { m += "<span class=\"note\">"; m += note; m += "</span>"; }
  m += "</div>\r\n";
}

void handleRoot() {
  IPAddress ip = WiFi.localIP();
  char tmpStr[64];
  char timeStr[20];
  char dateStr[20];

  Serial.println("Serving root web page");

  // Which section (if any) to render open - the one just touched by a switch/edit, so a
  // change doesn't collapse the page back to Status-only. Defaults to Status.
  String sec = w3srv.hasArg("s") ? w3srv.arg("s") : String("st");

  char NSurl[128];
  if(strncmp(cfg.url, "http", 4))
    strcpy(NSurl,"https://");
  else
    strcpy(NSurl,"");
  strcat(NSurl,cfg.url);
  if ((cfg.token!=NULL) && (strlen(cfg.token)>0)) {
    if(strchr(NSurl,'?'))
      strcat(NSurl,"&token=");
    else
      strcat(NSurl,"?token=");
    strcat(NSurl,cfg.token);
  }

  String sgvUnits = cfg.show_mgdl?"mg/dL":"mmol/L";
  int decpl = cfg.show_mgdl?0:1;
  int mult = cfg.show_mgdl?18:1;

  String message;
  message.reserve(11264);
  pageHeadOpen(message, NULL);

  message += "<a class=\"btn\" href=\"/savecfg\">Save configuration to M5NS.INI</a>\r\n";
  if (restartPending) {
    message += "<p class=\"warn\">Changes pending that need a restart - the device will restart automatically when you save.</p>\r\n";
  }

  // ---- Status ----
  detailsOpen(message, "st", "Status", sec);
  if ( WiFi.status() == WL_CONNECTED) {
    rowText(message, "WiFi SSID", WiFi.SSID());
    if(mDNSactive) {
      sprintf(tmpStr, "%s.local", cfg.deviceName);
      rowText(message, "mDNS name", tmpStr);
    }
    sprintf(tmpStr, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    rowText(message, "IP address", tmpStr);
    byte mac[6];
    WiFi.macAddress(mac);
    String macStr = String(mac[5],HEX) + ":" + String(mac[4],HEX) + ":" + String(mac[3],HEX) + ":" + String(mac[2],HEX) + ":" + String(mac[1],HEX) + ":" + String(mac[0],HEX);
    rowText(message, "MAC address", macStr);
  }
  sprintf(tmpStr, "%d%%", getBatteryLevel());
  rowText(message, "Battery status", tmpStr);
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)) {
    switch(cfg.time_format) {
      case 1:
        strftime(timeStr, 19, "%I:%M:%S %p ", &timeinfo);
        break;
      default:
        strftime(timeStr, 19, "%H:%M:%S ", &timeinfo);
    }
    switch(cfg.date_format) {
      case 1:
        strftime(dateStr, 19, "%m/%d/%Y ", &timeinfo);
        break;
      default:
        strftime(dateStr, 19, "%e.%m.%Y  ", &timeinfo);
    }
    strcpy(tmpStr, dateStr);
    strcat(tmpStr, timeStr);
  } else {
    strcpy(tmpStr, "??:??");
  }
  rowText(message, "Current time", tmpStr);
  switch(cfg.time_format) {
    case 1:
      strftime(timeStr, 15, "%I:%M:%S %p ", &ns.sensTm);
      break;
    default:
      strftime(timeStr, 15, "%H:%M:%S ", &ns.sensTm);
  }
  switch(cfg.date_format) {
    case 1:
      strftime(dateStr, 15, "%m/%d/%Y ", &ns.sensTm);
      break;
    default:
      strftime(dateStr, 15, "%e.%m.%Y  ", &ns.sensTm);
  }
  strcpy(tmpStr, dateStr);
  strcat(tmpStr, timeStr);
  rowText(message, "Sensor time", tmpStr);
  if( cfg.show_mgdl ) {
    sprintf(tmpStr, "%3.0f mg/dL", ns.sensSgvMgDl);
  } else {
    sprintf(tmpStr, "%4.1f mmol/L", ns.sensSgv);
  }
  rowText(message, "Last sensor value", tmpStr);
  rowText(message, "Last sensor direction", ns.sensDir);
  message += "</details>\r\n";

  // ---- Data source ----
  // Exactly one source is ever active (see readDataSource() in the .ino) - only the
  // fields relevant to the selected source are shown here, the rest stay hidden.
  detailsOpen(message, "dx", "Data source", sec);
  {
    const char* dsOpts[3] = {"Nightscout", "Dexcom Share", "LibreLinkUp"};
    const int dsVals[3] = {0, 1, 2};
    rowSelect(message, "Source", "data_source", "dx", dsOpts, dsVals, 3, cfg.data_source);
  }
  message += "<div class=\"row\"><span class=\"note\">Changing the source takes effect after a restart - the device restarts automatically when you save.</span></div>\r\n";
  if (cfg.data_source == 1) {
    rowEdit(message, "Dexcom account", strlen(cfg.dexcom_user) > 0 ? String(cfg.dexcom_user) : String("(not set)"), "dexcom", "dx");
    {
      const char* srvOpts[3] = {"US", "Outside US", "Japan"};
      const int srvVals[3] = {0, 1, 2};
      rowSelect(message, "Dexcom server", "dexcom_server", "dx", srvOpts, srvVals, 3, cfg.dexcom_server);
    }
  } else if (cfg.data_source == 2) {
    rowEdit(message, "LibreLinkUp account", strlen(cfg.libre_user) > 0 ? String(cfg.libre_user) : String("(not set)"), "libre", "dx");
    {
      int srvVals[13];
      for (int i = 0; i < 13; i++) srvVals[i] = i;
      rowSelect(message, "LibreLinkUp region", "libre_server", "dx", libreRegionNames, srvVals, 13, cfg.libre_server);
    }
    message += "<div class=\"row\"><span class=\"note\">Leave at AUTO: the device logs in through LibreLinkUp's universal entry point and detects your region automatically.</span></div>\r\n";
  } else {
    rowEdit(message, "Nightscout URL", "<a href=\"" + String(NSurl) + "\">" + String(NSurl) + "</a>", "NSurl", "dx");
    rowToggle(message, "Filter only SGV values", cfg.sgv_only, "sgv_only", "dx");
  }
  message += "</details>\r\n";

  // ---- Display ----
  detailsOpen(message, "di", "Display", sec);
  rowEdit(message, "User name", cfg.userName, "userName", "di");
  rowSeg(message, "Display units", cfg.show_mgdl, "show_mgdl", "di", "mg/dL", "mmol/L");
  {
    char pageLabels[8][4];
    const char* pageOpts[8]; int pageVals[8];
    int n = 0;
    for (int p = 0; p <= maxPage && p < 8; p++) {
      snprintf(pageLabels[p], sizeof(pageLabels[p]), "%d", p);
      pageOpts[p] = pageLabels[p];
      pageVals[p] = p;
      n++;
    }
    rowSelect(message, "Default page", "default_page", "di", pageOpts, pageVals, n, cfg.default_page);
  }
  rowSeg(message, "Show time", cfg.show_current_time, "show_current_time", "di", "current time", "last data");
  rowToggle(message, "Show COB+IOB", cfg.show_COB_IOB, "show_COB_IOB", "di");
  rowSeg(message, "Time format", cfg.time_format, "time_format", "di", "AM/PM", "24h");
  rowSeg(message, "Date format", cfg.date_format, "date_format", "di", "MM/DD", "dd.mm.");
  {
    const char* rotOpts[4] = {"buttons down","buttons up","mirror buttons up","mirror buttons down"};
    const int rotVals[4] = {1,3,5,7};
    rowSelect(message, "Display rotation", "display_rotation", "di", rotOpts, rotVals, 4, cfg.display_rotation);
  }
  {
    const char* invOpts[3] = {"do not change","false","true"};
    const int invVals[3] = {-1,0,1};
    rowSelect(message, "Display inversion", "invert_display", "di", invOpts, invVals, 3, cfg.invert_display);
  }
  rowEdit(message, "Brightness steps", String(cfg.brightness1) + ", " + String(cfg.brightness2) + ", " + String(cfg.brightness3), "brightness", "di");
  {
    const char* lineOpts[4] = {"sensor info","button function icons","loop info + basal","openaps info + basal"};
    const int lineVals[4] = {0,1,2,3};
    rowSelect(message, "Status line", "info_line", "di", lineOpts, lineVals, 4, cfg.info_line);
  }
  {
    const char* tempOpts[3] = {"Celsius","Kelvin","Fahrenheit"};
    const int tempVals[3] = {1,2,3};
    rowSelect(message, "Temperature units", "temperature_unit", "di", tempOpts, tempVals, 3, cfg.temperature_unit);
  }
  message += "</details>\r\n";

  // ---- Colors & thresholds ----
  detailsOpen(message, "th", "Colors & thresholds", sec);
  rowEdit(message, "Yellow below", String(cfg.yellow_low*mult, decpl) + " " + sgvUnits, "dispColors", "th", "y");
  rowEdit(message, "Yellow above", String(cfg.yellow_high*mult, decpl) + " " + sgvUnits, "dispColors", "th", "y");
  rowEdit(message, "Red below", String(cfg.red_low*mult, decpl) + " " + sgvUnits, "dispColors", "th", "r");
  rowEdit(message, "Red above", String(cfg.red_high*mult, decpl) + " " + sgvUnits, "dispColors", "th", "r");
  message += "</details>\r\n";

  // ---- Alarms & sounds ----
  detailsOpen(message, "al", "Alarms & sounds", sec);
  rowEdit(message, "Snooze timeout", String(cfg.snooze_timeout) + " min", "alarmTiming", "al");
  rowEdit(message, "Repeat alarm every", String(cfg.alarm_repeat) + " min", "alarmTiming", "al");
  rowEdit(message, "Warning sound below", String(cfg.snd_warning*mult, decpl) + " " + sgvUnits, "sndAlarms", "al", "t");
  rowEdit(message, "Warning sound above", String(cfg.snd_warning_high*mult, decpl) + " " + sgvUnits, "sndAlarms", "al", "t");
  rowEdit(message, "Alarm sound below", String(cfg.snd_alarm*mult, decpl) + " " + sgvUnits, "sndAlarms", "al", "t");
  rowEdit(message, "Alarm sound above", String(cfg.snd_alarm_high*mult, decpl) + " " + sgvUnits, "sndAlarms", "al", "t");
  rowEdit(message, "No-readings warning", String(cfg.snd_no_readings) + " min", "sndAlarms", "al", "t");
  rowToggle(message, "Alarm on LOOP Error", cfg.snd_loop_error, "snd_loop_error", "al");
  rowToggle(message, "Test warning at startup", cfg.snd_warning_at_startup, "snd_warning_at_startup", "al");
  rowToggle(message, "Test alarm at startup", cfg.snd_alarm_at_startup, "snd_alarm_at_startup", "al");
  rowEdit(message, "Warning sound volume", String(cfg.warning_volume) + "%", "sndAlarms", "al", "t");
  rowEdit(message, "Alarm sound volume", String(cfg.alarm_volume) + "%", "sndAlarms", "al", "t");
  message += "</details>\r\n";

  // ---- WiFi & network ----
  detailsOpen(message, "wf", "WiFi & network", sec);
  rowEdit(message, "Device name", cfg.deviceName, "deviceName", "wf");
  rowSeg(message, "Internal Web Server", !cfg.disable_web_server, "disable_web_server", "wf", "Enabled", "Disabled");
  int wlans_defined_count = 0;
  for(int i=0; i<10; i++) {
    if(cfg.wlanssid[i][0] != 0) {
      wlans_defined_count++;
      String val = "SSID='" + String(cfg.wlanssid[i]) + "'";
      if(cfg.wlanpass[i][0]!=0) {
        String stars;
        for(int j=0; j<(int)strlen(cfg.wlanpass[i]); j++)
          stars += "*";
        val += ", PASS='" + stars + "'";
      } else {
        val += ", no password (open)";
      }
      char lbl[10]; snprintf(lbl, sizeof(lbl), "wlan%d", i);
      rowEdit(message, lbl, val, "wlans", "wf");
    }
  }
  if (wlans_defined_count < 1) {
    rowEdit(message, "WiFi Configuration", "(none)", "wlans", "wf");
  }
  message += "</details>\r\n";

  // ---- Hardware add-ons ----
  detailsOpen(message, "hw", "Hardware add-ons", sec);
  {
    const char* ledOpts[4] = {"OFF","visualize sound","show warnings and alarms","light always"};
    const int ledVals[4] = {0,1,2,3};
    rowSelect(message, "LED strip mode", "LED_strip_mode", "hw", ledOpts, ledVals, 4, cfg.LED_strip_mode);
  }
  rowEdit(message, "LED strip pin", String(cfg.LED_strip_pin), "led_strip", "hw");
  rowEdit(message, "LED strip count", String(cfg.LED_strip_count), "led_strip", "hw");
  rowEdit(message, "LED strip brightness", String(cfg.LED_strip_brightness) + "%", "led_strip", "hw");
  rowToggle(message, "Vibration motor unit", cfg.vibration_mode, "vibration_mode", "hw");
  rowEdit(message, "Vibration motor pin", String(cfg.vibration_pin), "vibration_motor", "hw");
  rowEdit(message, "Vibration motor strength", String(cfg.vibration_strength), "vibration_motor", "hw");
  rowToggle(message, "Micro Dot pHAT", cfg.micro_dot_pHAT, "micro_dot_pHAT", "hw");
  message += "</details>\r\n";

  // ---- System & firmware ----
  detailsOpen(message, "sy", "System & firmware", sec);
  rowEdit(message, "Time offset", String(cfg.timeZone) + " s", "timeZoneDST", "sy");
  rowEdit(message, "DST offset", String(cfg.dst) + " s", "timeZoneDST", "sy");
  rowEdit(message, "Restart at time", strcmp(cfg.restart_at_time,"NORES")==0?String("do NOT restart"):String(cfg.restart_at_time), "restartAt", "sy");
  rowEdit(message, "Restart after N errors", cfg.restart_at_logged_errors==0?String("do NOT restart"):String(cfg.restart_at_logged_errors), "restartAt", "sy");
  rowToggle(message, "Developer mode", cfg.dev_mode, "dev_mode", "sy", "Enabled", "Disabled");
  rowText(message, "Current firmware version", M5NSversion);
  message += "<div class=\"row\"><span></span><span><a href=\"/fwcheck\">Check for update</a></span></div>\r\n";
  message += "</details>\r\n";

  message += "</body>\r\n</html>\r\n";
  w3srv.send(200, "text/html", message);

  if(cfg.LED_strip_mode != 0) {
    pixels.show();
  }
}

// Firmware version/whatsnew check, split out of handleRoot() so the root page never
// blocks on a GitHub fetch (up to 2x5s timeout) - loads instantly, checked on demand.
void handleFwCheck() {
  String webVer;
  String whatsNew;
  bool fetchOk = false;

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure updateClient;
    updateClient.setInsecure();

    http.begin(updateClient, updateURL("update.inf"));
    http.setTimeout(5000);
    http.setConnectTimeout(5000);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      webVer = http.getString();
      fetchOk = true;
    } else {
      Serial.println("Error getting update.inf");
    }
    http.end();

    if (fetchOk) {
      http.begin(updateClient, updateURL("whatsnew.txt"));
      http.setTimeout(5000);
      http.setConnectTimeout(5000);
      httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        whatsNew = http.getString();
        whatsNew.replace("\r\n", "<br />\r\n");
      }
      http.end();
    }
  }

  String message;
  message.reserve(1536);
  pageHeadOpen(message, NULL);
  message += "<p><b>Application firmware</b><br />\r\n";
  message += "Current version: "; message += M5NSversion; message += "<br />\r\n";
  if (!fetchOk) {
    message += "Could not reach the update server. Check the WiFi connection and try again.<br />\r\n";
  } else {
    message += "Latest version: "; message += webVer;
    if (webVer > M5NSversion) {
      message += " <a href=\"/update\"><b>click to update</b></a>";
    }
    message += "<br />\r\n";
    if (whatsNew.length() > 0) {
      message += "<b>Last update information:</b><br />\r\n";
      message += whatsNew;
      message += "\r\n";
    }
  }
  message += "</p>\r\n<p><a href=\"/\">Back to configuration</a></p>\r\n";
  message += "</body>\r\n</html>\r\n";
  w3srv.send(200, "text/html", message);
}

// ---- On-device OTA (no browser needed) ------------------------------------------
// Used by the config page (page 4, M5_NightscoutMon.ino): draw_page() checks once on
// page entry and caches the result, since draw_page() also re-runs every 15s from
// loop() and can't afford a network fetch on every redraw. The middle button then
// runs the update using the same WiFiClientSecure + httpUpdate path as handleUpdate()'s
// web /update route below, just without any HTML.
static String otaCachedVer = "";
static bool   otaCacheValid = false;

bool otaCheckLatest() {
  otaCacheValid = false;
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, updateURL("update.inf"));
  http.setTimeout(5000);
  http.setConnectTimeout(5000);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    otaCachedVer = http.getString();
    otaCachedVer.trim();
    otaCacheValid = true;
  }
  http.end();
  return otaUpdateAvailable();
}

bool otaUpdateAvailable() {
  return otaCacheValid && (otaCachedVer > M5NSversion);
}

String otaLatestVersion() {
  return otaCachedVer;
}

void otaRunUpdate() {
  if (!otaUpdateAvailable()) return;

  Serial.print("Updating firmware, please wait ... ");
  M5.Display.setBrightness(255);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 18);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setFreeFont(FMB9);
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.println("FIRMWARE UPDATE");
  M5.Lcd.println();
  M5.Lcd.print("Current firmware: "); M5.Lcd.println(M5NSversion);
  M5.Lcd.print("New firmware: "); M5.Lcd.println(otaCachedVer);
  M5.Lcd.println();
  M5.Lcd.println("Updating the firmware... ");
  M5.Lcd.println();

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(false);
  t_httpUpdate_return ret = httpUpdate.update(client, updateURL("M5_NightscoutMon.ino.bin"));

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      M5.Lcd.setTextColor(RED);
      M5.Lcd.println("UPDATE FAILED");
      delay(3000);
      M5.Lcd.fillScreen(BLACK);
      draw_page();
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      M5.Lcd.setTextColor(YELLOW);
      M5.Lcd.println("NO UPDATES");
      delay(3000);
      M5.Lcd.fillScreen(BLACK);
      draw_page();
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK, restarting ...");
      M5.Lcd.setTextColor(GREEN);
      M5.Lcd.println("UPDATED SUCCESSFULLY");
      M5.Lcd.println("Restarting ...");
      M5.update();
      delay(1000);
      ESP.restart();
      break;
  }
}

void handleUpdate() {
  Serial.print("Updating firmware, please wait ... ");
  M5.Display.setBrightness(255);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 18);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setFreeFont(FMB9);
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.println("FIRMWARE UPDATE");
  M5.Lcd.println();
  Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());
  M5.Lcd.print("Free Heap: "); M5.Lcd.println(ESP.getFreeHeap());
  M5.Lcd.println();

  String message;
  message.reserve(1024);
  pageHeadOpen(message, "<meta http-equiv=\"refresh\" content=\"30;url=/\" />\r\n");

  String webVer;
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  if((WiFi.status() == WL_CONNECTED)) {
    http.begin(client, updateURL("update.inf"));
    int httpCode = http.GET();
    if(httpCode > 0) {
      if(httpCode == HTTP_CODE_OK) {
        webVer = http.getString();
      }
    }
  }

  M5.Lcd.print("Current firmware: "); M5.Lcd.println(M5NSversion);
  M5.Lcd.print("Found firmware:"); M5.Lcd.println(webVer);
  
  if(webVer > M5NSversion) {
    message += "<p>Updating firmware to version ";
    message += webVer;
    message += ", please wait ... </p>\r\n";
    message += "<p>Device will restart automatically.</p>\r\n";
    message += "</body>\r\n";
    message += "</html>\r\n";
    w3srv.send(200, "text/html", message);

    M5.Lcd.println();
    M5.Lcd.println("Updating the firmware... ");
    M5.Lcd.println();
    httpUpdate.rebootOnUpdate(false);
    t_httpUpdate_return ret = httpUpdate.update(client, updateURL("M5_NightscoutMon.ino.bin"));
    //t_httpUpdate_return ret = httpUpdate.update(client, "server", 80, "file.bin");

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("UPDATE FAILED");
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        M5.Lcd.setTextColor(YELLOW);
        M5.Lcd.println("NO UPDATES");
        break;

      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK, restarting ...");
        M5.Lcd.setTextColor(GREEN);
        M5.Lcd.println("UPDATED SUCCESSFULLY");
        M5.Lcd.println("Restarting ...");

        M5.update();
        delay(1000);
        ESP.restart();
        break;
    }    
  } else {
    message += "<p>Nothing to update. Firmware version ";
    message += webVer;
    message += " is current.</p>\r\n";
    message += "</body>\r\n";
    message += "</html>\r\n";
    w3srv.send(200, "text/html", message);

    Serial.println("Nothing to update");
    M5.Lcd.println();
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("NOTHING TO UPDATE");
  }
  if (!cfg.is_task_bootstrapping) {
    M5.update();
    delay(2000);
    M5.Lcd.fillScreen(BLACK);
    draw_page();
    if(cfg.LED_strip_mode != 0) { 
      pixels.show();
    }
  }
}

void handleSwitchConfig() {
  // Multi-value settings accept an explicit "v" (from the root page's <select> dropdowns
  // and [Seg] two-way buttons) and set that value directly; without "v" they fall back to
  // the old cycle-to-next behavior, so existing bookmarked/typed switch links keep working.
  // Yes/No settings always just toggle. "s" carries the section id back to handleRoot() so
  // only the section just touched stays expanded.
  bool haveVal = w3srv.hasArg("v");
  long val = haveVal ? String(w3srv.arg("v")).toInt() : 0;
  String sec = w3srv.hasArg("s") ? w3srv.arg("s") : String("st");

  // Turning the internal web server off kills this page mid-session: loop() stops calling
  // handleClient() as soon as the flag flips, so the redirect back to "/" never loads and the
  // Save button becomes unreachable. That means the change can never actually be persisted from
  // here (a restart restores it from M5NS.INI/flash) - but the page simply going dead looks like
  // a crash, so say what will happen first. "ok=1" is the confirmed request coming back.
  if (w3srv.hasArg("param") && String(w3srv.arg("param")).equals("disable_web_server")
      && !w3srv.hasArg("ok")) {
    bool wouldDisable = haveVal ? (val == 0) : (cfg.disable_web_server == 0);
    if (wouldDisable) {
      String message;
      message.reserve(1536);
      pageHeadOpen(message, NULL);
      message += "<h2 class=\"warn\">Disable the internal web server?</h2>\r\n";
      message += "<p>This page <b>is</b> the internal web server. It will stop responding "
                 "immediately - this is expected, not a crash.</p>\r\n";
      message += "<p>The change is <b>not saved</b>: you will not be able to reach the Save "
                 "button, so simply <b>restart the device</b> and the web server comes back.</p>\r\n";
      message += "<p>To turn it off permanently, set <code>disable_web_server = 1</code> in "
                 "<code>M5NS.INI</code> on the SD card. You can still get back in by restarting "
                 "while holding the left button (or tapping CONFIG on touch models) to enter "
                 "setup mode, which always serves this page.</p>\r\n";
      message += "<p><a class=\"btn2\" href=\"switch?param=disable_web_server&v=0&ok=1&s=";
      message += sec; message += "\">Disable anyway</a>\r\n";
      message += "<a class=\"btn\" href=\"/?s="; message += sec; message += "\">Keep it enabled</a></p>\r\n";
      message += "</body>\r\n</html>\r\n";
      w3srv.send(200, "text/html", message);
      return;
    }
  }

  for (uint8_t i = 0; i < w3srv.args(); i++) {
    if(String(w3srv.argName(i)).equals("param")) {
      String param = w3srv.arg(i);
      if(param.equals("data_source")) {
        int oldSource = cfg.data_source;
        if(haveVal && val>=0 && val<=2)
          cfg.data_source = val;
        else {
          cfg.data_source++;
          if(cfg.data_source > 2)
            cfg.data_source = 0;
        }
        // Only one source is ever active - switching takes effect after the restart
        // that Save triggers, same as device name / WiFi changes.
        if(cfg.data_source != oldSource) restartPending = true;
        dexcomResetSession();
        libreResetSession();
      }
      else if(param.equals("dexcom_server")) {
        if(haveVal && val>=0 && val<=2)
          cfg.dexcom_server = val;
        else {
          cfg.dexcom_server++;
          if(cfg.dexcom_server > 2)
            cfg.dexcom_server = 0;
        }
        dexcomResetSession();
      }
      else if(param.equals("libre_server")) {
        if(haveVal && val>=0 && val<=12)
          cfg.libre_server = val;
        else {
          cfg.libre_server++;
          if(cfg.libre_server > 12)
            cfg.libre_server = 0;
        }
        libreResetSession();
      }
      else if(param.equals("show_mgdl")) {
        if(haveVal) cfg.show_mgdl = (val!=0);
        else cfg.show_mgdl = !cfg.show_mgdl;
      }
      else if(param.equals("sgv_only")) {
        cfg.sgv_only = !cfg.sgv_only;
      }
      else if(param.equals("default_page")) {
        if(haveVal && val>=0 && val<=maxPage)
          cfg.default_page = val;
        else {
          cfg.default_page++;
          if(cfg.default_page>maxPage)
            cfg.default_page = 0;
        }
        dispPage = cfg.default_page;
        setPageIconPos(dispPage);
      }
      else if(param.equals("show_current_time")) {
        if(haveVal) cfg.show_current_time = (val!=0);
        else cfg.show_current_time = !cfg.show_current_time;
      }
      else if(param.equals("show_COB_IOB")) {
        cfg.show_COB_IOB = !cfg.show_COB_IOB;
      }
      else if(param.equals("snd_loop_error")) {
        cfg.snd_loop_error = !cfg.snd_loop_error;
      }
      else if(param.equals("snd_warning_at_startup")) {
        cfg.snd_warning_at_startup = !cfg.snd_warning_at_startup;
      }
      else if(param.equals("snd_alarm_at_startup")) {
        cfg.snd_alarm_at_startup = !cfg.snd_alarm_at_startup;
      }
      else if(param.equals("info_line")) {
        if(haveVal && val>=0 && val<=3)
          cfg.info_line = val;
        else {
          cfg.info_line++;
          if(cfg.info_line>3)
            cfg.info_line = 0;
        }
      }
      else if(param.equals("time_format")) {
        if(haveVal && val>=0 && val<=1)
          cfg.time_format = val;
        else {
          cfg.time_format++;
          if(cfg.time_format>1)
            cfg.time_format = 0;
        }
      }
      else if(param.equals("date_format")) {
        if(haveVal && val>=0 && val<=1)
          cfg.date_format = val;
        else {
          cfg.date_format++;
          if(cfg.date_format>1)
            cfg.date_format = 0;
        }
      }
      else if(param.equals("display_rotation")) {
        if(haveVal && (val==1 || val==3 || val==5 || val==7)) {
          cfg.display_rotation = val;
        } else if(cfg.display_rotation == 1) {
          cfg.display_rotation = 3;
        } else if(cfg.display_rotation == 3) {
          cfg.display_rotation = 5;
        } else if(cfg.display_rotation == 5) {
          cfg.display_rotation = 7;
        } else {
          cfg.display_rotation = 1;
        }
        M5.Lcd.setRotation(cfg.display_rotation);
      }
      else if(param.equals("invert_display")) {
        if(haveVal && val>=-1 && val<=1)
          cfg.invert_display = val;
        else {
          cfg.invert_display++;
          if(cfg.invert_display>1)
            cfg.invert_display = -1;
        }
        if(cfg.invert_display != -1)
          M5.Lcd.invertDisplay(cfg.invert_display);
      }
      else if(param.equals("temperature_unit")) {
        if(haveVal && val>=1 && val<=3)
          cfg.temperature_unit = val;
        else {
          cfg.temperature_unit++;
          if(cfg.temperature_unit>3)
            cfg.temperature_unit = 1;
        }
      }
      else if(param.equals("LED_strip_mode")) {
        if(haveVal && val>=0 && val<=3)
          cfg.LED_strip_mode = val;
        else {
          cfg.LED_strip_mode++;
          if(cfg.LED_strip_mode>3)
            cfg.LED_strip_mode = 0;
        }
        if(cfg.LED_strip_mode==0) {
          pixels.clear();
          pixels.show();
        }
      }
      else if(param.equals("vibration_mode")) {
        cfg.vibration_mode++;
        if(cfg.vibration_mode>1) {
          cfg.vibration_mode = 0;
        }
      }
      else if(param.equals("micro_dot_pHAT")) {
        cfg.micro_dot_pHAT++;
        if(cfg.micro_dot_pHAT>1) {
          cfg.micro_dot_pHAT = 0;
          MD.writeString("      ");
        } else {
          MD.begin();
          MD.writeString("*M5NS*");
        }
      }
      else if(param.equals("dev_mode")) {
        cfg.dev_mode = !cfg.dev_mode;
      }
      else if(param.equals("disable_web_server")) {
        // rowSeg's "v" is 1=Enabled/0=Disabled (the state shown to the user), which is the
        // inverse of disable_web_server itself.
        if(haveVal) cfg.disable_web_server = (val==0);
        else cfg.disable_web_server = !cfg.disable_web_server;
      }
    }
  }

  // Instant redirect instead of the old 1s meta-refresh page - dropdown/toggle changes
  // feel immediate and each switch no longer costs ~0.5KB of throwaway HTML. Carries the
  // section id back so only the section just changed stays expanded.
  w3srv.sendHeader("Location", "/?s=" + sec);
  w3srv.send(303, "text/plain", "");

  if (!cfg.is_task_bootstrapping) {
    M5.Lcd.fillScreen(BLACK);
    draw_page();
    if(cfg.LED_strip_mode != 0) {
      pixels.show();
    }
  }
}

void handleEditConfigItem() {
  String sgvUnits = cfg.show_mgdl?"mg/dL":"mmol/L";
  int decpl = cfg.show_mgdl?0:1;
  int mult = cfg.show_mgdl?18:1;
  String sec = w3srv.hasArg("s") ? w3srv.arg("s") : String("st");

  String message;
  message.reserve(2048);
  pageHeadOpen(message, NULL);
  message += "<p>Edit configuration item.</p>\r\n";
  message += "<form action=\"/getedititem\" method=\"post\">\r\n";
  message += "<input type=\"hidden\" name=\"s\" value=\""; message += sec; message += "\">\r\n";
  if(String(w3srv.argName(0)).equals("param")) {
    if(String(w3srv.arg(0)).equals("userName")) {
      editRow(message, "User name", "<input type=\"text\" name=\"" + String(w3srv.arg(0)) + "\" value=\"" + String(cfg.userName) + "\" size=\"20\" maxlength=\"32\">");
    }
    if(String(w3srv.arg(0)).equals("NSurl")) {
      editRow(message, "Nightscout URL", "<input type=\"text\" name=\"url\" value=\"" + String(cfg.url) + "\" size=\"28\" maxlength=\"128\">", "e.g. sitename.herokuapp.com");
      editRow(message, "Security token", "<input type=\"text\" name=\"token\" value=\"" + String(cfg.token) + "\" size=\"28\" maxlength=\"64\">", "empty if not used");
    }
    if(String(w3srv.arg(0)).equals("dexcom")) {
      editRow(message, "Dexcom account", "<input type=\"text\" name=\"dexcom_user\" value=\"" + String(cfg.dexcom_user) + "\" size=\"28\" maxlength=\"63\">", "use the account that SHARES glucose data, not a follower account");
      editRow(message, "Dexcom password", "<input type=\"password\" name=\"dexcom_pass\" value=\"" + String(cfg.dexcom_pass) + "\" size=\"28\" maxlength=\"63\">", "stored on the device SD card / flash in plain text");
    }
    if(String(w3srv.arg(0)).equals("libre")) {
      editRow(message, "LibreLinkUp email", "<input type=\"text\" name=\"libre_user\" value=\"" + String(cfg.libre_user) + "\" size=\"28\" maxlength=\"63\">", "use a LibreLinkUp follower account, not the LibreView account itself");
      editRow(message, "LibreLinkUp password", "<input type=\"password\" name=\"libre_pass\" value=\"" + String(cfg.libre_pass) + "\" size=\"28\" maxlength=\"63\">", "stored on the device SD card / flash in plain text");
    }
    if(String(w3srv.arg(0)).equals("deviceName")) {
      editRow(message, "Device name", "<input type=\"text\" name=\"deviceName\" value=\"" + String(cfg.deviceName) + "\" size=\"12\" maxlength=\"32\">.local");
      message += "<p class=\"warn\">Applied after Save - the device will restart automatically.</p>\r\n";
    }
    if(String(w3srv.arg(0)).equals("timeZoneDST")) {
      editRow(message, "Time zone offset", "<input type=\"text\" name=\"timeZone\" value=\"" + String(cfg.timeZone) + "\" size=\"6\" maxlength=\"6\"> s", "e.g. 3600 for most of Europe - see epochconverter.com/timezones");
      editRow(message, "DST offset", "<input type=\"text\" name=\"dst\" value=\"" + String(cfg.dst) + "\" size=\"6\" maxlength=\"6\"> s", "usually 0 in winter, 3600 in summer");
      message += "<p class=\"warn\">Applied after Save - the device will restart automatically.</p>\r\n";
    }
    if(String(w3srv.arg(0)).equals("restartAt")) {
      editRow(message, "Restart at time", "<input type=\"text\" name=\"restart_at_time\" value=\"" + String(cfg.restart_at_time) + "\" size=\"5\" maxlength=\"5\">", "24h HH:MM, or NORES for never");
      editRow(message, "Restart after errors", "<input type=\"text\" name=\"restart_at_logged_errors\" value=\"" + String(cfg.restart_at_logged_errors) + "\" size=\"5\" maxlength=\"5\">", "0 = never");
    }
    if(String(w3srv.arg(0)).equals("alarmTiming")) {
      editRow(message, "Snooze timeout", "<input type=\"text\" name=\"snooze_timeout\" value=\"" + String(cfg.snooze_timeout) + "\" size=\"5\" maxlength=\"5\"> min");
      editRow(message, "Repeat alarm every", "<input type=\"text\" name=\"alarm_repeat\" value=\"" + String(cfg.alarm_repeat) + "\" size=\"5\" maxlength=\"5\"> min", "0 = as fast as it goes");
    }
    if(String(w3srv.arg(0)).equals("dispColors")) {
      editRow(message, "Yellow below", "<input type=\"text\" name=\"yellow_low\" value=\"" + String(cfg.yellow_low*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "y");
      editRow(message, "Yellow above", "<input type=\"text\" name=\"yellow_high\" value=\"" + String(cfg.yellow_high*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "y");
      editRow(message, "Red below", "<input type=\"text\" name=\"red_low\" value=\"" + String(cfg.red_low*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "r");
      editRow(message, "Red above", "<input type=\"text\" name=\"red_high\" value=\"" + String(cfg.red_high*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "r");
    }
    if(String(w3srv.arg(0)).equals("sndAlarms")) {
      editRow(message, "Warning sound below", "<input type=\"text\" name=\"snd_warning\" value=\"" + String(cfg.snd_warning*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "t");
      editRow(message, "Warning sound above", "<input type=\"text\" name=\"snd_warning_high\" value=\"" + String(cfg.snd_warning_high*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "t");
      editRow(message, "Alarm sound below", "<input type=\"text\" name=\"snd_alarm\" value=\"" + String(cfg.snd_alarm*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "t");
      editRow(message, "Alarm sound above", "<input type=\"text\" name=\"snd_alarm_high\" value=\"" + String(cfg.snd_alarm_high*mult, decpl) + "\" size=\"5\" maxlength=\"5\"> " + sgvUnits, NULL, "t");
      editRow(message, "No-readings warning", "<input type=\"text\" name=\"snd_no_readings\" value=\"" + String(cfg.snd_no_readings) + "\" size=\"5\" maxlength=\"5\"> min", NULL, "t");
      editRow(message, "Warning sound volume", "<input type=\"text\" name=\"warning_volume\" value=\"" + String(cfg.warning_volume) + "\" size=\"5\" maxlength=\"5\"> %", NULL, "t");
      editRow(message, "Alarm sound volume", "<input type=\"text\" name=\"alarm_volume\" value=\"" + String(cfg.alarm_volume) + "\" size=\"3\" maxlength=\"3\"> %", NULL, "t");
    }
    if(String(w3srv.arg(0)).equals("brightness")) {
      editRow(message, "Step 1 (default)", "<input type=\"text\" name=\"brightness1\" value=\"" + String(cfg.brightness1) + "\" size=\"3\" maxlength=\"3\"> %");
      editRow(message, "Step 2", "<input type=\"text\" name=\"brightness2\" value=\"" + String(cfg.brightness2) + "\" size=\"3\" maxlength=\"3\"> %");
      editRow(message, "Step 3", "<input type=\"text\" name=\"brightness3\" value=\"" + String(cfg.brightness3) + "\" size=\"3\" maxlength=\"3\"> %");
    }
    if(String(w3srv.arg(0)).equals("led_strip")) {
      editRow(message, "LED strip pin", "<input type=\"text\" name=\"LED_strip_pin\" value=\"" + String(cfg.LED_strip_pin) + "\" size=\"3\" maxlength=\"3\">", "15 = Fire internal, 26 = PORT B, 17 = PORT C (21/PORT A collides with I2C)");
      editRow(message, "LED strip count", "<input type=\"text\" name=\"LED_strip_count\" value=\"" + String(cfg.LED_strip_count) + "\" size=\"3\" maxlength=\"3\">");
      editRow(message, "LED strip brightness", "<input type=\"text\" name=\"LED_strip_brightness\" value=\"" + String(cfg.LED_strip_brightness) + "\" size=\"3\" maxlength=\"3\"> %", "use 5-10% for more than 10 LEDs, or load current will crash the M5Stack");
    }
    if(String(w3srv.arg(0)).equals("vibration_motor")) {
      editRow(message, "Vibration motor pin", "<input type=\"text\" name=\"vibration_pin\" value=\"" + String(cfg.vibration_pin) + "\" size=\"3\" maxlength=\"3\">", "26 = PORT B, 17 = PORT C - do not use 21/PORT A (collides with I2C)");
      editRow(message, "Vibration strength", "<input type=\"text\" name=\"vibration_strength\" value=\"" + String(cfg.vibration_strength) + "\" size=\"3\" maxlength=\"3\">", "0-1023, recommended 256-512 - higher can crash the M5Stack");
    }
    if(String(w3srv.arg(0)).equals("wlans")) {
      // Only scan here, not on every edit-item page: on ESP32 an AP-mode scan needs to
      // channel-hop, which can knock already-connected SoftAP clients (like the phone
      // filling out this very form) off the network mid-scan.
      int numSsids = WiFi.scanNetworks();
      // A <datalist> only offers scanned networks as suggestions - unlike a <select>, it
      // still lets you type any SSID, so hidden or out-of-range networks can be entered too.
      message += "<datalist id=\"wlanscan\">";
      for (int j=0; j<numSsids; j++) {
        message += "<option value=\"" + String(WiFi.SSID(j)) + "\">";
      }
      message += "</datalist>\r\n";
      for(int i=0; i<10; i++) {
        String ssidInput = "<input type=\"text\" name=\"wlanssid" + String(i) + "\" list=\"wlanscan\" value=\"" + String(cfg.wlanssid[i]) + "\" size=\"12\" maxlength=\"63\" placeholder=\"SSID (or type a hidden/out-of-range one)\">";
        ssidInput += " <input type=\"password\" name=\"wlanpass" + String(i) + "\" value=\"" + String(cfg.wlanpass[i]) + "\" size=\"12\" maxlength=\"63\" placeholder=\"password\">";
        char lbl[10]; snprintf(lbl, sizeof(lbl), "wlan%d", i);
        editRow(message, lbl, ssidInput);
      }
      message += "<p class=\"warn\">Applied after Save - the device will restart automatically.</p>\r\n";
    }
  }
  message += "<div style=\"margin-top:10px\">\r\n";
  message += "<input type=\"submit\" class=\"btn\" name=\"Submit\" value=\"Apply\">\r\n";
  message += "<input type=\"button\" class=\"btn2\" name=\"Cancel\" value=\"Cancel\" onClick=\"window.location.href='/?s=" + sec + "';\" />\r\n";
  message += "</div>\r\n";
  message += "</form>\r\n";
  message += "</body>\r\n";
  message += "</html>\r\n";
  w3srv.send(200, "text/html", message);
  if(cfg.LED_strip_mode != 0) {
    pixels.show();
  }
}

void handleGetEditConfigItem() {
  // String sgvUnits = cfg.show_mgdl?"mg/dL":"mmol/L";
  // int decpl = cfg.show_mgdl?0:1;
  float mult = cfg.show_mgdl?18.0:1.0;
  String tmpStr;
  bool wlanChanged = false;

  String sec = "st";
  for (uint8_t i = 0; i < w3srv.args(); i++) {
    if(String(w3srv.argName(i)).equals("s")) {
      sec = String(w3srv.arg(i));
    }
  }

  String message;
  message.reserve(768);
  String meta = "<meta http-equiv=\"refresh\" content=\"1;url=/?s=" + sec + "\" />\r\n";
  pageHeadOpen(message, meta.c_str());
  message += "<p>Updating edited items in M5Stack Nightscout monitor configuration (NOT saving to M5NS.INI file).</p>\r\n";
  for (uint8_t i = 0; i < w3srv.args(); i++) {
    if(String(w3srv.argName(i)).equals("userName")) {
      strncpy(cfg.userName, String(w3srv.arg(i)).c_str(), 32);
    }
    if(String(w3srv.argName(i)).equals("url")) {
      strncpy(cfg.url, String(w3srv.arg(i)).c_str(), 128);
    }
    if(String(w3srv.argName(i)).equals("token")) {
      strncpy(cfg.token, String(w3srv.arg(i)).c_str(), 64);
    }
    if(String(w3srv.argName(i)).equals("dexcom_user")) {
      strncpy(cfg.dexcom_user, String(w3srv.arg(i)).c_str(), 64);
      dexcomResetSession();
    }
    if(String(w3srv.argName(i)).equals("dexcom_pass")) {
      strncpy(cfg.dexcom_pass, String(w3srv.arg(i)).c_str(), 64);
      dexcomResetSession();
    }
    if(String(w3srv.argName(i)).equals("libre_user")) {
      strncpy(cfg.libre_user, String(w3srv.arg(i)).c_str(), 64);
      libreResetSession();
    }
    if(String(w3srv.argName(i)).equals("libre_pass")) {
      strncpy(cfg.libre_pass, String(w3srv.arg(i)).c_str(), 64);
      libreResetSession();
    }
    if(String(w3srv.argName(i)).equals("deviceName")) {
      String newVal = String(w3srv.arg(i));
      if(!newVal.equals(cfg.deviceName)) restartPending = true;
      strncpy(cfg.deviceName, newVal.c_str(), 32);
    }
    if(String(w3srv.argName(i)).equals("timeZone")) {
      long newVal = String(w3srv.arg(i)).toInt();
      if(newVal != cfg.timeZone) restartPending = true;
      cfg.timeZone = newVal;
    }
    if(String(w3srv.argName(i)).equals("dst")) {
      long newVal = String(w3srv.arg(i)).toInt();
      if(newVal != cfg.dst) restartPending = true;
      cfg.dst = newVal;
    }
    if(String(w3srv.argName(i)).equals("restart_at_time")) {
      strncpy(cfg.restart_at_time, String(w3srv.arg(i)).c_str(), 10);
    }
    if(String(w3srv.argName(i)).equals("restart_at_logged_errors")) {
      cfg.restart_at_logged_errors = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("snooze_timeout")) {
      cfg.snooze_timeout = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("alarm_repeat")) {
      cfg.alarm_repeat = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("yellow_low")) {
      cfg.yellow_low = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("yellow_high")) {
      cfg.yellow_high = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("red_low")) {
      cfg.red_low = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("red_high")) {
      cfg.red_high = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("snd_warning")) {
      cfg.snd_warning = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("snd_warning_high")) {
      cfg.snd_warning_high = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("snd_alarm")) {
      cfg.snd_alarm = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("snd_alarm_high")) {
      cfg.snd_alarm_high = String(w3srv.arg(i)).toFloat()/mult;
    }
    if(String(w3srv.argName(i)).equals("snd_no_readings")) {
      cfg.snd_no_readings = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("warning_volume")) {
      cfg.warning_volume = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("alarm_volume")) {
      cfg.alarm_volume = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("brightness1")) {
      if(lcdBrightness==cfg.brightness1) {
        // changing selected brightness, so update it
        lcdBrightness = String(w3srv.arg(i)).toInt();
        M5.Display.setBrightness(map(lcdBrightness, 0, 100, 0, 255));
      }
      cfg.brightness1 = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("brightness2")) {
      if(lcdBrightness==cfg.brightness2) {
        // changing selected brightness, so update it
        lcdBrightness = String(w3srv.arg(i)).toInt();
        M5.Display.setBrightness(map(lcdBrightness, 0, 100, 0, 255));
      }
      cfg.brightness2 = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("brightness3")) {
      if(lcdBrightness==cfg.brightness3) {
        // changing selected brightness, so update it
        lcdBrightness = String(w3srv.arg(i)).toInt();
        M5.Display.setBrightness(map(lcdBrightness, 0, 100, 0, 255));
      }
      cfg.brightness3 = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("LED_strip_pin")) {
      int oldpin = cfg.LED_strip_pin;
      cfg.LED_strip_pin = String(w3srv.arg(i)).toInt();
      if(oldpin != cfg.LED_strip_pin) {
        pixels.clear();
        pixels.show();
        pixels.setPin(cfg.LED_strip_pin);
      }
    }
    if(String(w3srv.argName(i)).equals("LED_strip_count")) {
      int oldcnt = cfg.LED_strip_count;
      cfg.LED_strip_count = String(w3srv.arg(i)).toInt();
      if(oldcnt != cfg.LED_strip_count) {
        pixels.clear();
        pixels.show();
        pixels.updateLength(cfg.LED_strip_count);
      }
    }
    if(String(w3srv.argName(i)).equals("LED_strip_brightness")) {
      cfg.LED_strip_brightness = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("vibration_pin")) {
      cfg.vibration_pin = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).equals("vibration_strength")) {
      cfg.vibration_strength = String(w3srv.arg(i)).toInt();
    }
    if(String(w3srv.argName(i)).startsWith("wlanssid")) {
      tmpStr = String(w3srv.argName(i));
      tmpStr.remove(0, 8);
      int nr = tmpStr.toInt();
      String newVal = String(w3srv.arg(i));
      if(!newVal.equals(cfg.wlanssid[nr])) { restartPending = true; wlanChanged = true; }
      message += "WLAN SSID [" + tmpStr + "] = " + newVal + " (" + newVal.length() + ")<br />\r\n";
      strncpy(cfg.wlanssid[nr], newVal.c_str(), 32);
    }
    if(String(w3srv.argName(i)).startsWith("wlanpass")) {
      tmpStr = String(w3srv.argName(i));
      tmpStr.remove(0, 8);
      int nr = tmpStr.toInt();
      String newVal = String(w3srv.arg(i));
      if(!newVal.equals(cfg.wlanpass[nr])) { restartPending = true; wlanChanged = true; }

      // message += "WLAN PASS [" + tmpStr + "] = " + String(w3srv.arg(i)) + " (" + String(w3srv.arg(i)).length() + ")<br />\r\n";

      message += "WLAN PASS [" + tmpStr + "] = ";
      for(int j=0; j< String(w3srv.arg(i)).length(); j++)
        message += "*";
      message += " (" + String(w3srv.arg(i).length()) + ")<br />\r\n";
      strncpy(cfg.wlanpass[nr], String(w3srv.arg(i)).c_str(), 64);
    }
  }

  // WiFi networks can't wait for a separate trip to the "Save configuration" button like every
  // other setting: losing an entered network here traps the user in an endless re-bootstrap
  // loop (a fresh random SoftAP passphrase every reboot, no way back to what was just typed).
  // So in bootstrap mode, persist to SD + flash immediately and tell the user plainly that a
  // restart is what's needed next - this also means the credentials are safely on disk before
  // the HTTP response is even sent, in case the page itself never finishes loading.
  if(wlanChanged && cfg.is_task_bootstrapping) {
    persistConfigToDisk();
    message += "<p><b>WiFi settings saved.</b> Restart the device now to connect using the new network.</p>\r\n";
  }

  message += "</body>\r\n";
  message += "</html>\r\n";
  w3srv.send(200, "text/html", message);

  if (!cfg.is_task_bootstrapping) {
    M5.Lcd.fillScreen(BLACK);
    draw_page();
    if(cfg.LED_strip_mode != 0) {
      pixels.show();
    }
  }
}

// Full SD M5NS.INI (with a M5NS.BAK backup) + NVS flash write of the current in-RAM cfg.
// Shared by handleSaveConfig() (the explicit "Save" button) and, in bootstrap mode,
// handleGetEditConfigItem() (WiFi networks only - see the call site there for why).
static void persistConfigToDisk() {
  String sgvUnits = cfg.show_mgdl?"mg/dL":"mmol/L";
  int decpl = cfg.show_mgdl?0:1;
  int mult = cfg.show_mgdl?18:1;

  M5.Lcd.drawJpgFile(SD, cfg.bootPic);

  File srcFil, dstFil;

  Serial.println("Creating backup copy of M5NS.INI to M5NS.BAK");

  if(!SD.remove("/M5NS.BAK")) {
    Serial.println("Error removing M5NS.BAK");
  }
  srcFil = SD.open("/M5NS.INI", FILE_READ);
  if(!srcFil) {
    Serial.println("Error opening M5NS.INI for read");
  } else {
    dstFil = SD.open("/M5NS.BAK", FILE_WRITE);
    if(!dstFil) {
      Serial.println("Error opening M5NS.BAK for write");
    } else {
      if(srcFil.size()>0) {
        size_t n;
        uint8_t buf[256];
        while ((n = srcFil.read(buf, sizeof(buf))) > 0) {
          dstFil.write(buf, n);
        }
        srcFil.close();
        dstFil.close();
        Serial.println("INI to BAK should be copied");
      } else {
        Serial.println("Source file M5NS.INI is empty.");
      }
    }
  }

  if(!SD.remove("/M5NS.INI")) {
    Serial.println("Error removing M5NS.BAK");
  }
  dstFil = SD.open("/M5NS.INI", FILE_WRITE);
  if(!dstFil) {
    Serial.println("Error opening M5NS.INI for write");
  } else {
    Serial.println("Writing configuration to M5NS.INI file on SD card");
    dstFil.print("[config]\r\n");
    dstFil.print("nightscout = "); dstFil.print(cfg.url); dstFil.print("\r\n");
    dstFil.print("token = "); dstFil.print(cfg.token); dstFil.print("\r\n");
    dstFil.print("data_source = "); dstFil.print(cfg.data_source); dstFil.print("\r\n");
    dstFil.print("dexcom_user = "); dstFil.print(cfg.dexcom_user); dstFil.print("\r\n");
    dstFil.print("dexcom_pass = "); dstFil.print(cfg.dexcom_pass); dstFil.print("\r\n");
    dstFil.print("dexcom_server = "); dstFil.print(cfg.dexcom_server); dstFil.print("\r\n");
    dstFil.print("libre_user = "); dstFil.print(cfg.libre_user); dstFil.print("\r\n");
    dstFil.print("libre_pass = "); dstFil.print(cfg.libre_pass); dstFil.print("\r\n");
    dstFil.print("libre_server = "); dstFil.print(cfg.libre_server); dstFil.print("\r\n");
    dstFil.print("bootpic = "); dstFil.print(cfg.bootPic); dstFil.print("\r\n");
    dstFil.print("name = "); dstFil.print(cfg.userName); dstFil.print("\r\n");
    dstFil.print("device_name = "); dstFil.print(cfg.deviceName); dstFil.print("\r\n");
    dstFil.print("time_zone = "); dstFil.print(cfg.timeZone); dstFil.print("\r\n");
    dstFil.print("dst = "); dstFil.print(cfg.dst); dstFil.print("\r\n");
    dstFil.print("show_mgdl = "); dstFil.print(cfg.show_mgdl); dstFil.print("\r\n");
    dstFil.print("show_current_time = "); dstFil.print(cfg.show_current_time); dstFil.print("\r\n");
    dstFil.print("show_COB_IOB = "); dstFil.print(cfg.show_COB_IOB); dstFil.print("\r\n");
    dstFil.print("default_page = "); dstFil.print(cfg.default_page); dstFil.print("\r\n");
    dstFil.print("restart_at_time = "); dstFil.print(cfg.restart_at_time); dstFil.print("\r\n");
    dstFil.print("restart_at_logged_errors = "); dstFil.print(cfg.restart_at_logged_errors); dstFil.print("\r\n");
    dstFil.print("\r\n");
    dstFil.print("snooze_timeout = "); dstFil.print(cfg.snooze_timeout); dstFil.print("\r\n");
    dstFil.print("alarm_repeat = "); dstFil.print(cfg.alarm_repeat); dstFil.print("\r\n");
    dstFil.print("\r\n");
    dstFil.print("yellow_low = "); dstFil.print(String(cfg.yellow_low*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("yellow_high = "); dstFil.print(String(cfg.yellow_high*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("red_low = "); dstFil.print(String(cfg.red_low*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("red_high = "); dstFil.print(String(cfg.red_high*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("\r\n");
    dstFil.print("snd_warning = "); dstFil.print(String(cfg.snd_warning*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("snd_alarm = "); dstFil.print(String(cfg.snd_alarm*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("snd_warning_high = "); dstFil.print(String(cfg.snd_warning_high*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("snd_alarm_high = "); dstFil.print(String(cfg.snd_alarm_high*mult, decpl)); dstFil.print("\r\n");
    dstFil.print("snd_no_readings = "); dstFil.print(cfg.snd_no_readings); dstFil.print("\r\n");
    dstFil.print("snd_loop_error = "); dstFil.print(cfg.snd_loop_error); dstFil.print("\r\n");
    dstFil.print("snd_warning_at_startup = "); dstFil.print(cfg.snd_warning_at_startup); dstFil.print("\r\n");
    dstFil.print("snd_alarm_at_startup = "); dstFil.print(cfg.snd_alarm_at_startup); dstFil.print("\r\n");
    dstFil.print("\r\n");
    dstFil.print("warning_volume = "); dstFil.print(cfg.warning_volume); dstFil.print("\r\n");
    dstFil.print("alarm_volume = "); dstFil.print(cfg.alarm_volume); dstFil.print("\r\n");
    dstFil.print("\r\n");
    dstFil.print("brightness1 = "); dstFil.print(cfg.brightness1); dstFil.print("\r\n");
    dstFil.print("brightness2 = "); dstFil.print(cfg.brightness2); dstFil.print("\r\n");
    dstFil.print("brightness3 = "); dstFil.print(cfg.brightness3); dstFil.print("\r\n");
    dstFil.print("\r\n");
    dstFil.print("sgv_only = "); dstFil.print(cfg.sgv_only); dstFil.print("\r\n");
    dstFil.print("info_line = "); dstFil.print(cfg.info_line); dstFil.print("\r\n");
    dstFil.print("date_format = "); dstFil.print(cfg.date_format); dstFil.print("\r\n");
    dstFil.print("time_format = "); dstFil.print(cfg.time_format); dstFil.print("\r\n");
    dstFil.print("display_rotation = "); dstFil.print(cfg.display_rotation); dstFil.print("\r\n");
    if(cfg.invert_display!=-1) {
      dstFil.print("invert_display = "); dstFil.print(cfg.invert_display); dstFil.print("\r\n");
    }
    dstFil.print("temperature_unit = "); dstFil.print(cfg.temperature_unit); dstFil.print("\r\n");
    dstFil.print("LED_strip_mode = "); dstFil.print(cfg.LED_strip_mode); dstFil.print("\r\n");
    dstFil.print("LED_strip_pin = "); dstFil.print(cfg.LED_strip_pin); dstFil.print("\r\n");
    dstFil.print("LED_strip_count = "); dstFil.print(cfg.LED_strip_count); dstFil.print("\r\n");
    dstFil.print("LED_strip_brightness = "); dstFil.print(cfg.LED_strip_brightness); dstFil.print("\r\n");
    dstFil.print("vibration_mode = "); dstFil.print(cfg.vibration_mode); dstFil.print("\r\n");
    dstFil.print("vibration_pin = "); dstFil.print(cfg.vibration_pin); dstFil.print("\r\n");
    dstFil.print("vibration_strength = "); dstFil.print(cfg.vibration_strength); dstFil.print("\r\n");
    dstFil.print("micro_dot_pHAT = "); dstFil.print(cfg.micro_dot_pHAT); dstFil.print("\r\n");
    dstFil.print("disable_web_server = "); dstFil.print(cfg.disable_web_server); dstFil.print("\r\n");
    dstFil.print("\r\n");
    dstFil.print("developer_mode = "); dstFil.print(cfg.dev_mode); dstFil.print("\r\n");
    dstFil.print("\r\n");
    for(int i=0; i<10; i++) {
      if(cfg.wlanssid[i][0] != 0) {
        dstFil.print("[wlan"); dstFil.print(i); dstFil.print("]\r\n");
        dstFil.print("SSID = "); dstFil.print(cfg.wlanssid[i]); dstFil.print("\r\n");
        if(cfg.wlanpass[i][0]!=0) {
          dstFil.print("PASS = "); dstFil.print(cfg.wlanpass[i]); dstFil.print("\r\n");
        }
      }
    }
    dstFil.flush();
    dstFil.close();
    Serial.println("New M5NS.INI saved to SD card.");
  }

  saveConfigToFlash(&cfg);
}

void handleSaveConfig() {
  // Bootstrap mode always restarts once configured (existing behavior); otherwise restart
  // only if an edited item that needs it (device name, time zone/DST, WiFi networks) was
  // actually changed - tracked in handleGetEditConfigItem() via restartPending.
  bool willRestart = cfg.is_task_bootstrapping || restartPending;

  String message;
  message.reserve(768);
  pageHeadOpen(message, willRestart
    ? "<meta http-equiv=\"refresh\" content=\"5;url=/\" />\r\n"
    : "<meta http-equiv=\"refresh\" content=\"2;url=/\" />\r\n");
  message += "<p>Saving configuration to M5NS.INI file.</p>\r\n";
  message += "<p>Backup copy of current M5NS.INI should be blaced in M5NS.BAK file.</p>\r\n";
  if (willRestart) {
    message += "<p><b>Restarting to apply changes...</b></p>\r\n";
  }
  message += "</body>\r\n";
  message += "</html>\r\n";

  persistConfigToDisk();

  w3srv.send(200, "text/html", message);
  delay(100);

  if (cfg.is_task_bootstrapping) {
    M5.update();
    WiFi.softAPdisconnect(true);
    delay(1000);
    ESP.restart();
  } else if (restartPending) {
    M5.update();
    delay(1000);
    ESP.restart();
  } else {
    M5.Lcd.fillScreen(BLACK);
    draw_page();
    if(cfg.LED_strip_mode != 0) {
      pixels.show();
    }
  }
}

void handleClearConfigFlash() {
  Preferences prefs;
  if( !prefs.begin("M5NSconfig", false) ) {
    Serial.println("Error opening Preferences M5NSconfig");
  } else {
    Serial.println("Clearing configuration Preferences M5NSconfig");
    prefs.clear();
    prefs.end();
  }
  ESP.restart();
}

void handleNotFound() {
  String message = "M5Stack Nighscout monitor ERROR 404 File Not Found\n\n";
  message += "URI: ";
  message += w3srv.uri();
  message += "\nMethod: ";
  message += (w3srv.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += w3srv.args();
  message += "\n";
  for (uint8_t i = 0; i < w3srv.args(); i++) {
    message += " " + w3srv.argName(i) + ": " + w3srv.arg(i) + "\n";
  }
  w3srv.send(404, "text/plain", message);
}
