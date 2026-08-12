#include "SimulatedEv.h"

#include <cmath>
#include <cstdlib>

namespace {
constexpr double kJitterPercent = 0.03;

double Clamp(double value, double minimum, double maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}
} // namespace

void SimulatedEv::Init()
{
    mMaxPowerW = mSnapshot.vrms * mMaxCurrentA * mSnapshot.powerFactor;

    mSnapshot.connected   = false;
    mSnapshot.evseEnabled = false;
    mSnapshot.charging    = false;
    mSnapshot.socPercent  = 40;
    mSnapshot.energyWh    = (static_cast<double>(mSnapshot.socPercent) / 100.0) * (mBatteryKwh * 1000.0);
    mSnapshot.activePowerW = 0.0;
    mSnapshot.rmsCurrentA  = 0.0;

    RecomputeDerived();
}

void SimulatedEv::ToggleConnected()
{
    mSnapshot.connected = !mSnapshot.connected;

    if (!mSnapshot.connected)
    {
        mSnapshot.activePowerW = 0.0;
        mSnapshot.rmsCurrentA  = 0.0;
        ResetSocToDemoValue();
    }

    RecomputeDerived();
}

double SimulatedEv::GetRandomLowerSoc(double limit)
{
    limit = Clamp(limit, 0.0, static_cast<double>(mSnapshot.socPercent));
    const double randomFraction = static_cast<double>(std::rand()) / static_cast<double>(RAND_MAX);
    const double lowerBiasedFraction = std::pow(randomFraction, 2.0);
    return limit + lowerBiasedFraction * (static_cast<double>(mSnapshot.socPercent) - limit);
}

void SimulatedEv::ResetSocToDemoValue()
{
    mSnapshot.socPercent = static_cast<uint8_t>(GetRandomLowerSoc(15.0));
    mSnapshot.energyWh = (static_cast<double>(mSnapshot.socPercent) / 100.0) * (mBatteryKwh * 1000.0);
    RecomputeDerived();
}

void SimulatedEv::SetEvseEnabled(bool enabled)
{
    mSnapshot.evseEnabled = enabled;
    RecomputeDerived();
}

void SimulatedEv::Tick(double dtSeconds)
{
    if (dtSeconds <= 0.0)
    {
        return;
    }

    RecomputeDerived();

    double targetPowerW = 0.0;
    if (mSnapshot.connected && mSnapshot.demand && mSnapshot.evseEnabled)
    {
        double taper = 1.0;
        if (mSnapshot.socPercent >= 80)
        {
            const double progress = (static_cast<double>(mSnapshot.socPercent) - 80.0) / 20.0;
            taper                 = Clamp(1.0 - 0.8 * progress, 0.2, 1.0);
        }

        const double sign   = ((++mJitterCounter) & 1U) ? 1.0 : -1.0;
        const double jitter = 1.0 + sign * kJitterPercent;
        targetPowerW        = mMaxPowerW * taper * jitter;
    }

    const double maximumStep = mPowerSlewWPerSec * dtSeconds;
    double delta             = targetPowerW - mSnapshot.activePowerW;
    delta                    = Clamp(delta, -maximumStep, maximumStep);
    mSnapshot.activePowerW += delta;

    if (!mSnapshot.connected || mSnapshot.activePowerW <= 1.0)
    {
        mSnapshot.activePowerW = 0.0;
        mSnapshot.rmsCurrentA  = 0.0;
    }
    else
    {
        const double denominator = mSnapshot.vrms * mSnapshot.powerFactor;
        const double currentA    = denominator > 0.0 ? mSnapshot.activePowerW / denominator : 0.0;
        mSnapshot.rmsCurrentA    = Clamp(currentA, 0.0, mMaxCurrentA);
    }

    if (mSnapshot.connected && mSnapshot.evseEnabled && mSnapshot.activePowerW > 0.0)
    {
        mSnapshot.energyWh += (mSnapshot.activePowerW * dtSeconds) / 3600.0;

        const double capacityWh = mBatteryKwh * 1000.0;
        mSnapshot.energyWh      = Clamp(mSnapshot.energyWh, 0.0, capacityWh);
        const double soc        = capacityWh > 0.0 ? (mSnapshot.energyWh / capacityWh) * 100.0 : 0.0;
        mSnapshot.socPercent    = static_cast<uint8_t>(Clamp(soc, 0.0, 100.0) + 0.5);
    }

    RecomputeDerived();
}

void SimulatedEv::RecomputeDerived()
{
    mSnapshot.demand   = mSnapshot.connected && mSnapshot.socPercent < 100;
    mSnapshot.charging = mSnapshot.connected && mSnapshot.demand && mSnapshot.evseEnabled;
}
