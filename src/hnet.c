#include "fmacros.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "hnet.h"

static void hnet_set_error(char *err, const char *fmt, ...)
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, HNET_ERR_LEN, fmt, ap);
    va_end(ap);
}

int hnet_nonblock(char *err, int fd) 
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        hnet_set_error(err, "fcntl(F_GETFL): %s", strerror(errno));
        return HNET_ERR;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        hnet_set_error(err, "fcntl(F_SETFL,O_NONBLOCK): %s", strerror(errno));
        return HNET_ERR;
    }
    return HNET_OK;
}

int hnet_keep_alive(char *err, int fd, int interval)
{
    int val = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
        hnet_set_error(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return HNET_ERR;
    }

    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        hnet_set_error(err, "setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
        return HNET_ERR;
    }

    val = interval / 3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        hnet_set_error(err, "setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
        return HNET_ERR;
    }

    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        hnet_set_error(err, "setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
        return HNET_ERR;
    }

    return HNET_OK;
}

int hnet_enable_tcp_nodelay(char *err, int fd)
{
    int val = 1;

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1) {
        hnet_set_error(err, "setsockopt TCP_NODELAY: %s", strerror(errno));
        return HNET_ERR;
    }
    return HNET_OK;
}

int hnet_send_timeout(char *err, int fd, long long ms) 
{
    struct timeval tv;

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
        hnet_set_error(err, "setsockopt SO_SNDTIMEO: %s", strerror(errno));
        return HNET_ERR;
    }
    return HNET_OK;
}

static int hnet_set_reuse_addr(char *err, int fd) 
{
    int yes = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        hnet_set_error(err, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        return HNET_ERR;
    }
    return HNET_OK;
}

static int hnet_set_reuse_port(char *err, int fd) 
{
    int yes = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) {
        hnet_set_error(err, "setsockopt SO_REUSEPORT: %s", strerror(errno));
        return HNET_ERR;
    }
    return HNET_OK;
}

static struct addrinfo *hnet_get_addr_info(char *err, int port, char *addr, 
    int af, int socktype, int flags)
{
    int rv;
    char portstr[6];
    struct addrinfo hints, *addrinfo;

    snprintf(portstr, 6, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = socktype;
    hints.ai_flags = flags;

    if ((rv = getaddrinfo(addr, portstr, &hints, &addrinfo)) != 0) {
        hnet_set_error(err, "%s", gai_strerror(rv));
        return NULL;
    }
    return addrinfo;
}

int hnet_tcp_nonblock_connect(char *err, char *addr, int port)
{
    int s = HNET_ERR;
    struct addrinfo *servinfo, *p;

    servinfo = hnet_get_addr_info(err, port, addr, AF_UNSPEC, SOCK_STREAM, 0);
    if (servinfo == NULL) {
        return HNET_ERR;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;
        if (hnet_set_reuse_addr(err, s) == HNET_ERR) goto error;
        if (hnet_nonblock(err,s) != HNET_OK)
            goto error;
        if (connect(s, p->ai_addr, p->ai_addrlen) == -1) {
            if (errno == EINPROGRESS)
                goto end;
            close(s);
            s = HNET_ERR;
            continue;
        }
        goto end;
    }
    if (p == NULL)
        hnet_set_error(err, "creating socket: %s", strerror(errno));

error:
    if (s != HNET_ERR) {
        close(s);
        s = HNET_ERR;
    }

end:
    freeaddrinfo(servinfo);
    return s;
}

static int hnet_listen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog) 
{
    if (bind(s, sa, len) == -1) {
        hnet_set_error(err, "bind: %s", strerror(errno));
        close(s);
        return HNET_ERR;
    }

    if (listen(s, backlog) == -1) {
        hnet_set_error(err, "listen: %s", strerror(errno));
        close(s);
        return HNET_ERR;
    }
    return HNET_OK;
}

static int hnet_v6_only(char *err, int s) 
{
    int yes = 1;
    if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) == -1) {
        hnet_set_error(err, "setsockopt: %s", strerror(errno));
        close(s);
        return HNET_ERR;
    }
    return HNET_OK;
}

static int hnet_generic_tcp_server(char *err, int port, char *bindaddr, 
    int af, int backlog, int reuse_port)
{
    int s = -1;
    struct addrinfo *servinfo, *p;

    servinfo = hnet_get_addr_info(err, port, bindaddr, af, SOCK_STREAM, AI_PASSIVE);
    if (servinfo == NULL) {
        return HNET_ERR;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;

        if (af == AF_INET6 && hnet_v6_only(err,s) == HNET_ERR) goto error;
        if (hnet_set_reuse_addr(err, s) == HNET_ERR) goto error;
        if (reuse_port && hnet_set_reuse_port(err, s) == HNET_ERR) goto error;
        if (hnet_listen(err, s, p->ai_addr, p->ai_addrlen, backlog) == HNET_ERR) s = HNET_ERR;
        goto end;
    }
    if (p == NULL) {
        hnet_set_error(err, "unable to bind socket, errno: %d", errno);
        goto error;
    }

error:
    if (s != -1) close(s);
    s = HNET_ERR;
end:
    freeaddrinfo(servinfo);
    return s;
}

int hnet_tcp_server(char *err, int port, char *bindaddr, int backlog, int reuse_port)
{
    return hnet_generic_tcp_server(err, port, bindaddr, AF_INET, backlog, reuse_port);
}

int hnet_tcp_6_server(char *err, int port, char *bindaddr, int backlog, int reuse_port)
{
    return hnet_generic_tcp_server(err, port, bindaddr, AF_INET6, backlog, reuse_port);
}

