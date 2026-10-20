/*
 * Copyright (c) 2020 Project CHIP Authors
 * Copyright (c) 2019 Google LLC.
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "CustomerAppTask.h"

#include <algorithm>

#include "EvseConfig.h"
#include "SimulatedEv.h"

#include <EVSEManufacturerImpl.h>
#include <EnergyEvseMain.h>
#include <app-common/zap-generated/cluster-enums.h>
#include <energy-evse-modes.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include "FreeRTOS.h"
#include "timers.h"

#ifdef DISPLAY_ENABLED
#include "demo-ui-bitmaps.h"

#include <cstdio>
#endif

using chip::app::Clusters::EnergyEvse::EVSEManufacturer;
using chip::app::Clusters::EnergyEvse::GetEvseManufacturer;
using chip::app::Clusters::EnergyEvse::StateEnum;
using chip::app::Clusters::EnergyEvse::SupplyStateEnum;
using EnergyEvseInstance = chip::app::Clusters::EnergyEvse::Instance;

namespace {

// One demo-loop minute is one real minute. Charging energy retains its
// separate acceleration below; the trip duration uses real RTOS elapsed time.
constexpr uint32_t kDriveDisconnectedSeconds = 4 * 60;
constexpr uint8_t kEvConnectionLed = 1;
constexpr double kSimulationSecondsPerTick = 60.0;
constexpr int64_t kDemoCircuitCapacitymA = 16000;
constexpr int64_t kDemoHardwareCurrentLimitmA = 16000;
constexpr int64_t kDemoCableCurrentLimitmA = 16000;
constexpr int64_t kDemoMainsVoltageMilliVolts = 230000;
constexpr uint8_t kDefaultTargetSocPercent = 80;
constexpr uint32_t kMinimumChargeCurrentmA = 6000;

bool sAutomaticDriveActive = false;
TickType_t sDriveStartedAt = 0;
SimulatedEv sSimulatedEv;
LEDWidget sConnectionLed;
TimerHandle_t sEvSimulationTimer = nullptr;
uint8_t sPreviousSoc              = 40;
bool sPreviousChargingState       = false;
bool sEvseLimitsInitialized       = false;
bool sReportedConnected          = false;
int64_t sPreviousRequiredEnergyMilliWh = -1;

#ifdef DISPLAY_ENABLED
constexpr uint8_t kHeaderLine             = 5;
constexpr uint8_t kProtocol1X              = 8;
constexpr uint8_t kProtocol2X              = 104;
constexpr uint8_t kProtocolY               = 74;

const uint8_t sSiliconLabsBitmap[] = { SILABS_BITMAP };
const uint8_t sThreadBitmap[]      = { THREAD_BITMAP };
const uint8_t sMatterBitmap[]      = { MATTER_LOGO_BITMAP };

void DrawEvseDemoUi(GLIB_Context_t * context)
{
    uint8_t line = kHeaderLine;
    char text[20];
    const SimulatedEv::Snapshot snapshot = sSimulatedEv.GetSnapshot();

    GLIB_clear(context);
    GLIB_drawBitmap(context, (context->pDisplayGeometry->xSize - SILICONLABS_BITMAP_WIDTH) / 2, 0,
                    SILICONLABS_BITMAP_WIDTH, SILICONLABS_BITMAP_HEIGHT, sSiliconLabsBitmap);
    GLIB_drawBitmap(context, kProtocol1X, kProtocolY, THREAD_BITMAP_WIDTH, THREAD_BITMAP_HEIGHT, sThreadBitmap);
    GLIB_drawBitmap(context, kProtocol2X, kProtocolY, MATTER_LOGO_WIDTH, MATTER_LOGO_HEIGHT, sMatterBitmap);

    GLIB_drawStringOnLine(context, "EVSE + SimEV", line++, GLIB_ALIGN_CENTER, 0, 0, true);
    ++line;
    GLIB_drawStringOnLine(context, "Connected", line++, GLIB_ALIGN_CENTER, 0, 0, true);
    GLIB_drawStringOnLine(context, snapshot.connected ? "Yes" : "No", line++, GLIB_ALIGN_CENTER, 0, 0, true);
    GLIB_drawStringOnLine(context, "Charging", line++, GLIB_ALIGN_CENTER, 0, 0, true);
    GLIB_drawStringOnLine(context, snapshot.charging ? "Yes" : "No", line++, GLIB_ALIGN_CENTER, 0, 0, true);
    std::snprintf(text, sizeof(text), "SoC: %u %%", static_cast<unsigned>(snapshot.socPercent));
    GLIB_drawStringOnLine(context, text, line, GLIB_ALIGN_CENTER, 0, 0, true);

    updateDisplay();
}
#endif

void EvSimulationTimerCallback(TimerHandle_t)
{
    AppEvent event{};
    event.Type    = AppEvent::kEventType_Timer;
    event.Handler = [](AppEvent *) { CustomerAppTask::GetAppTask().OnEvSimTick(); };
    CustomerAppTask::GetAppTask().PostEvent(&event);
}

} // namespace

CustomerAppTask CustomerAppTask::sAppTask;

AppTask & AppTask::GetAppTask()
{
    return CustomerAppTask::GetAppTask();
}

CHIP_ERROR CustomerAppTask::AppInitImpl()
{
    ReturnErrorOnFailure(AppTask::AppInit());

    sSimulatedEv.Init();
    sConnectionLed.Init(kEvConnectionLed);
    sConnectionLed.Set(false);

#ifdef DISPLAY_ENABLED
    GetLCD().SetCustomUI(DrawEvseDemoUi);
    RequestDemoScreenRefresh();
#endif

    if (sEvSimulationTimer == nullptr)
    {
        sEvSimulationTimer = xTimerCreate("SimEv", pdMS_TO_TICKS(1000), pdTRUE, nullptr, EvSimulationTimerCallback);
        VerifyOrReturnError(sEvSimulationTimer != nullptr, CHIP_ERROR_NO_MEMORY);
        VerifyOrReturnError(xTimerStart(sEvSimulationTimer, 0) == pdPASS, CHIP_ERROR_INTERNAL);
    }

    return CHIP_NO_ERROR;
}

void CustomerAppTask::EnergyManagementActionEventHandlerImpl(AppEvent * event)
{
    VerifyOrReturn(event != nullptr);

    if (event->Type != AppEvent::kEventType_Button)
    {
        ChipLogError(AppServer, "Unexpected EVSE action event type: %u", static_cast<unsigned>(event->Type));
        return;
    }

    // A deliberate connection change always wins and cancels a scheduled
    // return, so an old trip cannot toggle the vehicle again afterward.
    sAutomaticDriveActive = false;
    sSimulatedEv.ToggleConnected();
    const SimulatedEv::Snapshot snapshot = sSimulatedEv.GetSnapshot();
    sConnectionLed.Set(snapshot.connected);
    RequestDemoScreenRefresh();
    ChipLogProgress(AppServer, "SimEV manually %s", snapshot.connected ? "connected" : "disconnected");
   
}

void CustomerAppTask::OnEvSimTick()
{
    EVSEManufacturer * manufacturer = nullptr;
    chip::app::Clusters::EnergyEvse::EnergyEvseDelegate * evseDelegate = nullptr;
    EnergyEvseInstance * instance = nullptr;
    bool chargingEnabled = false;
    bool solarManagedMode = false;
    uint32_t maximumCurrentmA = kDemoCircuitCapacitymA;
    uint32_t minimumCurrentmA = kMinimumChargeCurrentmA;
    uint8_t targetSocPercent = kDefaultTargetSocPercent;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    manufacturer = GetEvseManufacturer();
    if (manufacturer != nullptr)
    {
        evseDelegate = manufacturer->GetEvseDelegate();
        if (evseDelegate != nullptr)
        {
            instance = evseDelegate->GetInstance();
            if (!sEvseLimitsInitialized && instance != nullptr)
            {
                evseDelegate->HwSetMaxHardwareChargeCurrentLimit(kDemoHardwareCurrentLimitmA);
                evseDelegate->HwSetCircuitCapacity(kDemoCircuitCapacitymA);
                evseDelegate->HwSetNominalMainsVoltage(kDemoMainsVoltageMilliVolts);
                evseDelegate->HwSetCableAssemblyLimit(kDemoCableCurrentLimitmA);
                // The SDK cluster starts UserMaximumChargeCurrent at 0, and the sample
                // delegate includes it in the effective-limit minimum. Seed the demo
                // preference once so an enabled EVSE is not permanently capped at 0 A.
                if (instance->GetUserMaximumChargeCurrent() == 0)
                {
                    TEMPORARY_RETURN_IGNORED instance->SetUserMaximumChargeCurrent(kDemoCircuitCapacitymA);
                }
                sEvseLimitsInitialized = true;
                ChipLogProgress(AppServer, "Configured SimEV supply limits: circuit/hardware/cable=%ld mA, mains=%ld mV",
                                static_cast<long>(kDemoCircuitCapacitymA), static_cast<long>(kDemoMainsVoltageMilliVolts));
            }
            chargingEnabled = evseDelegate->GetSupplyState() == SupplyStateEnum::kChargingEnabled;
            if (instance != nullptr)
            {
                auto * modeInstance = chip::app::Clusters::EnergyEvseMode::Instance();
                const uint8_t currentMode = modeInstance == nullptr ? chip::app::Clusters::EnergyEvseMode::kModeManual :
                    modeInstance->GetCurrentMode();
                solarManagedMode = currentMode == chip::app::Clusters::EnergyEvseMode::kModeSolarCharging ||
                    currentMode == chip::app::Clusters::EnergyEvseMode::kModeTimeOfUseAndSolarCharging;

                if (solarManagedMode)
                {
                    maximumCurrentmA = static_cast<uint32_t>(std::max<int64_t>(0, evseDelegate->GetMaximumChargeCurrent()));
                    minimumCurrentmA = static_cast<uint32_t>(std::max<int64_t>(kMinimumChargeCurrentmA,
                        evseDelegate->GetMinimumChargeCurrent()));
                }
                else
                {
                    // Manual mode ignores command and user current caps, but never the configured circuit ceiling.
                    const int64_t hardLimitmA = std::min({ evseDelegate->GetCircuitCapacity(),
                        evseDelegate->HwGetMaxHardwareChargeCurrentLimit(), evseDelegate->HwGetCableAssemblyLimit() });
                    maximumCurrentmA = static_cast<uint32_t>(std::max<int64_t>(0, hardLimitmA));
                    minimumCurrentmA = kMinimumChargeCurrentmA;
                }

                auto targetSoc = instance->GetNextChargeTargetSoC();
                if (targetSoc.IsNull())
                {
                    TEMPORARY_RETURN_IGNORED instance->SetNextChargeTargetSoC(chip::app::DataModel::MakeNullable(
                        static_cast<chip::Percent>(kDefaultTargetSocPercent)));
                }
                else
                {
                    targetSocPercent = static_cast<uint8_t>(targetSoc.Value());
                }
            }
        }
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (manufacturer == nullptr || evseDelegate == nullptr)
    {
        ChipLogError(AppServer, "SimEV cannot access the EVSE delegate");
        return;
    }

    const bool previouslyConnected = sSimulatedEv.GetSnapshot().connected;
    if (sAutomaticDriveActive &&
        static_cast<TickType_t>(xTaskGetTickCount() - sDriveStartedAt) >= pdMS_TO_TICKS(kDriveDisconnectedSeconds * 1000))
    {
        sAutomaticDriveActive = false;
        if (!sSimulatedEv.GetSnapshot().connected)
        {
            sSimulatedEv.ToggleConnected();
            ChipLogProgress(AppServer, "EV drive complete; SimEV reconnected");
        }
    }
    

    sSimulatedEv.SetEvseEnabled(chargingEnabled);
    sSimulatedEv.SetCurrentLimits(maximumCurrentmA, minimumCurrentmA);
    sSimulatedEv.SetTargetSocPercent(targetSocPercent);
    sSimulatedEv.Tick(kSimulationSecondsPerTick);
    SimulatedEv::Snapshot snapshot = sSimulatedEv.GetSnapshot();
    // Demand is based on exact battery energy and the configured target, not
    // rounded display SoC or a hard-coded 100-percent threshold.
    if (snapshot.connected && !snapshot.demand)
    {
        sSimulatedEv.ToggleConnected();
        snapshot = sSimulatedEv.GetSnapshot();
        sAutomaticDriveActive = true;
        sDriveStartedAt = xTaskGetTickCount();
        ChipLogProgress(AppServer, "EV reached target %u%%; disconnected for a %lu-second drive",
                        static_cast<unsigned>(targetSocPercent),
                        static_cast<unsigned long>(kDriveDisconnectedSeconds));
    }
    if (snapshot.connected != previouslyConnected)
    {
        sConnectionLed.Set(snapshot.connected);
        RequestDemoScreenRefresh();
    }

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    if (snapshot.connected && !sReportedConnected)
    {
        TEMPORARY_RETURN_IGNORED evseDelegate->HwSetCableAssemblyLimit(static_cast<int64_t>(snapshot.cableAssemblyLimitmA));
    }
    sReportedConnected = snapshot.connected;
    if (!snapshot.connected)
    {
        evseDelegate->HwSetState(StateEnum::kNotPluggedIn);
    } else {
        if (snapshot.demand)
        {
            evseDelegate->HwSetState(StateEnum::kPluggedInDemand);
        }
        else
        {
            evseDelegate->HwSetState(StateEnum::kPluggedInNoDemand);
        }
    }
    
    
    if(instance == nullptr){
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        ChipLogError(AppServer, "SimEV cannot access the EVSE instance");
        return;
    }

    TEMPORARY_RETURN_IGNORED instance->SetStateOfCharge(chip::app::DataModel::MakeNullable(
            static_cast<chip::Percent>(snapshot.socPercent)));
    TEMPORARY_RETURN_IGNORED instance->SetBatteryCapacity(chip::app::DataModel::MakeNullable(
            static_cast<int64_t>(snapshot.capacityWh * 1000.0)));
    if (snapshot.nextChargeRequiredEnergyMilliWh != sPreviousRequiredEnergyMilliWh)
    {
        TEMPORARY_RETURN_IGNORED instance->SetNextChargeRequiredEnergy(chip::app::DataModel::MakeNullable(
            snapshot.nextChargeRequiredEnergyMilliWh));
        sPreviousRequiredEnergyMilliWh = snapshot.nextChargeRequiredEnergyMilliWh;
    }


    if (auto * sensorManager = manufacturer->GetESManager())
    {
        const int64_t activePowerMilliwatts = static_cast<int64_t>(snapshot.activePowerW * 1000.0);
        const int64_t voltageMillivolts     = static_cast<int64_t>(snapshot.vrms * 1000.0);
        const int64_t activeCurrentMilliamps = static_cast<int64_t>(snapshot.rmsCurrentA * 1000.0);
        const int64_t importedEnergyMilliwattHours = static_cast<int64_t>(snapshot.cumulativeEnergyImportedMilliWh);
        
        
        (void) sensorManager->SendPowerReading(activePowerMilliwatts, voltageMillivolts, activeCurrentMilliamps);
        sensorManager->SetCumulativeEnergyImported(importedEnergyMilliwattHours);
        sensorManager->SetCumulativeEnergyExported(0);
        sensorManager->GenerateEEMReport();
        
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (sPreviousChargingState != snapshot.charging)
    {
        RequestDemoScreenRefresh();
        sPreviousChargingState = snapshot.charging;
    }

    if (snapshot.socPercent < sPreviousSoc)
    {
        sPreviousSoc = snapshot.socPercent;
    }
    else if (snapshot.socPercent - sPreviousSoc > 2)
    {
        RequestDemoScreenRefresh();
        sPreviousSoc = snapshot.socPercent;
    }

    //ChipLogProgress(AppServer, "SimEV: conn=%u demand=%u enabled=%u charging=%u soc=%u", snapshot.connected,
    //               snapshot.demand, snapshot.evseEnabled, snapshot.charging, snapshot.socPercent);
}

void CustomerAppTask::RequestDemoScreenRefresh()
{
#ifdef DISPLAY_ENABLED
    SilabsLCD::Screen_e screen;
    GetLCD().GetScreen(screen);
    if (screen == SilabsLCD::Screen_e::DemoScreen)
    {
        BaseApplication::PostUpdateDisplayEvent(SilabsLCD::Screen_e::DemoScreen);
    }
#endif
}
