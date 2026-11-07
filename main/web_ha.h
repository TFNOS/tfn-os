#pragma once

// The "Home Assistant" part of the board web page (/ha) and its API (web_ha.c).
void web_ha_register(void *h);  // h: the httpd_handle_t of wifi.c
