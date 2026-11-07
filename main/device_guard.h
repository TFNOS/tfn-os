#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
bool device_guard_setup_allowed(void);
// -2 means an ordinary build (use the app's existing storage). Other negative
// values fail closed. Account text is never returned to a generic file viewer.
int device_guard_accounts_read(char *out, size_t cap);
int device_guard_accounts_write(const char *text, size_t len);
bool device_guard_enabled(void);
bool device_guard_vault_init(void);
void device_guard_log_init(void);
bool device_guard_random_init(void);
bool device_guard_random(void *out, size_t len);
int device_guard_vault_hardware_key(uint8_t out[32]);
void device_guard_tick(void);
bool device_guard_usb_admin(void);
bool device_guard_local_admin(void);
bool device_guard_local_unlock(const char *password);
void device_guard_local_lock(void);
bool device_guard_console(const char *command, const char *args);
bool device_guard_verify_app(const uint8_t *data, size_t len, const uint8_t *sig, size_t siglen);
const uint8_t *device_guard_app_key(void);
bool device_guard_local_path(const char *path);
#else
static inline bool device_guard_setup_allowed(void)
{
    return true;
}
static inline int device_guard_accounts_read(char *out, size_t cap)
{
    (void)out;
    (void)cap;
    return -2;
}
static inline int device_guard_accounts_write(const char *text, size_t len)
{
    (void)text;
    (void)len;
    return -2;
}
static inline bool device_guard_enabled(void)
{
    return false;
}
static inline bool device_guard_vault_init(void)
{
    return true;
}
static inline void device_guard_log_init(void)
{
}
static inline bool device_guard_random_init(void)
{
    return true;
}
static inline void device_guard_tick(void)
{
}
static inline bool device_guard_usb_admin(void)
{
    return true;
}
static inline bool device_guard_local_admin(void)
{
    return true;
}
static inline bool device_guard_local_unlock(const char *p)
{
    (void)p;
    return false;
}
static inline void device_guard_local_lock(void)
{
}
static inline bool device_guard_console(const char *c, const char *a)
{
    (void)c;
    (void)a;
    return true;
}
static inline bool device_guard_verify_app(const uint8_t *d, size_t n, const uint8_t *s, size_t l)
{
    (void)d;
    (void)n;
    (void)s;
    (void)l;
    return false;
}
static inline const uint8_t *device_guard_app_key(void)
{
    return NULL;
}
static inline bool device_guard_local_path(const char *p)
{
    (void)p;
    return true;
}
#endif
