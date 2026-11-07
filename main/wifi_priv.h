#pragma once
// The private header of the WiFi: main/wifi.c (the station, the events, the API, the hotspot, the capture)
// and its parts wifi_nets.c (the saved networks) and wifi_web.c (the board pages). Only these files include it.
#include <stdio.h>
#include "arc_mac.h"
#include "tfn_extras.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "settings.h"
#include "storage.h"
#include "gfx.h"
#include "midifile.h"
#include "pad_config.h"
#include "app.h"
#if ARCADE_HAS_BOOKS
#include "books_web.h"
#endif
#include "wifi.h"
#include "lan.h"
#include "link.h"
#include "web_walkie.h"
#include "web_sys.h"
#include "evlog.h"
#include "web_files.h"
#include "ota.h"
#include "tfn_web.h"
#include "web_auth.h"
#include "web_server.h"

// THE SAVED NETWORKS: the NVS blob "nets" (namespace "wifi") keeps up to WIFI_SAVED_MAX networks, the last
// one in use first. The keys "ssid" and "pass" hold the first network (an older firmware reads only them; the first
// start makes the list from them). With two or more networks, a start (and a new try after a fault)
// scans first and joins the first saved network that it sees; one that failed last comes after the others.
#define WIFI_SAVED_MAX 10
typedef struct {
    char ssid[33];
    char pass[65];
} wnet_t;

#define MAX_AP 16

// the shared functions and variables of the parts
// wifi.c
extern wifi_ap_t wif_aps[MAX_AP];
extern int wif_nap;
// wifi_nets.c
extern char wif_bad[33];
extern bool wif_manual[WIFI_SAVED_MAX];
extern wnet_t *wif_nets;
extern int wif_nnet;
void wif_auto_pick(void);
int wif_count_auto(void);
int wif_first_auto(void);
int wif_nets_find(const char *ssid);
bool wif_nets_load(void);
void wif_nets_put_first(const char *ssid, const char *pass);
void wif_nets_save(void);
