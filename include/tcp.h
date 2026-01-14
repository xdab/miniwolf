#ifndef TCP_H
#define TCP_H

#include <sys/select.h>
#include <stddef.h>

#define MAX_TCP_CLIENTS 16
#define TCP_READ_BUF_SIZE 2048

typedef struct tcp_client
{
    int fd;
} tcp_client_t;

typedef struct tcp_server
{
    int listen_fd;
    int max_fd;
    fd_set readfds;
    tcp_client_t clients[MAX_TCP_CLIENTS];
    int num_clients;
} tcp_server_t;

int tcp_server_init(tcp_server_t *server, int port);

void tcp_server_free(tcp_server_t *server);

int tcp_server_process(tcp_server_t *server, char *buf, size_t buf_size);

void tcp_server_broadcast(tcp_server_t *server, const char *data, size_t len);

int tcp_client_init(tcp_client_t *client, const char *addr, int port);

void tcp_client_free(tcp_client_t *client);

int tcp_client_process(tcp_client_t *client, char *buf, size_t buf_size);

#endif
