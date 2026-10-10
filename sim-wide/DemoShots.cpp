#include "DemoShots.h"

#include <LittleFS.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <display/core/zones.h>
#include <display/main.h>
#include <display/models/shot_log_format.h>
#include <vector>

namespace {

constexpr float DT = SHOT_LOG_SAMPLE_INTERVAL_MS / 1000.0f;
constexpr float PRESSURE_TAU = 0.6f;     // s, how fast the pump reaches a pressure target
constexpr float DRIP_VOLUME = 5.0f;      // ml through the puck before the first drops reach the cup
constexpr float WEIGHT_SMOOTHING = 0.5f; // scale lag
constexpr uint16_t ACTIVE_SCALE_INFO = SYSTEM_INFO_SHOT_STARTED_VOLUMETRIC | SYSTEM_INFO_CURRENTLY_VOLUMETRIC |
                                       SYSTEM_INFO_BLUETOOTH_SCALE_CONNECTED | SYSTEM_INFO_VOLUMETRIC_AVAILABLE |
                                       SYSTEM_INFO_PROCESS_IS_BREW | SYSTEM_INFO_TARGET_IS_VOLUMETRIC |
                                       SYSTEM_INFO_ACTIVE_SCALE_CONNECTED;

enum class Control { Pressure, Flow, Bloom };

struct Phase {
    const char *name;
    Control control;
    float from;    // bar or ml/s at the start of the phase
    float to;      // ... at its end (linear in between)
    float seconds; // 0 = until the end of the shot
};

struct DemoShot {
    const char *profileId;
    const char *profileName;
    int daysAgo; // local time of day, moved back a day if it is still to come
    int hour;
    int minute;
    float seconds;
    float targetTemp;
    float resistance;    // bar per ml/s at the start; the puck erodes to ~65 % of it
    float pressureLimit; // for flow phases
    float dose;
    float yield;
    uint8_t rating;
    const char *bean;
    const char *grind;
    const char *taste;
    const char *notes;
    std::vector<Phase> phases;
};

const std::vector<DemoShot> &demoShots() {
    static const std::vector<DemoShot> shots = {
        {"demo-classic",
         "Classic 9 bar",
         3,
         7,
         48,
         29.5f,
         93.0f,
         5.2f,
         9.0f,
         18.0f,
         36.4f,
         4,
         "Ethiopia Yirgacheffe",
         "12",
         "balanced",
         "Bright, jammy, a little sharp at the end.",
         {{"Preinfusion", Control::Pressure, 3, 3, 7},
          {"Ramp", Control::Pressure, 3, 9, 3},
          {"Extraction", Control::Pressure, 9, 9, 0}}},
        {"demo-turbo",
         "Turbo",
         2,
         8,
         5,
         15.8f,
         90.0f,
         1.2f,
         6.0f,
         18.0f,
         45.2f,
         3,
         "Ethiopia Yirgacheffe",
         "18",
         "sour",
         "Fast and juicy, needs a touch finer.",
         {{"Fill", Control::Flow, 4, 4, 4}, {"Extraction", Control::Flow, 4.5f, 4.5f, 0}}},
        {"demo-lever",
         "Lever decline",
         1,
         7,
         52,
         34.0f,
         92.0f,
         5.6f,
         9.0f,
         18.0f,
         38.1f,
         5,
         "Brazil Cerrado",
         "11",
         "balanced",
         "Chocolate and hazelnut, syrupy body.",
         {{"Preinfusion", Control::Pressure, 3, 3, 8},
          {"Peak", Control::Pressure, 3, 9, 2},
          {"Decline", Control::Pressure, 9, 5, 0}}},
        {"demo-bloom",
         "Blooming espresso",
         1,
         14,
         30,
         38.5f,
         94.0f,
         4.0f,
         9.0f,
         18.0f,
         40.0f,
         4,
         "Kenya AA",
         "10",
         "balanced",
         "Blackcurrant, very clear.",
         {{"Fill", Control::Pressure, 3, 3, 6},
          {"Bloom", Control::Bloom, 0, 0, 8},
          {"Extraction", Control::Flow, 2.2f, 2.2f, 0}}},
        {"demo-classic",
         "Classic 9 bar",
         0,
         7,
         40,
         27.2f,
         93.0f,
         5.0f,
         9.0f,
         18.5f,
         37.0f,
         0,
         "Brazil Cerrado",
         "12",
         "",
         "",
         {{"Preinfusion", Control::Pressure, 3, 3, 7},
          {"Ramp", Control::Pressure, 3, 9, 3},
          {"Extraction", Control::Pressure, 9, 9, 0}}},
    };
    return shots;
}

uint16_t scaled(float value, float scale) { return static_cast<uint16_t>(std::max(0.0f, std::round(value * scale))); }
int16_t scaledSigned(float value, float scale) { return static_cast<int16_t>(std::round(value * scale)); }

// Small repeatable wobble so the lines do not look machine-drawn.
float jitter(uint32_t &seed, float amount) {
    seed = seed * 1664525u + 1013904223u;
    return ((seed >> 8) / 16777216.0f - 0.5f) * 2.0f * amount;
}

struct Recorded {
    ShotLogHeader header{};
    std::vector<ShotLogSample> samples;
};

Recorded simulate(const DemoShot &shot, uint32_t startEpoch) {
    Recorded out;
    ShotLogHeader &h = out.header;
    h.magic = SHOT_LOG_MAGIC;
    h.version = SHOT_LOG_VERSION;
    h.reserved0 = SHOT_LOG_SAMPLE_SIZE;
    h.headerSize = SHOT_LOG_HEADER_SIZE;
    h.sampleInterval = SHOT_LOG_SAMPLE_INTERVAL_MS;
    h.fieldsMask = SHOT_LOG_FIELDS_MASK_ALL;
    h.startEpoch = startEpoch;
    strncpy(h.profileId, shot.profileId, sizeof(h.profileId) - 1);
    strncpy(h.profileName, shot.profileName, sizeof(h.profileName) - 1);
    h.finalExitReason = PHASE_EXIT_REASON_TARGET_VOLUMETRIC;

    const size_t count = static_cast<size_t>(shot.seconds / DT) + 1;
    uint32_t seed = startEpoch;
    float pressure = 0.0f, puckVolume = 0.0f, rawWeight = 0.0f, shownWeight = 0.0f, pumped = 0.0f;
    std::vector<float> weights;
    size_t phase = 0;
    float phaseStart = 0.0f;

    for (size_t i = 0; i < count; i++) {
        const float t = i * DT;
        while (phase + 1 < shot.phases.size() && shot.phases[phase].seconds > 0 && t >= phaseStart + shot.phases[phase].seconds) {
            phaseStart += shot.phases[phase].seconds;
            phase++;
        }
        const Phase &p = shot.phases[phase];
        if (h.phaseTransitionCount == phase && phase < 12) {
            PhaseTransition &tr = h.phaseTransitions[h.phaseTransitionCount++];
            tr.sampleIndex = static_cast<uint16_t>(i);
            tr.phaseNumber = static_cast<uint8_t>(phase);
            tr.transitionReason = phase == 0 ? PHASE_EXIT_REASON_NONE : PHASE_EXIT_REASON_DURATION;
            strncpy(tr.phaseName, p.name, sizeof(tr.phaseName) - 1);
        }
        const float length = p.seconds > 0 ? p.seconds : shot.seconds - phaseStart;
        const float progress = std::min(1.0f, (t - phaseStart) / std::max(length, DT));
        const float target = p.from + (p.to - p.from) * progress;
        const float resistance = shot.resistance * (1.25f - 0.6f * t / shot.seconds);

        float wanted;
        switch (p.control) {
        case Control::Pressure:
            wanted = target;
            break;
        case Control::Flow:
            wanted = std::min(shot.pressureLimit, target * resistance + 0.5f);
            break;
        default:
            wanted = 0.8f; // pump off, the puck soaks and pressure bleeds away
            break;
        }
        const float lag = p.control == Control::Bloom ? 4.0f : PRESSURE_TAU;
        pressure += (wanted - pressure) * (1.0f - std::exp(-DT / lag));
        const float shown = std::max(0.0f, pressure + jitter(seed, 0.08f));

        const float puckFlow = std::max(0.0f, (pressure - 0.5f) / resistance) + jitter(seed, 0.04f);
        const float headspace = t < 1.5f ? 5.0f : 0.0f; // filling above the puck
        const float pumpFlow = p.control == Control::Bloom ? 0.0f : puckFlow + headspace;
        puckVolume += std::max(0.0f, puckFlow) * DT;
        pumped += pumpFlow * DT;
        if (puckVolume > DRIP_VOLUME)
            rawWeight += std::max(0.0f, puckFlow) * DT * 0.92f;
        shownWeight += (rawWeight - shownWeight) * WEIGHT_SMOOTHING;
        weights.push_back(shownWeight);

        ShotLogSample s{};
        s.t = static_cast<uint32_t>(std::lround(t * 1000.0f));
        s.tt = scaled(shot.targetTemp, 10);
        s.ct = scaled(shot.targetTemp - 1.4f * std::sin(std::min(t / 12.0f, 1.0f) * 3.14159f) + jitter(seed, 0.12f), 10);
        s.tp = p.control == Control::Pressure ? scaled(target, 10) : 0;
        s.cp = scaled(shown, 10);
        s.fl = scaledSigned(pumpFlow, 100);
        s.tf = p.control == Control::Flow ? scaledSigned(target, 100) : 0;
        s.pf = scaledSigned(std::max(0.0f, puckFlow), 100);
        s.pr = scaled(resistance, 100);
        s.si = ACTIVE_SCALE_INFO;
        s.wp = scaled(pumped, 10);
        out.samples.push_back(s);
    }

    // Stretch the recorded weight so the cup ends exactly at the shot's yield.
    const float factor = weights.back() > 0.0f ? shot.yield / weights.back() : 0.0f;
    for (size_t i = 0; i < out.samples.size(); i++) {
        const float w = weights[i] * factor;
        out.samples[i].v = scaled(w, 10);
        out.samples[i].ev = scaled(w, 10);
        const float previous = i > 0 ? weights[i - 1] * factor : 0.0f;
        out.samples[i].vf = scaledSigned((w - previous) / DT, 100);
    }
    h.sampleCount = static_cast<uint32_t>(out.samples.size());
    h.durationMs = out.samples.back().t;
    h.finalWeight = scaled(shot.yield, 10);
    return out;
}

bool writeFile(const char *path, const void *data, size_t size) {
    File file = LittleFS.open(path, FILE_WRITE);
    if (!file)
        return false;
    const bool ok = file.write(static_cast<const uint8_t *>(data), size) == size;
    file.close();
    return ok;
}

} // namespace

