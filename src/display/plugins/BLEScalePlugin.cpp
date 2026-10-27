#include "BLEScalePlugin.h"
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"
#include <cmath> // For isfinite()
#include <display/core/Controller.h>
#include <scales/acaia.h>
#include <scales/bookoo.h>
#include <scales/decent.h>
#include <scales/difluid.h>
#include <scales/dot.h>
#include <scales/eclair.h>
#include <scales/eureka.h>
#include <scales/felicitaScale.h>
#include <scales/myscale.h>
#include <scales/timemore.h>
#include <scales/varia.h>
#include <scales/weighmybru.h>

void on_ble_measurement(float value) {
    if (&BLEScales != nullptr) {
        BLEScales.onMeasurement(value);
    }
}

BLEScalePlugin BLEScales;

BLEScalePlugin::BLEScalePlugin() = default;

BLEScalePlugin::~BLEScalePlugin() noexcept {
    try {
        // Disable active flag first to stop processing
        active = false;

        // Give any running callbacks time to complete
        delay(100);

        // Ensure proper cleanup
        disconnect();
        delay(SCALE_RETIRE_MS);
        releaseRetiredScale();

        if (scanner != nullptr) {
            // Stop scanning first
            scanner->stopAsyncScan();
            // Give it time to actually stop
            delay(50);
            delete scanner;
            scanner = nullptr;
        }
    } catch (...) {
        // Swallow: destructors must not propagate exceptions.
        // NimBLE + Arduino delay() calls don't throw in practice; belt-and-braces.
    }
}

void BLEScalePlugin::setup(Controller *controller, PluginManager *manager) {
    if (controller == nullptr || manager == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Invalid controller or manager passed to setup");
        return;
    }

    this->controller = controller;
    this->pluginManager = manager;
    this->pluginRegistry = RemoteScalesPluginRegistry::getInstance();

    // Apply scale plugins with error checking
    AcaiaScalesPlugin::apply();
    BookooScalesPlugin::apply();
    DecentScalesPlugin::apply();
    DifluidScalesPlugin::apply();
    EclairScalesPlugin::apply();
    EurekaScalesPlugin::apply();
    FelicitaScalePlugin::apply();
    TimemoreScalesPlugin::apply();
    VariaScalesPlugin::apply();
    WeighMyBrewScalePlugin::apply();
    myscalePlugin::apply();
    TimemoreDotScalesPlugin::apply();

    // Initialize scanner with error handling
    this->scanner = new (std::nothrow) RemoteScalesScanner();
    if (this->scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Failed to create RemoteScalesScanner - out of memory");
        return;
    }

    manager->on("controller:bluetooth:connect", [this](Event const &) {
        if (this->controller != nullptr && this->controller->getMode() != MODE_STANDBY) {
            ESP_LOGI("BLEScalePlugin", "Resuming scanning");
            scan();
            active = true;
        }
    });
    manager->on("controller:bluetooth:disconnect", [this](Event const &) {
#ifdef WAVESHARE_ESP32_P4_7B
        // establishConnection() briefly drops the controller so the scale CCCD
        // subscribe can succeed on ESP-Hosted; do not tear the scale down then.
        if (this->controller != nullptr && this->controller->isScaleLinkPause()) {
            ESP_LOGI("BLEScalePlugin", "Controller disconnect treated as scale-link pause (keeping scale)");
            return;
        }
#endif
        ESP_LOGW("BLEScalePlugin", "Controller disconnect treated as real loss, stopping BLE scan");
        active = false;
    });
    manager->on("controller:brew:prestart", [this](Event const &) { onProcessStart(); });
    manager->on("controller:brew:end", [this](Event const &) {
        if (active && scale != nullptr && scale->isConnected() && scale->hasTimerControl()) {
            scale->stopTimer();
        }
    });
    manager->on("controller:grind:start", [this](Event const &) { onProcessStart(); });
    manager->on("controller:mode:change", [this](Event const &event) {
        if (event.getInt("value") != MODE_STANDBY) {
            ESP_LOGI("BLEScalePlugin", "Resuming scanning");
            scan();
            active = true;
            shutdownPending = false;
        } else {
            shutdownPending = true;
            active = false;
        }
    });
}

