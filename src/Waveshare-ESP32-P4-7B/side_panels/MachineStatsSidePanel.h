#pragma once

#include "SidePanel.h"

namespace waveshare_esp32_p4_7b {

// Standby status: firmware versions, network / IP, and (in AP mode) a Wi‑Fi join QR.
class MachineStatsSidePanel : public SidePanel {
  public:
    const char *id() const override { return "stats"; }
    const char *name() const override { return "Machine"; }
    void create(lv_obj_t *parent, const SidePanelTheme &theme) override;
    void destroy() override;
    void update(const Telemetry &telemetry) override;

  private:
    void refresh();

    lv_obj_t *title = nullptr;
    lv_obj_t *displayVersion = nullptr;
    lv_obj_t *controllerVersion = nullptr;
    lv_obj_t *network = nullptr;
    lv_obj_t *ip = nullptr;
    lv_obj_t *apPassword = nullptr;
    lv_obj_t *qr = nullptr;
    lv_obj_t *qrHint = nullptr;

    bool lastAp = false;
    char lastDisplay[40] = "";
    char lastController[40] = "";
    char lastNetwork[48] = "";
    char lastIp[24] = "";
    char lastApPassword[64] = "";
    char lastQr[160] = "";
    uint32_t lastRefreshMs = 0;
};

} // namespace waveshare_esp32_p4_7b
