#ifndef SENKO_VPN_STATUS_BAR_H
#define SENKO_VPN_STATUS_BAR_H

#import <Foundation/Foundation.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENKO_VPN_STARTED_NOTIFICATION "com.customproxy.vpn.started"
#define SENKO_VPN_STOPPED_NOTIFICATION "com.customproxy.vpn.stopped"
#define SENKO_STATUS_CHANGED_NOTIFICATION "com.senko.status.changed"

/* Initialize status bar hooks, restore persisted state across resprings, and
   register Darwin notification listeners. Safe to call multiple times; only
   activates inside com.apple.springboard. */
void InitVPNStatusBar(void);

/* Enable or disable the fake VPN status bar indicator, persist the state
   across resprings, and trigger an immediate status bar refresh. */
void SetVPNStatusBarEnabled(BOOL enabled);

/* Return the current fake VPN status bar state. */
BOOL IsVPNStatusBarEnabled(void);

/* Reload state from the shared state file (respecting com.senko.status.off)
   and schedule a status bar refresh. */
void SyncVPNStatusBarFromState(void);

/* Return YES when the persisted state records a tunnel riding a Wi-Fi
   interface (e.g. "1 en0"), used on legacy iOS to keep the Wi-Fi glyph. */
BOOL VPNStatusBarTunnelRidesWiFi(void);

/* Append a diagnostic line to the bounded status bar log ring. */
void VPNStatusBarLog(NSString *line);

/* Register an optional callback invoked after each status bar apply pass. */
void VPNStatusBarSetPostApplyCallback(void (*callback)(BOOL enabled, NSString *via));

#ifdef __cplusplus
}
#endif

#endif /* SENKO_VPN_STATUS_BAR_H */