void BLEScalePlugin::loop() {
    releaseRetiredScale();
    if (forgetRequested.exchange(false) && controller != nullptr) {
        ESP_LOGI("BLEScalePlugin", "Forgetting saved scale");
        controller->getSettings().setSavedScale("");
        controller->getSettings().setSavedScaleName("");
        doConnect = false;
        disconnect();
        if (active) {
            scan();
        }
    }
    if (doConnect && scale == nullptr && retiringScale == nullptr) {
        establishConnection();
    }
    if (!active) {
        if (scale != nullptr) {
            // Entering standby powers the scale off too; a no-op for drivers without shutdown support.
            if (shutdownPending && scale->isConnected()) {
#if !defined(CONFIG_NIMBLE_CPP_IDF)
                // ble-scales v1: power the scale off. v2 (P4) has no shutdown(); disconnect is enough.
                scale->shutdown();
                delay(100);
#endif
            }
            disconnect();
        }
        shutdownPending = false;
        if (scanner != nullptr && scanner->isScanRunning()) {
            scanner->stopAsyncScan();
        }
    }
    const unsigned long now = millis();
    if (scanRequested.exchange(false)) {
        // No running scan (scale connected, scanner missing) closes the window right away.
        const bool scanning = scanner != nullptr && scanner->isScanRunning();
        scanWindowOpen = true;
        scanDeadline = now + (scanning ? SCAN_WINDOW_MS : 0);
    }
    if (scanWindowOpen && static_cast<long>(now - scanDeadline) >= 0) {
        emitScanComplete();
    }
    if (now - lastUpdate > UPDATE_INTERVAL_MS) {
        lastUpdate = now;
        update();
    }
}

void BLEScalePlugin::update() {
    // Graceful failure - if controller is null, just disable ourselves
    if (controller == nullptr) {
        ESP_LOGW("BLEScalePlugin", "Controller is null, disabling BLE scale");
        active = false;
        return;
    }

    bool hasConnectedScale = false;
    if (scale != nullptr) {
        // Check if scale pointer is valid before accessing
        hasConnectedScale = scale->isConnected();
    }

    if (!active)
        return;

    if (scale != nullptr) {
        if (!hasConnectedScale) {
            // Drivers reconnect inside update() with a blocking connect(); drop the scale and let the scan find it again
            // (GM-215).
            ESP_LOGW("BLEScalePlugin", "Scale connection lost, resuming scan");
            disconnect();
            if (scanner != nullptr) {
                scanner->initializeAsyncScan();
            }
            return;
        }
        scale->update();
        // Poll slow-changing metadata (battery, unit). Flow rate is
        // emitted inline with each weight measurement, not polled here.
        pollScaleMetadata();
    } else if (!doConnect && scanner != nullptr) {
        if (controller->getSettings().getSavedScale() != "") {
            auto discoveredScales = scanner->getDiscoveredScales();
            for (const auto &d : discoveredScales) {
                if (d.getAddress().toString() == controller->getSettings().getSavedScale().c_str()) {
                    ESP_LOGI("BLEScalePlugin", "Connecting to last known scale");
                    connect(d.getAddress().toString());
                    return;
                }
            }
        }
        maintainScan(millis());
    }
}

// Keeps the scale scan alive and bounded while no scale is connected.
void BLEScalePlugin::maintainScan(unsigned long now) {
    if (!scanner->isScanRunning()) {
        scanRunningSince = 0;
        scanner->initializeAsyncScan();
        return;
    }
    if (scanRunningSince == 0) {
        scanRunningSince = now;
        return;
    }
    // The NimBLE scan is shared: a client connect (EBUSY) or a host reset stops it behind the scanner's back.
    if (!NimBLEDevice::getScan()->isScanning()) {
        ESP_LOGW("BLEScalePlugin", "Scale scan stopped externally, restarting");
    } else if (scanWindowOpen || now - scanRunningSince < SCAN_REFRESH_MS) {
        return;
    }
    // stop() + clearResults() frees NimBLE's pending results, which otherwise grow for the whole session.
    scanner->restartAsyncScan();
    scanRunningSince = now;
}

void BLEScalePlugin::connect(const std::string &uuid) {
    if (uuid.empty()) {
        ESP_LOGE("BLEScalePlugin", "Cannot connect with empty UUID");
        return;
    }
    if (controller == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Controller is null, cannot save scale setting");
        return;
    }

    // Web UI / API connect from standby would otherwise be torn down immediately
    // (!active stops the scanner and disconnects in loop()).
    active = true;
    shutdownPending = false;
    doConnect = true;
    this->uuid = uuid;
    if (controller->getSettings().getSavedScale() != uuid.c_str()) {
        controller->getSettings().setSavedScaleName("");
    }
    controller->getSettings().setSavedScale(uuid.data());
}

void BLEScalePlugin::scan() {
    // Allow an explicit scan (web Settings) even while the machine is in standby.
    active = true;
    shutdownPending = false;
    scanRequested = true;
    if (scale != nullptr && scale->isConnected()) {
        return;
    }
    if (scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Scanner not initialized, cannot start scan");
        return;
    }
    scanner->initializeAsyncScan();
}

