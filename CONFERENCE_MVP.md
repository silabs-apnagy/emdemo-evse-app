# EVSE + SimEV — conference 2.0 preview

Checkpoint: **2026-10-20**. Feature scope is frozen; bug fixes/validation only.
Working branch: `evse2.0`. Committed base: `61f8249`; target-completion auto-trip
changes remain local and must be committed before updating the super-repo pin.

## Current local behavior

- xG24 / BRD4187C Matter-over-Thread EVSE and local simulated vehicle.
- Demo circuit/hardware/cable ceilings: 16 A; minimum charging current: 6 A.
  Solar mode honors HEMS/user current limiting. Manual ignores those requested
  caps but retains circuit/hardware/cable safety ceilings.
- Default target: 80% SoC unless supplied through the supported HEMS target path;
  `NextChargeRequiredEnergy` follows the battery model and target.
- HEMS requests mode/current/charging permission; firmware retains authority over
  vehicle connection, demand, target completion, faults, and charging execution.
- At actual target completion, automatically disconnect for a four-real-minute
  simulated drive, then reconnect. Manual connection input cancels the pending
  automatic return. Obsolete drive CLI controls are not the conference interface.
- The vehicle model advances at 60 simulated seconds per application tick; this is
  separate from the controller's 24-minute environment loop and IHD ×60 presentation.

## Verification / next handoff

- App builds passed earlier in this work; not rebuilt for this documentation audit.
- Confirm flashed revision, live limit increases/decreases, target demand, and
  trip/button precedence over a full loop before shipping.
- Weekly-target command capability is not a complete charging scheduler. Forecast,
  departure-time optimization, direct meter subscriptions, and full DEM-mode
  coordination are deferred to full 2.0.
- Existing `todo.md` retains detailed EVSE accounting/model follow-ups; it is not
  a checklist of features newly validated by this audit.

Full-system status and pinning handoff: `energy-management-demo/docs/conference-status.md`.
