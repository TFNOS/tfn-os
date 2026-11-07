// TFN OS SDK example 5, the watcher of WATCH DEMO (main/watch.h): watch.elf. The board keeps it loaded and calls
// check() in its background rounds (SETUP > NOTIFICATIONS > BACKGROUND CHECKS), also while other apps run. A watcher
// has no screen, no sound and no touch (the loader links no such name): it counts, reads the secret store of its app,
// and sends a notice that opens the app at this check (the link "n=<count>").
#include <stdint.h>
#include <stdio.h>
#include "tfn/notify.h"
#include "tfn/settings.h"
#include "tfn/watch.h"

static void check(void)
{
    uint32_t n = store_get("watchdemo_n", 0) + 1;       // (the saves of the app: the prefix of its id)
    store_set("watchdemo_n", n);
    char sec[64], title[32], text[96], link[16];
    int l = tfn_secret_get("demo", sec, sizeof sec);
    snprintf(title, sizeof title, "CHECK %lu", (unsigned long)n);
    snprintf(text, sizeof text, "A BACKGROUND CHECK. THE SECRET OF THE APP: %s", l > 0 ? "FOUND" : "NOT THERE");
    snprintf(link, sizeof link, "n=%lu", (unsigned long)n);
    tfn_notify(title, text, link);
}

const watch_t watch_watchdemo = {5, NULL, check};
