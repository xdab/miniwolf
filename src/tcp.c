#include "tcp.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int tcp_server_init(tcp_server_t *server, int port)
{
    nonnull(server, "server");
    EXITIF(port < 0, -1, "port must be positive");
    EXITIF(port > 65535, -1, "port must be less than 65536");

    memset(server, 0, sizeof(tcp_server_t));
    FD_ZERO(&server->readfds);

    for (int i = 0; i < MAX_TCP_CLIENTS; i++)
        server->clients[i].fd = -1;

    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0)
    {
        LOGV("socket() failed");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        LOGV("setsockopt(SO_REUSEADDR) failed");
        close(server->listen_fd);
        return -1;
    }

    if (set_nonblocking(server->listen_fd) < 0)
    {
        LOGV("set_nonblocking() failed");
        close(server->listen_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOG("bind() failed on port %d", port);
        close(server->listen_fd);
        return -1;
    }

    if (listen(server->listen_fd, 5) < 0)
    {
        LOGV("listen() failed");
        close(server->listen_fd);
        return -1;
    }

    server->max_fd = server->listen_fd;
    LOG("server listening on port %d", port);

    return 0;
}

void tcp_server_free(tcp_server_t *server)
{
    nonnull(server, "server");

    for (int i = 0; i < server->num_clients; i++)
    {
        if (server->clients[i].fd >= 0)
            close(server->clients[i].fd);
    }

    if (server->listen_fd >= 0)
        close(server->listen_fd);

    for (int i = 0; i < MAX_TCP_CLIENTS; i++)
        server->clients[i].fd = -1;
    server->listen_fd = -1;
    server->num_clients = 0;
}

static void tcp_remove_client(tcp_server_t *server, int idx)
{
    if (idx < 0 || idx >= server->num_clients)
        return;

    close(server->clients[idx].fd);

    for (int i = idx; i < server->num_clients - 1; i++)
        server->clients[i] = server->clients[i + 1];

    server->num_clients--;
}

int tcp_server_process(tcp_server_t *server, char *buf, size_t buf_size)
{
    nonnull(server, "server");
    nonnull(buf, "buf");
    nonzero(buf_size, "buf_size");

    FD_ZERO(&server->readfds);
    FD_SET(server->listen_fd, &server->readfds);
    server->max_fd = server->listen_fd;

    for (int i = 0; i < server->num_clients; i++)
    {
        if (server->clients[i].fd >= 0)
        {
            FD_SET(server->clients[i].fd, &server->readfds);
            if (server->clients[i].fd > server->max_fd)
                server->max_fd = server->clients[i].fd;
        }
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    int ret = select(server->max_fd + 1, &server->readfds, NULL, NULL, &tv);
    if (ret < 0)
    {
        return -1;
    }

    if (ret == 0)
        return 0;

    if (FD_ISSET(server->listen_fd, &server->readfds))
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(server->listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd >= 0)
        {
            if (set_nonblocking(client_fd) < 0)
            {
                close(client_fd);
            }
            else if (server->num_clients < MAX_TCP_CLIENTS)
            {
                server->clients[server->num_clients].fd = client_fd;
                server->num_clients++;
                char ipbuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
                LOG("client connected from %s (total: %d)", ipbuf, server->num_clients);
            }
            else
            {
                LOG("max clients reached, rejecting connection");
                close(client_fd);
            }
        }
    }

    int total_read = 0;
    for (int i = 0; i < server->num_clients; i++)
    {
        if (server->clients[i].fd < 0)
            continue;

        if (!FD_ISSET(server->clients[i].fd, &server->readfds))
            continue;

        int n = read(server->clients[i].fd, buf, buf_size);
        if (n > 0)
        {
            total_read += n;
            return n;
        }
        else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        {
            LOG("client disconnected (total: %d)", server->num_clients - 1);
            tcp_remove_client(server, i);
            i--;
        }
    }

    return total_read;
}

void tcp_server_broadcast(tcp_server_t *server, const char *data, size_t len)
{
    nonnull(server, "server");
    nonnull(data, "data");
    nonzero(len, "len");

    for (int i = 0; i < server->num_clients; i++)
    {
        if (server->clients[i].fd <= 0)
            continue;

        ssize_t sent = send(server->clients[i].fd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            LOGV("send failed, removing client");
            tcp_remove_client(server, i);
            i--;
        }
    }
}

int tcp_client_init(tcp_client_t *client, const char *addr, int port)
{
    nonnull(client, "client");
    nonnull(addr, "addr");
    EXITIF(port < 0, -1, "port must be positive");
    EXITIF(port > 65535, -1, "port must be less than 65536");

    memset(client, 0, sizeof(tcp_client_t));

    client->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->fd < 0)
    {
        LOGV("socket() failed");
        return -1;
    }

    if (set_nonblocking(client->fd) < 0)
    {
        LOGV("set_nonblocking() failed");
        close(client->fd);
        client->fd = -1;
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, addr, &server_addr.sin_addr) <= 0)
    {
        LOG("invalid address: %s", addr);
        close(client->fd);
        client->fd = -1;
        return -1;
    }

    if (connect(client->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        if (errno != EINPROGRESS)
        {
            LOG("connect() failed to %s:%d", addr, port);
            close(client->fd);
            client->fd = -1;
            return -1;
        }
    }

    LOG("client connecting to %s:%d", addr, port);
    return 0;
}

void tcp_client_free(tcp_client_t *client)
{
    nonnull(client, "client");

    if (client->fd >= 0)
        close(client->fd);
    client->fd = -1;
}

int tcp_client_process(tcp_client_t *client, char *buf, size_t buf_size)
{
    nonnull(client, "client");
    nonnull(buf, "buf");
    nonzero(buf_size, "buf_size");

    if (client->fd < 0)
        return -1;

    // Check if connection is established
    int error;
    socklen_t len = sizeof(error);
    if (getsockopt(client->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
    {
        LOG("getsockopt failed");
        tcp_client_free(client);
        return -1;
    }
    if (error != 0)
    {
        // EINPROGRESS is expected for non-blocking connects, don't fail on it
        if (error != EINPROGRESS)
        {
            LOG("connection failed: %s", strerror(error));
            tcp_client_free(client);
            return -1;
        }
    }

    // Check if socket is ready for reading
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->fd, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms timeout

    int ret = select(client->fd + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0)
    {
        LOG("select failed");
        return -1;
    }
    if (ret == 0)
        return 0; // timeout

    if (!FD_ISSET(client->fd, &readfds))
        return 0;

    int n = read(client->fd, buf, buf_size);
    if (n > 0)
    {
        return n;
    }
    else if (n == 0)
    {
        LOG("server disconnected");
        tcp_client_free(client);
        return -1;
    }
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        LOG("read error");
        tcp_client_free(client);
        return -1;
    }

    return 0;
}
