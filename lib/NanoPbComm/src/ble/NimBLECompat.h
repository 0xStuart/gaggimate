// Narrow shims so NanoPbComm builds against NimBLE-Arduino 1.4 (upstream boards)
// and esp-nimble-cpp 2.x on ESP32-P4 (CONFIG_NIMBLE_CPP_IDF from display-p4).
#pragma once

#include <NimBLEDevice.h>

#if defined(CONFIG_NIMBLE_CPP_IDF)
#define GM_NIMBLE_V2 1
using GmScanCallbacks = NimBLEScanCallbacks;
using GmCharCallbacks = NimBLECharacteristicCallbacks;
inline const uint8_t *gmAddrBytes(const NimBLEAddress &addr) { return addr.getVal(); }
inline void gmSetTxPower() { NimBLEDevice::setPower(9); }
inline uint16_t gmConnHandle(NimBLEClient *client) { return client->getConnHandle(); }
#else
#define GM_NIMBLE_V2 0
using GmScanCallbacks = NimBLEAdvertisedDeviceCallbacks;
using GmCharCallbacks = BLECharacteristicCallbacks;
inline const uint8_t *gmAddrBytes(const NimBLEAddress &addr) { return addr.getNative(); }
inline void gmSetTxPower() { NimBLEDevice::setPower(ESP_PWR_LVL_P9); }
inline uint16_t gmConnHandle(NimBLEClient *client) { return client->getConnId(); }
#endif
