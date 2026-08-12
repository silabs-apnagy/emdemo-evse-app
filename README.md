# Matter over Thread EVSE Solution — Version 2.0

This repository contains the complete Silicon Labs Simplicity Studio 6 solution for the Matter over Thread Electric Vehicle Supply Equipment (EVSE) application. Version 2.0 ports the solution to Simplicity SDK 2026.6.1 and includes both the EVSE application and its Series 2 external-storage bootloader project.

## Clone

Clone the repository directly into your Simplicity Studio 6 workspace:

```bash
cd v6_workspace/

git clone git@github.com:silabs-apnagy/emdemo-evse-app.git matter_thread_soc_evse_app_series_2_freertos
```

## Build

1. Open the cloned solution in Simplicity Studio 6.
2. Open `matter_thread_soc_evse_app_freertos/matter_thread_soc_evse_app_freertos.slcp`.
3. In the **Overview** panel, open the three-dot menu in **Project Details** and select **Force Generation**.
4. Build the solution in VS Code or with the Simplicity Studio build tools.

The solution build includes the `matter_bootloader_external_series_2` bootloader and produces combined programming artifacts.

## Run

Flash the combined solution artifact to the target board, then commission the device to a Matter fabric over Bluetooth Low Energy and Thread. The commissioning QR code is shown on the board display and is also available in the RTT log.

- Press **BTN0** to restart BLE advertising and print the commissioning QR-code URL.
- Hold **BTN0** for six seconds to initiate a factory reset.
- Control EVSE charging through a commissioned Matter controller.

For application behavior, commissioning commands, and troubleshooting, see the [EVSE application README](matter_thread_soc_evse_app_freertos/README.md).
