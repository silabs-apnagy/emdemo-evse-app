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

#include "EvseConfig.h"
#include "SimulatedEv.h"

#include <EVSEManufacturerImpl.h>
#include <EnergyEvseMain.h>
#include <app-common/zap-generated/cluster-enums.h>
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

namespace {

constexpr uint8_t kEvConnectionLed = 1;
constexpr double kSimulationSecondsPerTick = 50.0;

SimulatedEv sSimulatedEv;
LEDWidget sConnectionLed;
TimerHandle_t sEvSimulationTimer = nullptr;
uint8_t sPreviousSoc              = 40;
bool sPreviousChargingState       = false;

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

    sSimulatedEv.ToggleConnected();
    const SimulatedEv::Snapshot snapshot = sSimulatedEv.GetSnapshot();
    sConnectionLed.Set(snapshot.connected);
    CustomerAppTask::GetAppTask().RequestDemoScreenRefresh();

    ChipLogProgress(AppServer, "SimEV %s", snapshot.connected ? "connected" : "disconnected");
}

void CustomerAppTask::OnEvSimTick()
{
    EVSEManufacturer * manufacturer = nullptr;
    chip::app::Clusters::EnergyEvse::EnergyEvseDelegate * evseDelegate = nullptr;
    bool chargingEnabled = false;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    manufacturer = GetEvseManufacturer();
    if (manufacturer != nullptr)
    {
        evseDelegate = manufacturer->GetEvseDelegate();
        if (evseDelegate != nullptr)
        {
            chargingEnabled = evseDelegate->GetSupplyState() == SupplyStateEnum::kChargingEnabled;
        }
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (manufacturer == nullptr || evseDelegate == nullptr)
    {
        ChipLogError(AppServer, "SimEV cannot access the EVSE delegate");
        return;
    }

    sSimulatedEv.SetEvseEnabled(chargingEnabled);
    sSimulatedEv.Tick(kSimulationSecondsPerTick);
    const SimulatedEv::Snapshot snapshot = sSimulatedEv.GetSnapshot();

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    if (!snapshot.connected)
    {
        evseDelegate->HwSetState(StateEnum::kNotPluggedIn);
    }
    else if (snapshot.charging)
    {
        evseDelegate->HwSetState(StateEnum::kPluggedInCharging);
    }
    else if (snapshot.demand)
    {
        evseDelegate->HwSetState(StateEnum::kPluggedInDemand);
    }
    else
    {
        evseDelegate->HwSetState(StateEnum::kPluggedInNoDemand);
    }

    if (auto * sensorManager = manufacturer->GetESManager())
    {
        const int64_t activePowerMilliwatts = static_cast<int64_t>(snapshot.activePowerW * 1000.0);
        const int64_t voltageMillivolts     = static_cast<int64_t>(snapshot.vrms * 1000.0);
        const int64_t activeCurrentMilliamps = static_cast<int64_t>(snapshot.rmsCurrentA * 1000.0);
        const int64_t importedEnergyMilliwattHours = static_cast<int64_t>(snapshot.energyWh * 1000.0);

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

    ChipLogProgress(AppServer, "SimEV: conn=%u demand=%u enabled=%u charging=%u soc=%u", snapshot.connected,
                    snapshot.demand, snapshot.evseEnabled, snapshot.charging, snapshot.socPercent);
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
