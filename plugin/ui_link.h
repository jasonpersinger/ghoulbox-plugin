/* SPDX-License-Identifier: GPL-3.0-only */
/* The one call a plugin window makes into its plugin (DPF direct access): that instance's UI bridge, nullptr when the
 * instance has no core. */
#pragma once
class UiBridge;
UiBridge* ghoulboxBridge(void* pluginInstance);
