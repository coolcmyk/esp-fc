#pragma once

#include "Model.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Serves the App's MSP protocol over the TCP socket that wifi_bridge accepts.
// Shares the flight controller's Model with the serial MSP port.
void msp_tcp_start(Espfc::Model *model);

#ifdef __cplusplus
}
#endif
