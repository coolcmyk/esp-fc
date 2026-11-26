#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Joins the configured AP through the on-board ESP32-C6 (ESP-Hosted/SDIO) and
// starts a TCP server that will carry the MSP stream for the App's TCP
// transport. Returns immediately; connection happens in the background.
void wifi_bridge_start(void);

#ifdef __cplusplus
}
#endif