void BLEScalePlugin::forget() { forgetRequested = true; }

void BLEScalePlugin::emitScanComplete() {
    scanWindowOpen = false;
    if (pluginManager == nullptr) {
        return;
    }
    const int count = scanner != nullptr ? static_cast<int>(scanner->getDiscoveredScales().size()) : 0;
    pluginManager->trigger("scale:scan:complete", "count", count);
}

void BLEScalePlugin::emitConnectError(const std::string &address, const char *reason) {
    if (pluginManager == nullptr) {
        return;
    }
    Event event;
    event.id = "scale:connect:error";
    event.setString("address", String(address.c_str()));
    event.setString("reason", String(reason));
    pluginManager->trigger(event);
}

void BLEScalePlugin::releaseRetiredScale() {
    if (retiringScale == nullptr)
        return;
    if (millis() - retiringSince < SCALE_RETIRE_MS)
        return;
    ESP_LOGI("BLEScalePlugin", "Retired scale destroyed after %lu ms", millis() - retiringSince);
    retiringScale.reset();
}

void BLEScalePlugin::disconnect() {
    if (scale == nullptr)
        return;

    const String address = String(scale->getDeviceAddress().c_str());
    const String name = String(scale->getDeviceName().c_str());
    const bool notify = scale->isConnected() && pluginManager != nullptr;

    // Drop firmware callbacks first. NimBLE deleteClient() only starts GAP
    // disconnect while the link is up; notify lambdas still capture the driver.
    scale->setWeightUpdatedCallback(nullptr);
    scale->setLogCallback(nullptr);
    scale->disconnect();

    ESP_LOGI("BLEScalePlugin", "Retiring scale %s (%s)", name.c_str(), address.c_str());
    retiringSince = millis();
    retiringScale = std::move(scale);
    uuid = "";
    doConnect = false;
    lastBatteryLevel = REMOTE_SCALES_BATTERY_UNKNOWN;
    lastWeightUnit = ScaleWeightUnit::UNKNOWN;

    if (notify) {
        Event event;
        event.id = "scale:disconnect";
        event.setString("address", address);
        event.setString("name", name);
        pluginManager->trigger(event);
    }
}

void BLEScalePlugin::onProcessStart() const {
    if (scale != nullptr && scale->isConnected()) {
        // Double tare with validation
        scale->tare();
        delay(50);

        // Check if scale is still connected before second tare
        if (scale != nullptr && scale->isConnected()) {
            scale->tare();
        }
    }
}

void BLEScalePlugin::pollScaleMetadata() {
    if (scale == nullptr || !scale->isConnected() || pluginManager == nullptr) {
        return;
    }
    auto *pm = pluginManager;

    // Battery % -- fire event only on change so consumers can subscribe without
    // being hammered at 1 Hz with duplicate values.
    if (scale->hasBatteryLevel()) {
        const uint8_t pct = scale->getBatteryLevel();
        if (pct != lastBatteryLevel && pct != REMOTE_SCALES_BATTERY_UNKNOWN) {
            lastBatteryLevel = pct;
            pm->trigger("scale:battery:change", "value", static_cast<int>(pct));
        }
    }
}

void BLEScalePlugin::tare() const { onProcessStart(); }

