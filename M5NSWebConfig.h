#ifndef _M5NSWEBCONFIG_H
#define _M5NSWEBCONFIG_H

#include <Adafruit_NeoPixel.h>
extern Adafruit_NeoPixel pixels;

#include "microdot.h"
extern MicroDot MD;

void handleRoot();
void handleFwCheck();
void handleUpdate();

// On-device OTA (no browser needed) - used by the config page's middle-button update.
bool otaCheckLatest();
bool otaUpdateAvailable();
String otaLatestVersion();
void otaRunUpdate();
void handleSwitchConfig();
void handleEditConfigItem();
void handleGetEditConfigItem();
void handleSaveConfig();
void handleDownloadConfig();
void handleUploadConfig();
void handleUploadConfigFile();
void handleClearConfigFlash();
void handleNotFound();

#endif
