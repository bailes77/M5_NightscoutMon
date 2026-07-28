#ifndef _M5NSDEVICE_H
#define _M5NSDEVICE_H

// Single include point for the device layer. M5Stack boards go through M5Unified
// (auto-detect Basic/Fire/Core2/CoreS3); non-M5 boards get a shim that exposes the
// same `M5` object surface (Lcd/Display/BtnA-C/Touch/Speaker/Power/begin/update),
// so the sketch and the web-config code compile unchanged for every target.

#include <SD.h>          // must precede M5GFX/M5Unified so the fs::FS (SD) image overloads are enabled

#ifdef DEVICE_JC3248W535
  #include "hal_jc3248w535.h"
#else
  #include <M5Unified.h>
  // SD wiring differs per device: M5 boards are handled by the variant's default CS,
  // the shim boards bring their own SPI bus (see their halSDBegin()).
  inline bool halSDBegin() { return SD.begin(); }
#endif

#endif