void BLEScalePlugin::establishConnection() {
    doConnect = false;
    if (uuid.empty()) {
        ESP_LOGE("BLEScalePlugin", "Cannot establish connection with empty UUID");
        return;
    }

    ESP_LOGI("BLEScalePlugin", "Connecting to %s", uuid.c_str());
    if (scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Scanner not initialized, cannot establish connection");
        return;
    }

    // Connecting aborts the scan, so end the UI's scan window now instead of at its deadline.
    if (scanWindowOpen) {
        emitScanComplete();
    }
    scanner->stopAsyncScan();

    auto discoveredScales = scanner->getDiscoveredScales();
    bool deviceFound = false;

    for (const auto &d : discoveredScales) {
        if (d.getAddress().toString() == uuid) {
            deviceFound = true;

            auto factory = RemoteScalesFactory::getInstance();
            if (factory == nullptr) {
                ESP_LOGE("BLEScalePlugin", "RemoteScalesFactory instance is null");
                emitConnectError(uuid, "unsupported");
                return;
            }

            scale = factory->create(d);
            if (!scale) {
                ESP_LOGE("BLEScalePlugin", "Connection to device %s failed", d.getName().c_str());
                emitConnectError(uuid, "unsupported");
                return;
            }

            scale->setLogCallback([](std::string message) {
                if (!message.empty()) {
                    Serial.print(message.c_str());
                }
            });

            scale->setWeightUpdatedCallback([](float weight) {
                // Check if we're in an ISR context
                if (xPortInIsrContext()) {
                    // Skip measurement to avoid FreeRTOS deadlocks from interrupt context
                    return;
                }
                // Safe to call directly from task context with null check
                if (&BLEScales != nullptr) {
                    BLEScales.onMeasurement(weight);
                }
            });

#ifdef WAVESHARE_ESP32_P4_7B
            // ESP-Hosted: CCCD subscribe for the scale fails while the controller
            // link is busy (descriptor discovery / write returns EALREADY). Drop the
            // controller without rescanning — a scan during scale->connect() crashes.
            controller->setScaleLinkPause(true);
            if (controller->getClientController()->isConnected()) {
                ESP_LOGW("BLEScalePlugin", "Pausing controller BLE for scale subscribe");
                controller->getClientController()->disconnect(false);
                delay(300);
            }
            active = true;
            shutdownPending = false;
#endif

            bool connectResult = scale->connect();
#ifdef WAVESHARE_ESP32_P4_7B
            controller->setScaleLinkPause(false);
            ESP_LOGI("BLEScalePlugin", "Scale-link pause ended, subscribe=%s controller=%s", connectResult ? "ok" : "fail",
                     controller->getClientController()->isConnected() ? "up" : "down");
            if (!controller->getClientController()->isConnected()) {
                ESP_LOGI("BLEScalePlugin", "Resuming controller BLE scan");
                controller->getClientController()->scan();
            }
#endif
            if (!connectResult) {
                ESP_LOGW("BLEScalePlugin", "Failed to connect to scale, retrying scan");
                emitConnectError(uuid, "connect_failed");
                disconnect();
                if (scanner != nullptr) {
                    scanner->initializeAsyncScan();
                }
            } else {
                const String name = scale->getDeviceName().c_str();
                ESP_LOGI("BLEScalePlugin", "Scale subscribed: %s", name.c_str());
                if (controller->getSettings().getSavedScaleName() != name) {
                    controller->getSettings().setSavedScaleName(name);
                }
                if (pluginManager != nullptr) {
                    Event event;
                    event.id = "scale:connect:success";
                    event.setString("address", String(uuid.c_str()));
                    event.setString("name", name);
                    pluginManager->trigger(event);
                }
            }
            break;
        }
    }

    if (!deviceFound) {
        ESP_LOGW("BLEScalePlugin", "Device %s not found in discovered scales", uuid.c_str());
        emitConnectError(uuid, "not_found");
        if (scanner != nullptr) {
            scanner->initializeAsyncScan();
        }
    }
}

void BLEScalePlugin::onMeasurement(float value) {
    // Rate limiting to prevent callback flooding
    unsigned long now = millis();
    if (now - lastMeasurementTime < MIN_MEASUREMENT_INTERVAL_MS) {
        return; // Drop measurement to prevent flooding
    }
    lastMeasurementTime = now;

    // Multiple safety checks to prevent crashes
    if (controller == nullptr) {
        return; // Silently ignore if controller is null
    }

    // Check if we're being destroyed or in an unsafe state
    if (!active) {
        return; // Don't process measurements when not active
    }

    // Validate the measurement value
    if (!isfinite(value) || value < -1000.0f || value > 10000.0f) {
        ESP_LOGW("BLEScalePlugin", "Invalid measurement value: %f, ignoring", value);
        return;
    }

    // Safe to call controller method
    controller->onVolumetricMeasurement(value, VolumetricMeasurementSource::BLUETOOTH);

    // If the scale driver also provides native flow rate (e.g. Bookoo), emit
    // it on the same tick so consumers get it at the scale's native cadence
    // (~10 Hz) without having to poll. Controller.onVolumetricMeasurement
    // updates lastBluetoothMeasurement timestamps as a side effect; we reuse
    // a lighter path here since flow is not gating shot state.
    if (scale != nullptr && scale->hasFlowRate() && pluginManager != nullptr) {
        pluginManager->trigger("controller:volumetric-measurement:scale-flow:change", "value", scale->getFlowRate());
    }
}

std::vector<DiscoveredDevice> BLEScalePlugin::getDiscoveredScales() const {
    if (scanner == nullptr) {
        ESP_LOGW("BLEScalePlugin", "Scanner not initialized, returning empty device list");
        return std::vector<DiscoveredDevice>();
    }
    return scanner->getDiscoveredScales();
}
