#ifndef ARDUINOOTAPLUGIN_H
#define ARDUINOOTAPLUGIN_H

#include "../core/Plugin.h"

struct Event;
class Settings;

// Fork-only: settings-gated LAN firmware push (PlatformIO espota). Off by default.
class ArduinoOTAPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

  private:
    void start();
    void stop();

    Controller *controller = nullptr;
    Settings *settings = nullptr;
    bool staConnected = false;
    bool running = false;
    bool otaInProgress = false;
};

#endif // ARDUINOOTAPLUGIN_H
