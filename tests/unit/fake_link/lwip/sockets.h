// A small copy of the lwIP socket API for the link test. The names go to fk_* functions (no clash with the C library).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>  // (struct timeval)

typedef uint32_t socklen_t_fk;
#define socklen_t socklen_t_fk
struct in_addr_fk {
    uint32_t s_addr;
};
struct sockaddr_in_fk {
    uint8_t sin_len;
    uint8_t sin_family;
    uint16_t sin_port;
    struct in_addr_fk sin_addr;
    char sin_zero[8];
};
struct sockaddr_fk {
    uint8_t sa_len;
    uint8_t sa_family;
    char sa_data[14];
};
#define sockaddr_in      sockaddr_in_fk
#define sockaddr         sockaddr_fk
#define AF_INET          2
#define SOCK_DGRAM       2
#define IPPROTO_UDP      17
#define SOL_SOCKET       0xfff
#define SO_BROADCAST     0x20
#define SO_RCVTIMEO      0x1006
#define INADDR_ANY       0u
#define INADDR_BROADCAST 0xffffffffu
#define htons(x)         ((uint16_t)((((x) & 0xff) << 8) | (((x) >> 8) & 0xff)))
#define htonl(x)         fk_htonl(x)
static inline uint32_t fk_htonl(uint32_t x)
{
    return (x & 0xff) << 24 | (x & 0xff00) << 8 | (x >> 8 & 0xff00) | x >> 24;
}

#define socket     fk_socket
#define setsockopt fk_setsockopt
#define bind       fk_bind
#define sendto     fk_sendto
#define recvfrom   fk_recvfrom
#define close      fk_close
int fk_socket(int domain, int type, int proto);
int fk_setsockopt(int s, int level, int name, const void *v, socklen_t n);
int fk_bind(int s, const struct sockaddr *a, socklen_t n);
int fk_sendto(int s, const void *p, size_t n, int flags, const struct sockaddr *to, socklen_t tn);
int fk_recvfrom(int s, void *p, size_t n, int flags, struct sockaddr *from, socklen_t *fn);
int fk_close(int s);
