#pragma once
#include <Arduino.h>
// Default captions for built-in TFT widgets. User overrides are stored in LittleFS.
struct UiTexts { String stringPrefix,currentLabel,voltageLabel,routerLabel,apLabel,noRouterLabel,networkLabel,signalLabel,apSetupLabel,selectHint,changeHint,applyLabel; };
static inline void setDefaultUiTexts(UiTexts& t) {
 t.stringPrefix="Стринг";t.currentLabel="Ток";t.voltageLabel="Напр.";t.routerLabel="Роутер";t.apLabel="Точка";t.noRouterLabel="Роутер не подключён";
 t.networkLabel="Сеть";t.signalLabel="Сигнал";t.apSetupLabel="Точка настройки:";t.selectHint="ВВЕРХ/ВНИЗ: выбор";t.changeHint="OK: изменить (5 сек.)";t.applyLabel="Применение через";
}
static inline String sanitizeUiText(String v,const char* fallback) {
 v.replace("\r"," ");v.replace("\n"," ");v.trim();if(!v.length())v=fallback;
 if(v.length()>72){v.remove(72);while(v.length()&&(((uint8_t)v[v.length()-1]&0xC0)==0x80))v.remove(v.length()-1);}return v;
}
