#pragma once
#include <Arduino.h>

#ifdef ESP8266

#define RTCU_ERROR 0
#define RTCU_READ 1
#define RTCU_RESET 2

// записать данные в rtc память. Вернёт false при ошибке
template <typename T>
bool rtc_write(T* data, uint8_t offset = 0, uint32_t* _crc = nullptr) {
    uint32_t crc = _crc ? *_crc : crc32((void*)data, sizeof(T));
    if (!ESP.rtcUserMemoryWrite(offset, &crc, sizeof(crc))) return 0;
    if (!ESP.rtcUserMemoryWrite(offset + 1, (uint32_t*)data, sizeof(T))) return 0;
    return 1;
}

// прочитать данные из rtc памяти. Вернёт 0 при ошибке, 1 если данные прочитаны, 2 если это первый запуск (данные сброшены)
template <typename T>
uint8_t rtc_read(T* data, uint8_t offset = 0) {
    uint32_t crc;
    if (!ESP.rtcUserMemoryRead(offset, &crc, sizeof(crc))) return RTCU_ERROR;
    if (!ESP.rtcUserMemoryRead(offset + 1, (uint32_t*)data, sizeof(T))) return RTCU_ERROR;
    uint32_t _crc = crc32((void*)data, sizeof(T));
    if (_crc != crc) {
        *data = T();
        return rtc_write(data, offset, &_crc) ? RTCU_RESET : RTCU_READ;
    }
    return RTCU_READ;
}

#endif