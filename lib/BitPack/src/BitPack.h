/*
    BitPack - библиотека для упаковки битовых флагов в байтовый массив (экономия места)
    Документация:
    GitHub: https://github.com/GyverLibs/BitPack

    AlexGyver, alex@alexgyver.ru
    https://alexgyver.ru/
    MIT License
*/

#pragma once
#include <Arduino.h>

#define BP_BYTE(pack, idx) pack[(idx) >> 3]
#define BP_BIT(pack, idx) ((idx) & 0b111)
#define BP_SET(pack, idx) (BP_BYTE(pack, idx) |= 1 << BP_BIT(pack, idx))
#define BP_CLEAR(pack, idx) (BP_BYTE(pack, idx) &= ~(1 << BP_BIT(pack, idx)))
#define BP_READ(pack, idx) ((BP_BYTE(pack, idx) >> BP_BIT(pack, idx)) & 1)
#define BP_TOGGLE(pack, idx) (BP_BYTE(pack, idx) ^= 1 << BP_BIT(pack, idx))
#define BP_WRITE(pack, idx, val) (val) ? BP_SET(pack, idx) : BP_CLEAR(pack, idx)

// ============== STATIC PACK ==============
template <uint16_t flag_amount>
class BitPack {
   public:
    uint8_t pack[(flag_amount + 8 - 1) >> 3];  // round up

    BitPack() {
        clearAll();
    }

    // размер pack в байтах
    uint16_t size() {
        return (flag_amount + 8 - 1) >> 3;
    }

    // количество флагов
    uint16_t amount() {
        return flag_amount;
    }

    // установить
    void set(uint16_t idx) {
        if (idx < flag_amount) BP_SET(pack, idx);
    }

    // снять
    void clear(uint16_t idx) {
        if (idx < flag_amount) BP_CLEAR(pack, idx);
    }

    // инвертировать
    void toggle(uint16_t idx) {
        if (idx < flag_amount) BP_TOGGLE(pack, idx);
    }

    // записать
    void write(uint16_t idx, bool state) {
        if (idx < flag_amount) BP_WRITE(pack, idx, state);
    }

    // прочитать
    bool read(uint16_t idx) {
        if (idx < flag_amount) return BP_READ(pack, idx);
        else return 0;
    }

    // установить все
    void setAll() {
        memset(pack, 255, size());
    }

    // очистить все
    void clearAll() {
        memset(pack, 0, size());
    }

    // копировать в
    bool copyTo(BitPack& bp) {
        if (amount() != bp.amount()) return 0;
        memcpy(bp.pack, pack, size());
        return 1;
    }

    // копировать из
    bool copyFrom(BitPack& bp) {
        if (amount() != bp.amount()) return 0;
        memcpy(pack, bp.pack, size());
        return 1;
    }

#ifndef BP_NO_ARRAY
    BitPack& operator[](uint16_t idx) {
        _idx = idx;
        return *this;
    }
    void operator=(bool val) {
        write(_idx, val);
    }
    operator bool() {
        return read(_idx);
    }
    void operator=(BitPack& bp) {
        write(_idx, (bool)bp);
    }
#endif

   private:
#ifndef BP_NO_ARRAY
    uint16_t _idx;
#endif
};

// ============== EXTERNAL BUFFER ==============
class BitPackExt {
   public:
    uint8_t* pack = nullptr;

    BitPackExt() {}

    // передать буфер и его размер в количестве флагов (8 флагов - 1 байт)
    BitPackExt(uint8_t* pack, uint16_t amount, bool clear = true) {
        setBuffer(pack, amount, clear);
    }

    // передать буфер и его размер в количестве флагов (8 флагов - 1 байт)
    void setBuffer(uint8_t* pack, uint16_t amount, bool clear = true) {
        this->pack = pack;
        this->_amount = amount;
        if (clear) clearAll();
    }

    // размер pack в байтах
    uint16_t size() {
        return (_amount + 8 - 1) >> 3;  // round up
    }

    // количество флагов
    uint16_t amount() {
        return _amount;
    }

    // установить
    void set(uint16_t idx) {
        if (pack && idx < _amount) BP_SET(pack, idx);
    }

    // снять
    void clear(uint16_t idx) {
        if (pack && idx < _amount) BP_CLEAR(pack, idx);
    }

    // инвертировать
    void toggle(uint16_t idx) {
        if (pack && idx < _amount) BP_TOGGLE(pack, idx);
    }

    // записать
    void write(uint16_t idx, bool state) {
        if (pack && idx < _amount) BP_WRITE(pack, idx, state);
    }

    // прочитать
    bool read(uint16_t idx) {
        if (pack && idx < _amount) return BP_READ(pack, idx);
        else return 0;
    }

    // установить все
    void setAll() {
        if (pack) memset(pack, 255, size());
    }

    // очистить все
    void clearAll() {
        if (pack) memset(pack, 0, size());
    }

    // копировать в
    bool copyTo(BitPackExt& bp) {
        if (!pack || amount() != bp.amount()) return 0;
        memcpy(bp.pack, pack, size());
        return 1;
    }

    // копировать из
    bool copyFrom(BitPackExt& bp) {
        if (!pack || amount() != bp.amount()) return 0;
        memcpy(pack, bp.pack, size());
        return 1;
    }

#ifndef BP_NO_ARRAY
    BitPackExt& operator[](uint16_t idx) {
        _idx = idx;
        return *this;
    }
    void operator=(bool val) {
        write(_idx, val);
    }
    operator bool() {
        return read(_idx);
    }
    void operator=(BitPackExt& bp) {
        write(_idx, (bool)bp);
    }
#endif

   protected:
    uint16_t _amount;

   private:
#ifndef BP_NO_ARRAY
    uint16_t _idx;
#endif
};

// ============== DYNAMIC BUFFER ==============
class BitPackDyn : public BitPackExt {
   public:
    BitPackDyn() {}

    // указать количество флагов
    BitPackDyn(uint16_t amount) {
        init(amount);
    }

    // указать количество флагов
    void init(uint16_t amount) {
        if (pack) delete[] pack;
        _amount = amount;
        pack = new uint8_t[size()];
        if (!pack) _amount = 0;
        clearAll();
    }

    ~BitPackDyn() {
        if (pack) delete[] pack;
    }

   private:
};