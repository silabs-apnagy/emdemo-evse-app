# EVSE Demo 2.0 Requirements

Status: inspection completed; source code has not been changed.

This document defines the EVSE application work required for Energy Management Demo 2.0.

## Current implementation findings

- `SimulatedEv::energyWh` is the simulated vehicle battery energy and is derived from SoC.
- `CustomerAppTask::OnEvSimTick()` publishes that battery energy as Electrical Energy Measurement `CumulativeEnergyImported`.
- This makes cumulative imported energy behave like battery SoC, including the approximately 65 kWh cap and random reset behavior. These are different quantities and must be separated.
- The Energy EVSE ZAP configuration includes `StateOfCharge` as an external, nullable `percent` attribute at attribute ID `0x0030` (decimal 48), but the application never updates it; the deployed device consequently reports null.
- The EVSE state is updated from the simulation once per timer tick through `EnergyEvseDelegate::HwSetState()`.
- Controller enable/disable is sampled from `GetSupplyState()`, then passed to `SimulatedEv::SetEvseEnabled()`.
- The connection button deliberately represents the physical connection between SimEV and the EVSE. Disconnecting represents the EV being driven and reconnecting represents returning with a lower SoC; the random lower-SoC reset is required demo behavior.
- At full SoC, `demand` becomes false, but the charging-energy accumulator can still advance while residual active power is ramping down. The charging predicate and energy accounting need to be made consistent.
- The README still describes BTN1 behavior as not implemented, although the custom app now toggles the simulated vehicle.

## Required data model separation

- Keep battery state in `SimulatedEv`: battery capacity, battery energy, and SoC.
- Add a separate monotonic cumulative imported-energy value for energy delivered by the EVSE. It must not reset when the vehicle disconnects or when charging is disabled.
- Define cumulative imported energy as demo-boot lifetime energy: initialize it at zero on each device boot, then preserve it across charging sessions, BTN1 drive cycles, reconnects, and controller enable/disable changes until the next reboot.
- Increment cumulative imported energy only for actual positive charging power and elapsed time. Do not increment it merely because charging is enabled or while power is ramping down after charging stops.
- Keep exported energy at zero for this non-V2X simulation. Do not use exported energy as a SoC substitute.
- Use consistent units and conversion at the API boundary: the Electrical Energy Measurement delegate expects milli-watt-hours; the EV model may continue using watt-hours internally.

## Electrical Energy Measurement compliance

- Confirm the Electrical Energy Measurement endpoint feature map has `ImportedEnergy` (bit 0) and `CumulativeEnergy` (bit 2) enabled. `CumulativeEnergyImported` is an `EnergyMeasurementStruct`, not a plain scalar; its `Energy` field is expressed in milli-watt-hours.
- Treat cumulative imported energy as a lifetime odometer for energy delivered from the EVSE supply to the vehicle. It must only increase or remain constant while charging is idle.
- Never reset cumulative imported energy when BTN1 starts a simulated drive, when the vehicle reconnects, when charging permission changes, or when a new charging session begins.
- Keep session-specific energy separate. If the EVSE sample exposes `SessionEnergyCharged`, derive it from the difference between the cumulative imported meter at session start and the current cumulative meter, or use the SDK’s session-energy mechanism. Do not reset the lifetime register to implement a session counter.
- Treat any attempted decrease as an error. Log the previous and proposed values and reject, clamp, or explicitly mark the reading invalid rather than silently publishing a lower lifetime value.
- Keep the demo-boot counter in application/runtime state; NVM persistence across reboot is explicitly out of scope for this demo requirement.
- Do not restore a previous imported-energy value from NVM at startup. The first valid post-boot value must be zero or the accumulated value from the current boot only.
- Add a monotonicity guard at both the simulator and Matter delegate boundaries so future changes cannot accidentally map battery energy or a reset session value into the boot-lifetime register.
- Do not implement rollover as an unannounced reset to zero. A zero after a large value violates the monotonic contract expected by the dashboard. Use the widest supported signed range, detect saturation/overflow, log an explicit fault, and define a maintenance/reset procedure separately if rollover must eventually be supported.

## Cumulative energy snapshots and events