void seedDemoShots() {
    Settings &settings = controller.getSettings();
    if (LittleFS.exists("/h/index.bin") || settings.getHistoryIndex() != 0)
        return;

    // Same zone the firmware applies once Wi-Fi is up, so the times of day come out as written.
    setenv("TZ", resolve_timezone(settings.getTimezone()), 1);
    tzset();

    const auto &shots = demoShots();
    const time_t now = time(nullptr);
    std::vector<std::pair<time_t, const DemoShot *>> schedule;
    for (const DemoShot &shot : shots) {
        tm local{};
        localtime_r(&now, &local);
        local.tm_mday -= shot.daysAgo;
        local.tm_hour = shot.hour;
        local.tm_min = shot.minute;
        local.tm_sec = 0;
        local.tm_isdst = -1;
        time_t at = mktime(&local);
        while (at > now - 10 * 60)
            at -= 24 * 60 * 60;
        schedule.emplace_back(at, &shot);
    }
    std::sort(schedule.begin(), schedule.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    ShotIndexHeader indexHeader{};
    indexHeader.magic = SHOT_INDEX_MAGIC;
    indexHeader.version = SHOT_INDEX_VERSION;
    indexHeader.entrySize = SHOT_INDEX_ENTRY_SIZE;
    std::vector<ShotIndexEntry> entries;

    for (uint32_t id = 0; id < schedule.size(); id++) {
        const DemoShot &shot = *schedule[id].second;
        const Recorded rec = simulate(shot, static_cast<uint32_t>(schedule[id].first));

        char path[32];
        snprintf(path, sizeof(path), "/h/%06u.slog", static_cast<unsigned>(id));
        std::vector<uint8_t> log(sizeof(rec.header) + rec.samples.size() * sizeof(ShotLogSample));
        memcpy(log.data(), &rec.header, sizeof(rec.header));
        memcpy(log.data() + sizeof(rec.header), rec.samples.data(), rec.samples.size() * sizeof(ShotLogSample));
        if (!writeFile(path, log.data(), log.size()))
            return;

        char notes[512];
        const int length = snprintf(notes, sizeof(notes),
                                    "{\"id\":\"%u\",\"rating\":%u,\"beanType\":\"%s\",\"doseIn\":\"%.1f\",\"doseOut\":\"%.1f\","
                                    "\"ratio\":\"%.2f\",\"grindSetting\":\"%s\",\"balanceTaste\":\"%s\",\"notes\":\"%s\"}",
                                    static_cast<unsigned>(id), shot.rating, shot.bean, shot.dose, shot.yield,
                                    shot.yield / shot.dose, shot.grind, shot.taste, shot.notes);
        snprintf(path, sizeof(path), "/h/%06u.json", static_cast<unsigned>(id));
        writeFile(path, notes, static_cast<size_t>(length));

        uint32_t tempSum = 0, flowSum = 0, flowCount = 0;
        uint16_t maxPressure = 0;
        for (const ShotLogSample &s : rec.samples) {
            tempSum += s.ct;
            maxPressure = std::max(maxPressure, s.cp);
            if (s.pf > 0) {
                flowSum += s.pf;
                flowCount++;
            }
        }
        ShotIndexEntry entry{};
        entry.id = id;
        entry.timestamp = rec.header.startEpoch;
        entry.duration = rec.header.durationMs;
        entry.volume = rec.header.finalWeight;
        entry.rating = shot.rating;
        entry.flags = SHOT_FLAG_COMPLETED | SHOT_FLAG_HAS_NOTES;
        strncpy(entry.profileId, shot.profileId, sizeof(entry.profileId) - 1);
        strncpy(entry.profileName, shot.profileName, sizeof(entry.profileName) - 1);
        entry.avgTemp = static_cast<uint16_t>(tempSum / rec.samples.size());
        entry.maxPressure = maxPressure;
        entry.avgFlow = static_cast<uint16_t>(flowCount ? flowSum / flowCount : 0);
        entries.push_back(entry);
    }

    indexHeader.entryCount = static_cast<uint32_t>(entries.size());
    indexHeader.nextId = static_cast<uint32_t>(entries.size());
    std::vector<uint8_t> index(sizeof(indexHeader) + entries.size() * sizeof(ShotIndexEntry));
    memcpy(index.data(), &indexHeader, sizeof(indexHeader));
    memcpy(index.data() + sizeof(indexHeader), entries.data(), entries.size() * sizeof(ShotIndexEntry));
    if (writeFile("/h/index.bin", index.data(), index.size()))
        settings.setHistoryIndex(static_cast<int>(entries.size()));
}
