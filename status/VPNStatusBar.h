#ifndef SENKO_VPN_STATUS_BAR_H
#define SENKO_VPN_STATUS_BAR_H

#import <Foundation/Foundation.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENKO_VPN_STARTED_NOTIFICATION "com.customproxy.vpn.started"
#define SENKO_VPN_STOPPED_NOTIFICATION "com.customproxy.vpn.stopped"
#define SENKO_STATUS_CHANGED_NOTIFICATION "com.senko.status.changed"

/* installs the hooks, restores the state across resprings and registers the
   darwin listeners. does nothing outside springboard, safe to call twice */
void InitVPNStatusBar(void);

/* turns the badge on or off, persists it and redraws the status bar now */
void SetVPNStatusBarEnabled(BOOL enabled);

/* the badge state springboard is drawing */
BOOL IsVPNStatusBarEnabled(void);

/* rereads the state file, honouring the com.senko.status.off marker, and
   schedules a redraw */
void SyncVPNStatusBarFromState(void);

/* YES when the state file says the tunnel rides wifi ("1 en0"), which the
   ios 4 to 6 springboard needs to keep its wifi glyph */
BOOL VPNStatusBarTunnelRidesWiFi(void);

/* appends a line to the status log, which restarts at 32 KiB */
void VPNStatusBarLog(NSString *line);

/* calls back after every redraw pass, with the path that refreshed the bar */
void VPNStatusBarSetPostApplyCallback(void (*callback)(BOOL enabled, NSString *via));

#ifdef __cplusplus
}
#endif

#endif
