#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WebServer.h>

namespace desk {
void begin(Preferences *preferences);
void routes(WebServer &server, bool (*writableRequest)());
String stateJson();
void invalidate();
void tick();
bool draw(TFT_eSPI &screen, bool uploading);
bool takeVideoResume();
void videoClock(TFT_eSPI &screen, bool newFrame = false);
uint8_t brightness(uint8_t dayBrightness);
uint8_t ledLevel(uint8_t dayLevel);
} // namespace desk