static int hnet_generic_accept(char *err, int s, struct sockaddr *sa, socklen_t *len) 
{
    int fd;
    while(1) {
        fd = accept(s, sa, len);
        if (fd == -1) {
            if (errno == EINTR)
                continue;
            else {
                hnet_set_error(err, "accept: %s", strerror(errno));
                return HNET_ERR;
            }
        }
        break;
    }
    return fd;
}

int hnet_tcp_accept(char *err, int s, struct sockaddr_storage *sa) 
{
    int fd;
    socklen_t salen = sizeof(*sa);

    if ((fd = hnet_generic_accept(err, s, (struct sockaddr*)sa, &salen)) == -1)
        return HNET_ERR;
    return fd;
}

int hnet_set_recv_buffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffsize, sizeof(buffsize)) == -1)
    {
        hnet_set_error(err, "setsockopt SO_RCVBUF: %s", strerror(errno));
        return HNET_ERR;
    }
    return HNET_OK;
}

int hnet_set_send_buffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize)) == -1)
    {
        hnet_set_error(err, "setsockopt SO_SNDBUF: %s", strerror(errno));
        return HNET_ERR;
    }
    return HNET_OK;
}

int hnet_get_sock_error(int fd)
{
    int sockerr = 0;
    socklen_t errlen = sizeof(sockerr);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1)
        sockerr = errno;
    return sockerr;
}

void hnet_get_ip_port(struct sockaddr_storage *sa, char *ip, size_t ip_len, int *port)
{
    if (sa->ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)sa;
        if (ip) inet_ntop(AF_INET, (void*)&(s->sin_addr), ip, ip_len);
        if (port) *port = ntohs(s->sin_port);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)sa;
        if (ip) inet_ntop(AF_INET6,(void*)&(s->sin6_addr), ip, ip_len);
        if (port) *port = ntohs(s->sin6_port);
    }
}

static int hnet_generic_udp_server(char *err, int port, char *bindaddr, int af, int reuse_port)
{
    int s = -1;
    struct addrinfo *servinfo, *p;

    servinfo = hnet_get_addr_info(err, port, bindaddr, af, SOCK_DGRAM, AI_PASSIVE);
    if (servinfo == NULL) {
        return HNET_ERR;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;

        if (af == AF_INET6 && hnet_v6_only(err,s) == HNET_ERR) goto error;
        if (hnet_set_reuse_addr(err, s) == HNET_ERR) goto error;
        if (reuse_port && hnet_set_reuse_port(err, s) == HNET_ERR) goto error;
        if (bind(s, p->ai_addr, p->ai_addrlen) == -1) {
            hnet_set_error(err, "bind: %s", strerror(errno));
            goto error;
        }
        goto end;
    }
    if (p == NULL) {
        hnet_set_error(err, "unable to bind socket, errno: %d", errno);
        goto error;
    }

error:
    if (s != -1) close(s);
    s = HNET_ERR;
end:
    freeaddrinfo(servinfo);
    return s;
}

int hnet_udp_server(char *err, int port, char *bindaddr, int reuse_port)
{
    return hnet_generic_udp_server(err, port, bindaddr, AF_INET, reuse_port);
}

int hnet_udp6_server(char *err, int port, char *bindaddr, int reuse_port)
{
    return hnet_generic_udp_server(err, port, bindaddr, AF_INET6, reuse_port);
}

int hnet_udp_nonblock_sendto(char *err, char *addr, int port, 
    void *buf, size_t len, ssize_t *written)
{
    int s = HNET_ERR;
    struct addrinfo *servinfo, *p;

    servinfo = hnet_get_addr_info(err, port, addr, AF_UNSPEC, SOCK_DGRAM, 0);
    if (servinfo == NULL) {
        return HNET_ERR;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;
        if (hnet_set_reuse_addr(err, s) == HNET_ERR) goto error;
        if (hnet_nonblock(err,s) != HNET_OK) goto error;
        *written = sendto(s, buf, len, 0, p->ai_addr, p->ai_addrlen);
        if (*written == -1) {
            if (errno == EAGAIN) {
                goto end;
            } else {
                hnet_set_error(err, "sendto: %s", strerror(errno));
                goto error;
            }
        }
        goto end;
    }
    if (p == NULL)
        hnet_set_error(err, "creating socket: %s", strerror(errno));

error:
    if (s != HNET_ERR) {
        close(s);
        s = HNET_ERR;
    }

end:
    freeaddrinfo(servinfo);
    return s;
}

ssize_t hnet_recvfrom(int fd, void *buf, size_t len, struct sockaddr_storage *sa)
{
    socklen_t salen = sizeof(*sa);

    return recvfrom(fd, buf, len, 0, (struct sockaddr*)sa, &salen);
}

ssize_t hnet_sendto(int fd, void *buf, size_t len, struct sockaddr_storage *sa) 
{
    return sendto(fd, buf, len, 0, (struct sockaddr*)sa, sizeof(*sa));
}

void hnet_set_mmsghdr(void *bufs, size_t len, unsigned int vlen, 
    struct sockaddr_storage *sas, struct mmsghdr *msgs, struct iovec *iovecs)
{
    unsigned int i;

    for (i = 0; i < vlen; i++) {
        iovecs[i].iov_base = (char*)bufs + i * len;
        iovecs[i].iov_len = len;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &sas[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(sas[i]);
    }
}

int hnet_recvmmsg(int fd, struct mmsghdr *msgs, unsigned int vlen)
{
    int retval;

    retval = recvmmsg(fd, msgs, vlen, 0, NULL);
    if (retval == -1) {
        return HNET_ERR;
    }
    return retval;
}
