#pragma once

#include <cstdint>

// Pure EV simulation model. Matter integration and attribute reporting remain
// in CustomerAppTask so this class can be tested independently.
class SimulatedEv
{
public:
    struct Snapshot
    {
        bool connected   = false;
        bool demand      = false;
        bool evseEnabled = false;
        bool charging    = false;

        uint8_t socPercent = 0;

        double vrms         = 230.0;
        double powerFactor  = 0.98;
        double activePowerW = 0.0;
        double rmsCurrentA  = 0.0;
        double energyWh     = 0.0;
    };

    void Init();
    void Tick(double dtSeconds);

    void ToggleConnected();
    void ResetSocToDemoValue();
    double GetRandomLowerSoc(double limit);
    void SetEvseEnabled(bool enabled);

    Snapshot GetSnapshot() const { return mSnapshot; }

private:
    void RecomputeDerived();

    Snapshot mSnapshot;
    double mBatteryKwh       = 60.0;
    double mMaxCurrentA      = 16.0;
    double mMaxPowerW        = 0.0;
    double mPowerSlewWPerSec = 500.0;
    uint32_t mJitterCounter  = 0;
};
