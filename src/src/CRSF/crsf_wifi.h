#ifndef _CRSF_WIFI_H
#define _CRSF_WIFI_H

#ifdef HAVE_CRSF

class WiFiManager;

void crsf_wifi_register_page(WiFiManager &wm);
void crsf_wifi_save_params(WiFiManager &wm);
void crsf_wifi_update_values();

#endif

#endif
