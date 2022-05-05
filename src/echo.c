#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "he.h"
#include "hnet.h"

#define UNUSED(V) ((void) V)
#define NET_IP_STR_LEN 46
#define MAX_ACCEPTS_PER_CALL 1000

static long long time_in_milliseconds(void) 
{
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

static int server_cron(he_event_loop *event_loop, void *client_data) 
{
    UNUSED(event_loop);
    UNUSED(client_data);

    printf("server_cron %lld\n", time_in_milliseconds());
    return 1;
}

static void free_fd(he_event_loop *el, int fd) 
{
    he_delete_file_event(el, fd, HE_READABLE);
    he_delete_file_event(el, fd, HE_WRITABLE);
    close(fd);
}

static void write_tcp_handler(he_event_loop *el, int fd, void *privdata, int mask) 
{
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);
    printf("%d write_tcp_handler\n", fd);
}

static void read_tcp_handler(he_event_loop *el, int fd, void *privdata, int mask) 
{
    int nread, nwritten;
    char buf[4096];
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    nread = read(fd, buf, 4096);
    if (nread == -1) {
        if (errno == EAGAIN) {
            return;
        } else {
            printf("Reading from client: %s\n", strerror(errno));
            free_fd(el, fd);
        }
    } else if (nread == 0) {
        printf("Client closed connection\n");
        free_fd(el, fd);
    } else {
        buf[nread - 1] = '\0';
        printf("read %s\n", buf);
        nwritten = write(fd, buf, nread);
        if (nwritten == -1) {
            if (errno == EAGAIN) {
                he_create_file_event(el, fd, HE_WRITABLE, write_tcp_handler, NULL);
            } else {
                printf("Error writing to client: %s\n", strerror(errno));
                free_fd(el, fd);
            }
        } else if (nwritten >= 0) {
            if (nwritten < nread) {
                he_create_file_event(el, fd, HE_WRITABLE, write_tcp_handler, NULL);
            }
        }
    }
}

static void accept_tcp_handler(he_event_loop *el, int fd, void *privdata, int mask) 
{
    int cport, cfd, max = MAX_ACCEPTS_PER_CALL;
    char cip[NET_IP_STR_LEN];
    char neterr[HNET_ERR_LEN];
    struct sockaddr_storage sa;
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    while(max--) {
        cfd = hnet_tcp_accept(neterr, fd, &sa);
        if (cfd == HNET_ERR) {
            if (errno != EWOULDBLOCK)
                printf("Accepting client connection: %s\n", neterr);
            return;
        }
        hnet_get_ip_port(&sa, cip, sizeof(cip), &cport);
        printf("Accepted %s:%d\n", cip, cport);
        hnet_nonblock(neterr, cfd);
        hnet_enable_tcp_nodelay(neterr, cfd);
        hnet_keep_alive(neterr, cfd, 300);
        if (he_create_file_event(el, cfd, HE_READABLE, read_tcp_handler, NULL) == HE_ERR) {
            close(cfd);
        }
    }
}

static void connect_tcp_handler(he_event_loop *el, int fd, void *privdata, int mask) 
{
    int sockerr, nwrite = 0;
    char neterr[HNET_ERR_LEN];
    UNUSED(privdata);
    UNUSED(mask);

    he_delete_file_event(el, fd, HE_WRITABLE);
    sockerr = hnet_get_sock_error(fd);
    if (sockerr) {
        printf("client hnet_get_sock_error: %s\n", strerror(sockerr));
        close(fd);
        return;
    }
    hnet_enable_tcp_nodelay(neterr, fd);
    hnet_keep_alive(neterr, fd, 300);
    if (he_create_file_event(el, fd, HE_READABLE, read_tcp_handler, NULL) == HE_ERR) {
        close(fd);

        // reconnect
        return;
    }
    nwrite = write(fd, "hello", 6);
    if (nwrite < 0) {
        if (errno == EAGAIN) {
            // rewrite
            return;
        }
        // reconnect
    }
}

