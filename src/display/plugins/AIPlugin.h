#ifndef AI_PLUGIN_H
#define AI_PLUGIN_H

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <display/core/Controller.h>
#include <display/core/Plugin.h>
#include <display/core/PluginManager.h>
#include <display/core/Settings.h>

class AIPlugin : public Plugin {
  public:
    AIPlugin();
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

    void updateMessage();
    String getCurrentMessage() const { return currentMessage; }

  private:
    Controller *controller;
    PluginManager *pluginManager;
    Settings *settings;

    String currentMessage = "";
    unsigned long lastUpdate = 0;
    unsigned long lastCheck = 0;
    static const unsigned long CHECK_INTERVAL = 60000; // 1 minute

    bool isUpdateNeeded();
    void fetchAiMessage();
};

extern AIPlugin AI;

#endif // AI_PLUGIN_H