- Preserve the existing `GenerateEEMReport()` path, but ensure it publishes the corrected cumulative imported value and zero exported value.
- The Matter Electrical Energy Measurement cluster defines `CumulativeEnergyMeasured` as the snapshot event for cumulative imported/exported energy. Generate it when a cumulative snapshot is taken, subject to the cluster’s reporting cadence; do not assume that every internal simulation tick must create an event.
- Populate the `EnergyMeasurementStruct` timestamps when the configured SDK path supports them. At minimum, use a valid epoch sample time for the snapshot; define whether `StartTimestamp` represents meter lifetime/start-of-counter time and `EndTimestamp` represents the current sample time.
- Ensure event and attribute values use the same counter snapshot so controllers cannot observe an event value different from the current cumulative attribute.
- Generate a snapshot immediately or at the next valid reporting point on meaningful transitions: charging start, charging stop, vehicle disconnect/reconnect, and boot-time counter initialization. Respect the SDK’s minimum reporting cadence.
- Confirm whether `GenerateSnapshots()` emits the event and updates the readable attributes for the selected SDK version; document the result rather than relying on the method name alone.
- Keep cumulative imported and cumulative exported registers independent. For this demo the exported register must remain `0` and must not be derived from, subtracted from, or reused for battery SoC.

## SoC and energy-accounting relationship

- Continue using the battery-energy model internally to calculate SoC; this is vehicle battery state, not grid energy metering.
- On each tick, calculate two independent deltas: battery energy added to the vehicle and supply energy imported by the EVSE. For this simplified demo they may use the same charging power integral, but they must be stored and exposed as different quantities.
- BTN1’s reconnect reset changes only the simulated battery SoC/battery energy. It must not change cumulative imported energy, cumulative exported energy, or the lifetime meter timestamps.
- While disconnected for the simulated drive, SoC remains at the pre-drive value until the reconnect reset; imported/exported energy remain unchanged; the cumulative snapshot may still be emitted for the state transition with the same counter value.
- Controller disable/enable changes whether new charging energy is accumulated, but never changes either lifetime counter directly.

## Required SoC publication

- Confirm the Energy EVSE endpoint has the SoC Reporting feature enabled in ZAP and regenerate after any ZAP change. Do not fabricate a dashboard-only value.
- On initialization, connection/reset, and every simulation tick where SoC changes, publish the simulated SoC through the generated Energy EVSE instance:
  - obtain `EnergyEvseDelegate` from `GetEvseManufacturer()->GetEvseDelegate()`;
  - obtain its `EnergyEvse::Instance` with `GetInstance()`;
  - call `SetStateOfCharge(DataModel::MakeNullable(static_cast<Percent>(socPercent)))`;
  - handle and log the returned `CHIP_ERROR`.
- Publish a nullable value only when the simulated vehicle/SoC is intentionally unknown. Do not publish null during controller disable or the deliberate BTN1 drive cycle; publish the pre-drive SoC while disconnected, then publish the lower reset SoC after reconnection.
- Ensure attribute notifications are generated by the instance setter, so Matter subscriptions and the dashboard receive updates.
- Consider publishing battery capacity through `SetBatteryCapacity()` as a consistent companion value.
- Verify with Matter read and subscription commands that SoC is non-null, in the 0–100 percent range, and changes while charging.

## Charging/state-machine requirements

- Treat these as separate state variables: vehicle connected, EVSE charging permission, vehicle demand, actual charging power, EVSE state, and SoC.
- Controller enable/disable must immediately affect simulated charging power while the vehicle remains connected.
- Disabling charging must stop SoC growth and bring actual power/current to zero through the existing slew behavior; it must not alter SoC.
- Re-enabling charging must resume from the same SoC and continue cumulative imported-energy accounting.
- Repeated enable/disable commands must be idempotent and must not reset the battery model or create energy increments without power.
- Disconnecting the simulated vehicle must force actual power/current to zero and set EVSE state to `NotPluggedIn`; it must retain the current SoC while disconnected until the simulated drive completes.
- Reconnecting must perform the intentional drive-return reset to a lower SoC, then restore a coherent demand/charging decision from that new SoC and the current EVSE permission.
- Keep BTN1 as the physical SimEV/EVSE connection control. Do not replace it with a separate reset action; any reset helper or CLI command must use the same drive-cycle semantics.
- Ensure the drive-cycle reset occurs exactly once per disconnect/reconnect cycle, not on every simulation tick or repeated button event.
- At SoC 100%, demand and charging must be false, power must settle to zero, and neither battery energy nor cumulative imported energy may continue increasing.
- Define the behavior at the 80% demo threshold. The current simulator tapers only above 80% but continues toward 100%; this must agree with the dashboard’s target policy.
- During the disconnected drive interval, SoC and cumulative imported energy must remain stable, actual charging power/current must remain zero, and EVSE state must remain `NotPluggedIn`.
- Hold the EV disconnected long enough for Matter reports to settle before reconnecting. The default demo cycle should support approximately two seconds disconnected, followed by a configurable connected/charging interval.
- Run all state transitions on the Matter/application task context or otherwise protect shared state. Avoid reading delegate state outside the appropriate Matter stack lock.

