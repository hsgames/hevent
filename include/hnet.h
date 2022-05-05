#ifndef HNET_H
#define HNET_H

#include <sys/types.h>

#define HNET_OK 0
#define HNET_ERR -1
#define HNET_ERR_LEN 256

#define HNET_NONE 0
#define HNET_IP_ONLY (1<<0)

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo;
struct sockaddr_storage;

int hnet_tcp_nonblock_connect(char *err, char *addr, int port);
int hnet_tcp_server(char *err, int port, char *bindaddr, int backlog, int reuse_port);
int hnet_tcp6_server(char *err, int port, char *bindaddr, int backlog, int reuse_port);
int hnet_tcp_accept(char *err, int serversock, struct sockaddr_storage *sa);
int hnet_nonblock(char *err, int fd);
int hnet_enable_tcp_nodelay(char *err, int fd);
int hnet_send_timeout(char *err, int fd, long long ms);
int hnet_keep_alive(char *err, int fd, int interval);
int hnet_set_recv_buffer(char *err, int fd, int buffsize);
int hnet_set_send_buffer(char *err, int fd, int buffsize);
int hnet_get_sock_error(int fd);
int hnet_udp_server(char *err, int port, char *bindaddr, int reuse_port);
int hnet_udp6_server(char *err, int port, char *bindaddr, int reuse_port);
int hnet_udp_nonblock_sendto(char *err, char *addr, int port, void *buf, size_t len, ssize_t *written);
ssize_t hnet_recvfrom(int fd, void *buf, size_t len, struct sockaddr_storage *sa);
ssize_t hnet_sendto(int fd, void *buf, size_t len, struct sockaddr_storage *sa);
void hnet_get_ip_port(struct sockaddr_storage *sa, char *ip, size_t ip_len, int *port);

#ifdef __cplusplus
}
#endif

#endif