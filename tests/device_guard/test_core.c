#include <assert.h>
#include <stdio.h>
#include "../../main/device_guard_core.h"
int main(void)
{
    dg_state_t s = {0};
    uint8_t nonce[32], out[32];
    memset(nonce, 0x5a, sizeof nonce);
    assert(!dg_usb(&s, true, 0) && !dg_local(&s, 0));
    assert(dg_challenge(&s, nonce, 0) && dg_take(&s, 1, out) && !memcmp(nonce, out, 32));
    assert(!dg_take(&s, 2, out)); // a replay cannot reuse the challenge
    dg_result(&s, false, 2);
    assert(!dg_challenge(&s, nonce, 1001) && dg_challenge(&s, nonce, 1002));
    dg_lock(&s);
    assert(!dg_challenge(&s, nonce, 1001)); // lock cannot cancel failure backoff
    assert(dg_challenge(&s, nonce, 1002));
    assert(!dg_take(&s, 11002, out)); // expiry is exclusive
    assert(dg_challenge(&s, nonce, 12000));
    assert(dg_take(&s, 12001, out));
    dg_result(&s, true, 12001);
    assert(dg_usb(&s, true, 12002));
    assert(!dg_usb(&s, false, 12003));
    assert(!dg_usb(&s, true, 12004));
    dg_result(&s, true, 20000);
    assert(!dg_usb(&s, true, 20000 + DG_USB_IDLE_MS));
    dg_result(&s, true, 30000);
    for (uint64_t t = 30001; t < 30000 + DG_USB_MAX_MS; t += 30000)
        assert(dg_usb(&s, true, t));
    assert(!dg_usb(&s, true, 30000 + DG_USB_MAX_MS)); // activity cannot keep a session forever
    dg_result(&s, true, 5000000);
    assert(!dg_usb(&s, true, 1)); // clock rollback fails closed
    s.local_until = 120;
    assert(dg_local(&s, 119) && !dg_local(&s, 120));
    dg_lock(&s);
    assert(!s.pending && !s.usb && !s.local_until);
    assert(dg_public_command("ota", "") && dg_public_command("webpass", NULL));
    assert(!dg_public_command("webpass", "off") && !dg_public_command("webpass", "set guess"));
    assert(!dg_public_command("apps", "dev 1") && !dg_public_command("ls", "/data"));
    assert(!dg_public_command("sdusb", "1 get 2f 0 512") && !dg_public_command("shot", ""));
    assert(!dg_public_command("storeall", "") && !dg_public_command("wifi", "list"));
    assert(dg_private_path("/data") && dg_private_path("/data/ai_limits.key"));
    assert(!dg_private_path("/database") && !dg_private_path("/sdcard/WIFI_TEST/capture.pcap"));
    char path[240];
    assert(dg_maintenance_path("2f646174612f61695f6c696d6974732e6b6579", path, sizeof path) &&
           !strcmp(path, "/data/ai_limits.key"));
    const char *bad[] = {"",
                         "0",
                         "zz",
                         "2f6574632f706173737764",
                         "2f6461746162617365",
                         "2f646174612f2e2e2f78",
                         "2f646174612f2f78",
                         "2f646174612f2e2f78",
                         "2f646174612f0078",
                         "2f646174612f5c78",
                         "2f646174612f3a78"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        assert(!dg_maintenance_path(bad[i], path, sizeof path));
    for (unsigned i = 0; i < 20000; i++) {
        uint64_t t = (uint64_t)i * 20000;
        dg_result(&s, false, t);
        assert(!dg_challenge(&s, nonce, t) && !dg_usb(&s, true, t));
    }
    puts("DEVICE GUARD CORE OK: replay/expiry/backoff, disconnect/idle/max lifetime, local expiry and "
         "command/path policy");
    return 0;
}