## EVSE/Matter integration

- Keep application-specific behavior in `CustomerAppTask` and `SimulatedEv`; do not edit generated `autogen` sources or shared SDK delegate implementations unless the SDK API makes it unavoidable.
- Confirm whether the current Silicon Labs EVSE delegate already exposes the generated `EnergyEvse::Instance` through `GetInstance()`. The connectedhomeip EVSE sample uses this path for `SetStateOfCharge()`.
- Preserve the existing `HwSetState()` mapping, but verify that `PluggedInDemand`, `PluggedInCharging`, and `PluggedInNoDemand` reflect the same snapshot used for power and SoC reporting.
- Decide whether controller writes to `ChargingEnabledUntil` or other timed controls must be supported, or whether the demo intentionally supports only the current supply-state enable/disable path.

## Deterministic demo behavior

- Replace the implicit `std::rand()` reset dependency with a repeatable seed/reset option, while retaining a lower-biased random mode if desired for the visual demo.
- Guarantee reset values are below the dashboard target and above the configured minimum.
- Add a reusable SimEV drive-cycle operation with explicit phases: connected, disconnect/drive, and reconnect with lower-SoC reset.
- Add an optional custom CLI command such as `custom ev-drive-timer <seconds>` to start or configure the drive cycle. Define whether the argument means the connected interval, the complete cycle interval, or the time until the next simulated drive; document the choice.
- Support the conference-demo behavior `custom ev-drive-timer 50`: remain connected for 50 seconds, disconnect for approximately two seconds so Matter attributes settle, then reconnect with a lower SoC and repeat.
- Make the drive timer cancellable and idempotent. Starting it again must replace or explicitly reject an existing timer rather than create competing timers.
- Ensure timer callbacks post work to the application task; they must not mutate SimEV or Matter attributes directly from an unsafe timer context.
- Define button/timer interaction: BTN1 must remain usable while the timer is active, and a manual toggle must cancel, restart, or safely synchronize with the active cycle.
- Log drive-cycle phase changes and the SoC before disconnect, during the drive interval, and after reconnect.
- Log every transition with connected, EVSE-enabled, demand, charging, SoC, active power, cumulative imported energy, and EVSE state.
- Refresh the LCD and Matter attributes when connection, charging, SoC, or EVSE state changes—not only after a SoC delta greater than two percent.

## Verification checklist

- Build and flash the combined Series 2 solution.
- Read and subscribe to Energy EVSE `StateOfCharge`.
- Read Electrical Energy Measurement imported/exported cumulative energy.
- Confirm the imported counter is an `EnergyMeasurementStruct` in mWh, increases only with actual imported charging energy, and remains unchanged while idle or disconnected.
- Confirm cumulative imported energy is unchanged by BTN1’s lower-SoC reconnect reset and by controller enable/disable changes.
- Confirm cumulative exported energy remains exactly zero throughout the demo.
- Reboot after charging and confirm the demo-boot imported counter intentionally restarts at zero, then increases only from energy imported during the new boot.
- Confirm cumulative-energy snapshots/events contain the same imported/exported values as the readable attributes and use valid timestamps where supported.
- Attempt or simulate a counter decrease and confirm it is detected and not silently published.
- Connect vehicle, enable charging, and verify SoC rises and imported energy rises by the corresponding power integral.
- Disable charging while connected; verify SoC and imported energy stop changing after power reaches zero.
- Re-enable charging; verify both resume without a reset.
- Disconnect with BTN1; verify power/current become zero, EVSE reports `NotPluggedIn`, SoC remains stable during the drive interval, and cumulative imported energy does not reset.
- Reconnect with BTN1; verify the intentional lower-SoC reset is published exactly once and charging resumes according to the current controller permission.
- Run the custom drive timer; verify repeated 50-second connected intervals, approximately two-second disconnected intervals, settled Matter reports, no duplicate timers, and no state races.
- Reach 100%; verify demand, charging, power, SoC, and energy behavior.
- Test controller commands, button transitions, reconnects, and Matter subscriptions after commissioning.

## Documentation references

- Silicon Labs Custom Matter Device Development: https://docs.silabs.com/matter/2.9.1/matter-references/custom-matter-device
- Silicon Labs guidance: customize app behavior through `CustomerAppTask` overrides and keep app logic out of generated sources.
- Matter Energy EVSE cluster: `StateOfCharge` is nullable and read-only to clients; the server application updates it through the generated instance setter.
- Local reference implementation: `repos/connectedhomeip/examples/evse-app/evse-common/src/EnergyEvseEventTriggers.cpp`, which uses `GetInstance()->SetStateOfCharge(...)`.
