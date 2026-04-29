#pragma once

/* Captive-portal DNS server.
   Call once after wifi_start_ap().  Intercepts every DNS query on UDP:53
   and replies with 192.168.4.1 so the phone OS detects the portal and
   opens the setup page automatically. */
void captive_portal_dns_start(void);
