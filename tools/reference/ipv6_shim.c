/*
 * Test-harness shim for running the original BDS 1.6.0.15 on hosts without
 * kernel IPv6 (e.g. some sandboxed CI/cloud containers).
 *
 * Observed with strace: when socket(AF_INET6) fails with EAFNOSUPPORT, the
 * original RakPeerHelper::peerStartup retries "IPv4 only" but still opens an
 * IPv6 descriptor, fails again, and leaves the server with no sockets at all.
 * This shim makes AF_INET6 UDP sockets succeed by backing them with IPv4
 * sockets bound to 127.0.0.1 on the requested IPv6 port, so RakNet starts and
 * the real IPv4 listener works. It changes no server logic and is never part
 * of the independent implementation.
 *
 * Build: cc -shared -fPIC -O2 -o ipv6_shim.so ipv6_shim.c -ldl
 * Use:   LD_PRELOAD=/path/ipv6_shim.so ./bedrock_server
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#define MAX_FD 4096
static unsigned char fake_v6[MAX_FD];

static int is_fake(int fd) { return fd >= 0 && fd < MAX_FD && fake_v6[fd]; }

int socket(int domain, int type, int protocol) {
    static int (*real)(int, int, int);
    if (!real) real = (int (*)(int, int, int))dlsym(RTLD_NEXT, "socket");
    int fd = real(domain, type, protocol);
    if (fd < 0 && errno == EAFNOSUPPORT && domain == AF_INET6 && (type & 0xf) == SOCK_DGRAM) {
        fd = real(AF_INET, type, protocol);
        if (fd >= 0 && fd < MAX_FD) fake_v6[fd] = 1;
    } else if (fd >= 0 && fd < MAX_FD) {
        fake_v6[fd] = 0;
    }
    return fd;
}

static void to_v4(const struct sockaddr *in, struct sockaddr_in *out) {
    const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)in;
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = v6->sin6_port;
    out->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static void to_v6(const struct sockaddr_in *in, struct sockaddr *out, socklen_t *len) {
    struct sockaddr_in6 v6;
    memset(&v6, 0, sizeof(v6));
    v6.sin6_family = AF_INET6;
    v6.sin6_port = in->sin_port;
    v6.sin6_addr = in6addr_loopback;
    socklen_t n = *len < sizeof(v6) ? *len : (socklen_t)sizeof(v6);
    memcpy(out, &v6, n);
    *len = sizeof(v6);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    static int (*real)(int, const struct sockaddr *, socklen_t);
    if (!real) real = (int (*)(int, const struct sockaddr *, socklen_t))dlsym(RTLD_NEXT, "bind");
    if (is_fake(fd) && addr && addr->sa_family == AF_INET6) {
        struct sockaddr_in v4;
        to_v4(addr, &v4);
        return real(fd, (struct sockaddr *)&v4, sizeof(v4));
    }
    return real(fd, addr, len);
}

int setsockopt(int fd, int level, int name, const void *value, socklen_t len) {
    static int (*real)(int, int, int, const void *, socklen_t);
    if (!real) real = (int (*)(int, int, int, const void *, socklen_t))dlsym(RTLD_NEXT, "setsockopt");
    if (is_fake(fd) && level == IPPROTO_IPV6) return 0;
    return real(fd, level, name, value, len);
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *len) {
    static int (*real)(int, struct sockaddr *, socklen_t *);
    if (!real) real = (int (*)(int, struct sockaddr *, socklen_t *))dlsym(RTLD_NEXT, "getsockname");
    if (!is_fake(fd)) return real(fd, addr, len);
    struct sockaddr_in v4;
    socklen_t n = sizeof(v4);
    int result = real(fd, (struct sockaddr *)&v4, &n);
    if (result == 0) to_v6(&v4, addr, len);
    return result;
}

ssize_t sendto(int fd, const void *buf, size_t size, int flags, const struct sockaddr *addr, socklen_t len) {
    static ssize_t (*real)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
    if (!real) real = (ssize_t(*)(int, const void *, size_t, int, const struct sockaddr *, socklen_t))dlsym(RTLD_NEXT, "sendto");
    if (is_fake(fd) && addr && addr->sa_family == AF_INET6) {
        struct sockaddr_in v4;
        to_v4(addr, &v4);
        return real(fd, buf, size, flags, (struct sockaddr *)&v4, sizeof(v4));
    }
    return real(fd, buf, size, flags, addr, len);
}

ssize_t recvfrom(int fd, void *buf, size_t size, int flags, struct sockaddr *addr, socklen_t *len) {
    static ssize_t (*real)(int, void *, size_t, int, struct sockaddr *, socklen_t *);
    if (!real) real = (ssize_t(*)(int, void *, size_t, int, struct sockaddr *, socklen_t *))dlsym(RTLD_NEXT, "recvfrom");
    if (!is_fake(fd) || !addr || !len) return real(fd, buf, size, flags, addr, len);
    struct sockaddr_in v4;
    socklen_t n = sizeof(v4);
    ssize_t result = real(fd, buf, size, flags, (struct sockaddr *)&v4, &n);
    if (result >= 0) to_v6(&v4, addr, len);
    return result;
}
