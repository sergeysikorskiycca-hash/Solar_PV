#pragma once
#include <Arduino.h>
// Default captions for built-in TFT widgets. User overrides are stored in LittleFS.
struct UiTexts { String stringPrefix,currentLabel,voltageLabel,routerLabel,apLabel,noRouterLabel,networkLabel,signalLabel,apSetupLabel,selectHint,changeHint,applyLabel; };
static inline void setDefaultUiTexts(UiTexts& t) {
 t.stringPrefix="Стринг";t.currentLabel="Ток";t.voltageLabel="Напр.";t.routerLabel="Роутер";t.apLabel="Точка";t.noRouterLabel="Роутер не подключён";
 t.networkLabel="Сеть";t.signalLabel="Сигнал";t.apSetupLabel="Точка настройки:";t.selectHint="ВВЕРХ/ВНИЗ: выбор";t.changeHint="OK: изменить (5 сек.)";t.applyLabel="Применение через";
}
static inline String sanitizeUiText(String v,const char* fallback) {
  v.replace("\r"," "); v.replace("\n"," "); v.trim();
  if (!v.length()) v = fallback;
  String out; out.reserve(72);
  for (size_t i = 0; i < v.length();) {
    uint8_t c = (uint8_t)v[i];
    if (c < 0x20) { ++i; continue; }
    uint8_t n = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2 :
                 ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 0)));
    if (!n || i + n > v.length() || out.length() + n > 72) break;
    bool valid = true;
    for (uint8_t j = 1; j < n; ++j)
      if (((uint8_t)v[i + j] & 0xC0) != 0x80) valid = false;
    if (valid) { out += v.substring(i, i + n); i += n; }
    else ++i;
  }
  out.trim();
  if (!out.length()) out = fallback;
  return out;
}