static void read_udp_handler(he_event_loop *el, int fd, void *privdata, int mask) 
{
    int cport, nread, nwritten;
    char cip[NET_IP_STR_LEN];
    char buf[4096];
    struct sockaddr_storage sa;
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    nread = hnet_recvfrom(fd, buf, 4096, &sa);
    if (nread == -1) {
        if (errno == EAGAIN) {
            return;
        } else {
            printf("Reading from client: %s\n", strerror(errno));
            free_fd(el, fd);
        }
    } else if (nread == 0) {
        printf("Client closed connection\n");
        free_fd(el, fd);
    } else {
        buf[nread - 1] = '\0';
        hnet_get_ip_port(&sa, cip, sizeof(cip), &cport);
        printf("read %s, from %s:%d\n", buf, cip, cport);
        nwritten = hnet_sendto(fd, buf, nread, &sa);
        if (nwritten == -1) {
            if (errno == EAGAIN) {
                
            } else {
                printf("Error writing to client: %s\n", strerror(errno));
                free_fd(el, fd);
            }
        } else if (nwritten >= 0) {
           
        }
    }
}

int main(int argc, char **argv) 
{
    int s, fd;
    ssize_t written;
    char neterr[HNET_ERR_LEN];

    if (argc != 3) {
        printf("echo argc != 3\n");
        exit(1);
    }
    he_event_loop *el = he_create_event_loop(1024, 2000, server_cron, NULL);
    if (el == NULL) {
        printf("Failed creating the event loop. Error message: %s\n", strerror(errno));
        exit(1);
    }
    if (!strcasecmp(argv[1], "tcp")) {
        if (!strcasecmp(argv[2], "server")) {
            printf("echo tcp server\n");
            if ((s = hnet_tcp_server(neterr, 8888, NULL, 511, 0)) == HNET_ERR) {
                printf("Could not create server TCP listening socket %s", neterr);
                exit(1);
            }
            hnet_nonblock(NULL, s);
            if (he_create_file_event(el, s, HE_READABLE, accept_tcp_handler, NULL) == HE_ERR) {
                printf("Unrecoverable error creating server.ipfd file event\n");
                exit(1);
            }
        } else if (!strcasecmp(argv[2], "client")) {
            printf("echo tcp client\n");
            fd = hnet_tcp_nonblock_connect(neterr, "127.0.0.1", 8888);
            if (fd == HNET_ERR) {
                printf("Could not connect socket %s", neterr);
                exit(1);
            }
            if (he_create_file_event(el, fd, HE_WRITABLE, connect_tcp_handler, NULL) == HE_ERR) {
                printf("Unrecoverable error creating client.ipfd file event\n");
                exit(1);
            }
        }
    } else if (!strcasecmp(argv[1], "udp")) {
        if (!strcasecmp(argv[2], "server")) {
            printf("echo udp server\n");
            if ((s = hnet_udp_server(neterr, 8888, NULL, 0)) == HNET_ERR) {
                printf("Could not create server UDP listening socket %s", neterr);
                exit(1);
            }
            hnet_nonblock(NULL, s);
            if (he_create_file_event(el, s, HE_READABLE, read_udp_handler, NULL) == HE_ERR) {
                printf("Unrecoverable error creating server.ipfd file event\n");
                exit(1);
            }
        } else if (!strcasecmp(argv[2], "client")) {
            printf("echo udp client\n");
            if ((fd = hnet_udp_nonblock_sendto(neterr, "127.0.0.1", 8888, "hello", 6, &written)) == HNET_ERR) {
                printf("Could not create client UDP socket %s\n", neterr);
                exit(1);
            }
            if (he_create_file_event(el, fd, HE_READABLE, read_udp_handler, NULL) == HE_ERR) {
                printf("Unrecoverable error creating client.ipfd file event\n");
                exit(1);
            }
        }
    }
    he_main(el);
    he_delete_event_loop(el);
    return 0;
}