#ifndef UDP_H
#define UDP_H

#include <stddef.h>
#include <netinet/in.h>

#define UDP_MAX_PORT 65535

typedef struct udp_sender
{
    int sock;
    struct sockaddr_in dest_addr;
} udp_sender_t;

int udp_sender_init(udp_sender_t *sender, const char *addr, int port);

int udp_sender_send(udp_sender_t *sender, const char *data, size_t len);

void udp_sender_free(udp_sender_t *sender);

typedef struct udp_server
{
    int sock;
    fd_set readfds;
    int max_fd;
} udp_server_t;

int udp_server_init(udp_server_t *server, int port);

void udp_server_free(udp_server_t *server);

int udp_server_process(udp_server_t *server, char *buf, size_t buf_size);

#endif
