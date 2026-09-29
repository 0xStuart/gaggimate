#include "ArduinoOTAPlugin.h"
#include "../core/Controller.h"
#include "../core/Event.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp_log.h>

static constexpr char LOG_TAG[] = "ArduinoOTAPlugin";
static constexpr uint16_t NETWORK_OTA_PORT = 3232;

void ArduinoOTAPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    this->settings = &controller->getSettings();

    ArduinoOTA.setPort(NETWORK_OTA_PORT);
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.onStart([this]() {
        otaInProgress = true;
        ESP_LOGI(LOG_TAG, "Network firmware upload started");
    });
    ArduinoOTA.onEnd([this]() {
        otaInProgress = false;
        ESP_LOGI(LOG_TAG, "Network firmware upload finished");
    });
    ArduinoOTA.onError([this](ota_error_t error) {
        otaInProgress = false;
        ESP_LOGE(LOG_TAG, "Network firmware upload failed (%u)", error);
    });

    pluginManager->on("controller:wifi:connect", [this](Event const &event) { staConnected = event.getInt("AP") == 0; });
    pluginManager->on("controller:wifi:disconnect", [this](Event const &) { staConnected = false; });
}

void ArduinoOTAPlugin::loop() {
    const bool want = staConnected && settings != nullptr && settings->isNetworkOtaEnabled();
    if (want && !running) {
        start();
    } else if (!want && running) {
        stop();
    }

    if (!running)
        return;
    if (!otaInProgress && (controller->isActive() || controller->isUpdating()))
        return;
    ArduinoOTA.handle();
}

void ArduinoOTAPlugin::start() {
    String hostname = settings->getMdnsName();
    if (hostname.isEmpty()) {
        hostname = "gaggimate";
    }
    ArduinoOTA.setHostname(hostname.c_str());
    ArduinoOTA.begin();
    running = true;
    ESP_LOGI(LOG_TAG, "Network OTA listening on %s:%u (no auth)", WiFi.localIP().toString().c_str(), NETWORK_OTA_PORT);
}

void ArduinoOTAPlugin::stop() {
    ArduinoOTA.end();
    running = false;
    otaInProgress = false;
    ESP_LOGI(LOG_TAG, "Network OTA stopped");
}
