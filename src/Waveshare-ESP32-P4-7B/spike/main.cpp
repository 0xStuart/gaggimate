// Isolated go/no-go spike for the Waveshare ESP32-P4 7B.
// Proves USB flash, backlight, Wi-Fi (ESP-Hosted C6), and BLE scan.
// Optional STA / BLE connect via an untracked wifi_secrets.h — never commit secrets.

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <cstring>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

#ifndef SPIKE_WIFI_SSID
#define SPIKE_WIFI_SSID ""
#endif
#ifndef SPIKE_WIFI_PASS
#define SPIKE_WIFI_PASS ""
#endif
#ifndef SPIKE_BLE_CONNECT_NAME
#define SPIKE_BLE_CONNECT_NAME ""
#endif

namespace {

constexpr int BACKLIGHT_GPIO = 32; // Waveshare 7B: PWM, active-low / inverted
constexpr int BLE_SCAN_SECONDS = 15;
constexpr uint32_t HEARTBEAT_MS = 5000;

bool wifiSecretsPresent() { return SPIKE_WIFI_SSID[0] != '\0'; }
bool bleConnectRequested() { return SPIKE_BLE_CONNECT_NAME[0] != '\0'; }

void setupBacklight() {
    // Arduino 3.x LEDC API: attach by pin. Inverted — duty 0 = full on.
    ledcAttach(BACKLIGHT_GPIO, 5000, 8);
    ledcWrite(BACKLIGHT_GPIO, 0);
}

void pulseBacklight() {
    ledcWrite(BACKLIGHT_GPIO, 200);
    delay(80);
    ledcWrite(BACKLIGHT_GPIO, 0);
}

void runWifiCheck() {
    Serial.println();
    Serial.println(F("=== Wi-Fi (ESP-Hosted / C6) ==="));
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(200);

    if (wifiSecretsPresent()) {
        Serial.printf("STA connect to SSID '%s'...\n", SPIKE_WIFI_SSID);
        WiFi.begin(SPIKE_WIFI_SSID, SPIKE_WIFI_PASS);
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
            delay(250);
            Serial.print('.');
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("GO Wi-Fi STA: connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(),
                          WiFi.RSSI());
        } else {
            Serial.printf("Wi-Fi STA failed (status %d); falling back to scan\n", WiFi.status());
        }
    } else {
        Serial.println(F("No SPIKE_WIFI_SSID — scanning only (add spike/wifi_secrets.h for STA)"));
    }

    Serial.println(F("Wi-Fi scan..."));
    const int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    if (n < 0) {
        Serial.printf("NO-GO Wi-Fi: scan failed (%d)\n", n);
        return;
    }
    if (n == 0) {
        Serial.println(F("Wi-Fi scan returned 0 networks (hosted may be up; environment empty)"));
    } else {
        Serial.printf("GO Wi-Fi: %d network(s)\n", n);
        for (int i = 0; i < n && i < 12; i++) {
            Serial.printf("  %2d  %s  (%d dBm)%s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " open" : "");
        }
    }
    WiFi.scanDelete();
}

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  public:
    void onResult(BLEAdvertisedDevice device) override {
        count_++;
        Serial.printf("  BLE  %s  RSSI %d", device.getAddress().toString().c_str(), device.getRSSI());
        if (device.haveName()) {
            Serial.printf("  name='%s'", device.getName().c_str());
            if (bleConnectRequested() && target_.isEmpty() && device.getName() == SPIKE_BLE_CONNECT_NAME) {
                target_ = device.getAddress().toString();
                Serial.print(F("  [match]"));
            }
        }
        Serial.println();
    }

    int count() const { return count_; }
    const String &target() const { return target_; }

  private:
    int count_ = 0;
    String target_;
};

void tryBleConnect(const String &addr) {
    Serial.printf("BLE connect to %s ('%s')...\n", addr.c_str(), SPIKE_BLE_CONNECT_NAME);
    BLEClient *client = BLEDevice::createClient();
    BLEAddress address(addr.c_str());
    if (!client->connect(address)) {
        Serial.println(F("BLE connect failed"));
        return;
    }
    Serial.println(F("GO BLE: GATT connected"));
    auto *services = client->getServices();
    if (services != nullptr) {
        for (auto &entry : *services) {
            Serial.printf("  service %s\n", entry.first.c_str());
        }
    }
    client->disconnect();
}

void runBleCheck() {
    Serial.println();
    Serial.println(F("=== BLE (ESP-Hosted / C6 / NimBLE) ==="));
    BLEDevice::init("gaggimate-p4-spike");
    BLEScan *scan = BLEDevice::getScan();
    auto *callbacks = new ScanCallbacks();
    scan->setAdvertisedDeviceCallbacks(callbacks, /*wantDuplicates=*/false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    Serial.printf("Scanning %d s...\n", BLE_SCAN_SECONDS);
    BLEScanResults *results = scan->start(BLE_SCAN_SECONDS, /*is_continue=*/false);
    const int found = results != nullptr ? results->getCount() : callbacks->count();
    Serial.printf("Scan finished: %d advertisement(s), callback count %d\n", found, callbacks->count());
    if (callbacks->count() > 0 || found > 0) {
        Serial.println(F("GO BLE: scan returned devices"));
    } else {
        Serial.println(F("BLE scan returned 0 devices (stack may still be OK if nothing is advertising)"));
    }

    if (bleConnectRequested()) {
        if (callbacks->target().isEmpty()) {
            Serial.printf("No advertiser named '%s'\n", SPIKE_BLE_CONNECT_NAME);
        } else {
            tryBleConnect(callbacks->target());
        }
    } else {
        Serial.println(F("No SPIKE_BLE_CONNECT_NAME — skip GATT connect"));
    }

    scan->clearResults();
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F(" Waveshare ESP32-P4 7B hardware spike"));
    Serial.println(F(" USB flash + Hosted Wi-Fi + BLE go/no-go"));
    Serial.println(F("========================================"));
    Serial.printf("Chip model: %s  rev %d  cores %d\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("Flash: %u bytes  PS: %u\n", ESP.getFlashChipSize(), ESP.getPsramSize());

    setupBacklight();
    pulseBacklight();
    Serial.println(F("Backlight PWM on GPIO32 (inverted) — pulsed once"));

    runWifiCheck();
    runBleCheck();

    Serial.println();
    Serial.println(F("=== Spike checks finished ==="));
    Serial.println(F("Heartbeat every 5 s. Reset the board to re-run."));
}

void loop() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last >= HEARTBEAT_MS) {
        last = now;
        Serial.printf("[%lu] spike alive  heap=%u  wifi=%d\n", static_cast<unsigned long>(now),
                      ESP.getFreeHeap(), WiFi.status());
        pulseBacklight();
    }
}
