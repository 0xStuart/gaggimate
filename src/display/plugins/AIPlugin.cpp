#include "AIPlugin.h"
#include <WiFiClientSecure.h>
#include <display/core/Controller.h>
#include <esp_log.h>

const String AI_LOG_TAG = F("AIPlugin");

AIPlugin AI;

AIPlugin::AIPlugin() : controller(nullptr), pluginManager(nullptr), settings(nullptr) {}

void AIPlugin::setup(Controller *ctrl, PluginManager *pm) {
    this->controller = ctrl;
    this->pluginManager = pm;
    this->settings = &ctrl->getSettings();

    ESP_LOGI(AI_LOG_TAG.c_str(), "AI plugin initialized");
}

void AIPlugin::loop() {
    if (!settings->isAiEnabled()) {
        return;
    }

    unsigned long now = millis();

    // Check every minute
    if (now - lastCheck > CHECK_INTERVAL) {
        lastCheck = now;

        if (isUpdateNeeded()) {
            needsUpdate = true;
        }
    }

    if (needsUpdate && updateTaskHandle == nullptr) {
        needsUpdate = false;
        xTaskCreate(updateTask, "AIPlugin::update", 1024 * 16, this, 1, &updateTaskHandle);
    }
}

void AIPlugin::updateTask(void *arg) {
    auto *plugin = static_cast<AIPlugin *>(arg);
    plugin->fetchAiMessage();
    plugin->updateTaskHandle = nullptr;
    vTaskDelete(NULL);
}

bool AIPlugin::isUpdateNeeded() {
    // If we don't have a message yet, we need one
    if (currentMessage.isEmpty()) {
        return true;
    }

    time_t now;
    if (time(&now) < 1000000) {
        // Time not synced yet
        return false;
    }

    unsigned long intervalSeconds = (unsigned long)settings->getAiUpdateInterval() * 3600;

    if ((unsigned long)now - lastUpdate > intervalSeconds) {
        return true;
    }

    return false;
}

void AIPlugin::fetchAiMessage() {
    if (settings->getAiApiUrl().isEmpty() || settings->getAiApiKey().isEmpty()) {
        ESP_LOGW(AI_LOG_TAG.c_str(), "AI API URL or Key is missing");
        return;
    }

    ESP_LOGI(AI_LOG_TAG.c_str(), "Fetching new AI message from %s", settings->getAiApiUrl().c_str());

    WiFiClientSecure client;
    client.setInsecure(); // Use insecure mode for simplicity

    HTTPClient http;
    if (!http.begin(client, settings->getAiApiUrl())) {
        ESP_LOGE(AI_LOG_TAG.c_str(), "Unable to connect to AI API");
        return;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + settings->getAiApiKey());

    // Create JSON request body (OpenAI format)
    JsonDocument doc;
    doc["model"] = settings->getAiModel();
    JsonArray messages = doc["messages"].to<JsonArray>();
    JsonObject msg = messages.add<JsonObject>();
    msg["role"] = "user";
    msg["content"] = settings->getAiPrompt();

    String requestBody;
    serializeJson(doc, requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode == 200) {
        String response = http.getString();
        JsonDocument resDoc;
        DeserializationError error = deserializeJson(resDoc, response);

        if (!error) {
            const char *aiMessage = resDoc["choices"][0]["message"]["content"];
            if (aiMessage != nullptr) {
                String messageStr = String(aiMessage);
                messageStr.trim();

                // Simple markdown and newline cleanup
                messageStr.replace("**", "");
                messageStr.replace("*", "");
                messageStr.replace("\n", " ");
                messageStr.replace("\r", "");
                while (messageStr.indexOf("  ") != -1) {
                    messageStr.replace("  ", " ");
                }
                messageStr.trim();

                currentMessage = messageStr;

                time_t now;
                time(&now);
                lastUpdate = (unsigned long)now;

                ESP_LOGI(AI_LOG_TAG.c_str(), "New AI message received: %s", currentMessage.c_str());
                pluginManager->trigger("ai:message:new", "message", currentMessage);
            }
        } else {
            ESP_LOGE(AI_LOG_TAG.c_str(), "JSON deserialization failed: %s", error.c_str());
        }
    } else {
        ESP_LOGE(AI_LOG_TAG.c_str(), "HTTP POST failed, code: %d, response: %s", httpResponseCode, http.getString().c_str());
    }

    http.end();
}

void AIPlugin::updateMessage() { needsUpdate = true; }
