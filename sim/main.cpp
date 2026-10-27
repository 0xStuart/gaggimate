// Desktop simulator entry point. Runs the real display Controller on a single
// cooperative loop on the main thread (LVGL/SDL must stay on the main thread on
// macOS), driving the firmware's loop methods directly since the FreeRTOS tasks
// are no-ops in the simulator.
#include "ESPAsyncWebServer.h"
#include "SdlDriver.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <display/core/Controller.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/ui/default/DefaultUI.h>

// The generated UI event handlers reference this global (see main.h on device).
Controller controller;

int main(int argc, char **argv) {
    // Optional: `--screenshot <path> [delayMs]` renders for a bit, saves a BMP, exits.
    const char *shotPath = nullptr;
    unsigned long shotDelayMs = 4000;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
            if (i + 1 < argc)
                shotDelayMs = strtoul(argv[i + 1], nullptr, 10);
        }
    }

    // Present as a machine already on Wi-Fi. Seed before setup() so:
    // - setupWifi() takes the STA path (the shim's begin() reports WL_CONNECTED)
    // - the island shows the clock and Wi-Fi icon, not captive-portal AP mode
    // - the right-half standby default is the clock, not Machine stats (that
    //   panel is for unconfigured / AP setup). Seed only once; persisted
    //   settings in sim_data/ / IndexedDB win on later runs.
    Settings &settings = controller.getSettings();
    if (settings.getWifiSsid().isEmpty())
        settings.setWifiSsid("GaggiMate-Sim");
    if (settings.getWifiPassword().isEmpty())
        settings.setWifiPassword("simulator");

    controller.setup(); // builds the UI, installs the SDL driver, marks screen ready

    SdlDriver *drv = SdlDriver::getInstance();
    DefaultUI *ui = controller.getUI();
    const unsigned long start = millis();
    bool shotTaken = false;

    while (!drv->shouldQuit()) {
        controller.loop();      // connection lifecycle, comms pump, plugins
        controller.loopLogic(); // process + control logic (normally a FreeRTOS task)

        // Shot history sampling normally runs in its own FreeRTOS task (a no-op in
        // the sim), so drive record() here at its native cadence.
        {
            static unsigned long lastShotSample = 0;
            if (millis() - lastShotSample >= SHOT_LOG_SAMPLE_INTERVAL_MS) {
                lastShotSample = millis();
                ShotHistory.record();
            }
        }

        if (ui) {
            ui->loop();
            ui->loopProfiles();
        }
        gm_web_pump(); // service the embedded WebUI HTTP/WS server

        // Settings persistence normally runs in a deferred save task (a no-op in the
        // sim), so flush dirty settings to NVS periodically. save(true) is a cheap
        // no-op when nothing changed.
        {
            static unsigned long lastSave = 0;
            if (millis() - lastSave >= 2000) {
                lastSave = millis();
                controller.getSettings().save(true);
            }
        }

        drv->pumpAndRender();

        if (shotPath && !shotTaken && millis() - start >= shotDelayMs) {
            drv->screenshot(shotPath);
            shotTaken = true;
            break;
        }
        delay(5);
    }
    return 0;
}
