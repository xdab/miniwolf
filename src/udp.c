#include "udp.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>

int udp_sender_init(udp_sender_t *sender, const char *addr, int port)
{
    nonnull(sender, "sender");
    nonnull(addr, "addr");
    EXITIF(port < 0, -1, "port must be positive");
    EXITIF(port > 65535, -1, "port must be less than 65536");

    size_t addr_len = strlen(addr);
    if (addr_len == 0 || addr_len >= INET_ADDRSTRLEN)
    {
        LOG("invalid address length: %s", addr);
        return -1;
    }

    memset(sender, 0, sizeof(udp_sender_t));

    sender->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender->sock < 0)
    {
        LOGV("socket() failed");
        return -1;
    }

    memset(&sender->dest_addr, 0, sizeof(sender->dest_addr));
    sender->dest_addr.sin_family = AF_INET;
    sender->dest_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, addr, &sender->dest_addr.sin_addr) <= 0)
    {
        LOG("invalid address: %s", addr);
        close(sender->sock);
        return -1;
    }

    // Enable broadcast if address ends with .255
    uint32_t addr_int = ntohl(sender->dest_addr.sin_addr.s_addr);
    if ((addr_int & 0xFF) == 0xFF)
    {
        int broadcast = 1;
        if (setsockopt(sender->sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
        {
            LOGV("setsockopt(SO_BROADCAST) failed");
            close(sender->sock);
            return -1;
        }
    }

    LOG("udp sender initialized for %s:%d", addr, port);
    return 0;
}

int udp_sender_send(udp_sender_t *sender, const char *data, size_t len)
{
    nonnull(sender, "sender");
    nonnull(data, "data");
    nonzero(len, "len");

    ssize_t sent = sendto(sender->sock, data, len, 0,
                          (struct sockaddr *)&sender->dest_addr, sizeof(sender->dest_addr));
    if (sent < 0)
    {
        LOG("sendto failed: %s (errno=%d)", strerror(errno), errno);
        return -1;
    }
    else if ((size_t)sent != len)
    {
        LOG("partial send: %zd/%zu bytes", sent, len);
        return -2;
    }
    else
    {
        LOGV("sent %zu bytes to %s:%d", len,
             inet_ntoa(sender->dest_addr.sin_addr), ntohs(sender->dest_addr.sin_port));
        return 0;
    }
}

void udp_sender_free(udp_sender_t *sender)
{
    nonnull(sender, "sender");

    if (sender->sock >= 0)
    {
        close(sender->sock);
        sender->sock = -1;
    }
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int udp_server_init(udp_server_t *server, int port)
{
    nonnull(server, "server");
    EXITIF(port < 0, -1, "port must be positive");
    EXITIF(port > 65535, -1, "port must be less than 65536");

    memset(server, 0, sizeof(udp_server_t));
    FD_ZERO(&server->readfds);

    server->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server->sock < 0)
    {
        LOGV("socket() failed");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        LOGV("setsockopt(SO_REUSEADDR) failed");
        close(server->sock);
        return -1;
    }

    if (set_nonblocking(server->sock) < 0)
    {
        LOGV("set_nonblocking() failed");
        close(server->sock);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOG("bind() failed on port %d", port);
        close(server->sock);
        return -1;
    }

    server->max_fd = server->sock;
    LOG("udp server listening on port %d", port);

    return 0;
}

void udp_server_free(udp_server_t *server)
{
    nonnull(server, "server");

    if (server->sock >= 0)
    {
        close(server->sock);
        server->sock = -1;
    }
}

int udp_server_process(udp_server_t *server, char *buf, size_t buf_size)
{
    nonnull(server, "server");
    nonnull(buf, "buf");
    nonzero(buf_size, "buf_size");

    FD_ZERO(&server->readfds);
    FD_SET(server->sock, &server->readfds);
    server->max_fd = server->sock;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    int ret = select(server->max_fd + 1, &server->readfds, NULL, NULL, &tv);
    if (ret < 0)
    {
        LOG("select() failed: %s (errno=%d)", strerror(errno), errno);
        return -1;
    }

    if (ret == 0 || !FD_ISSET(server->sock, &server->readfds))
        return 0;

    ssize_t n = recvfrom(server->sock, buf, buf_size, 0, NULL, NULL);
    if (n < 0)
    {
        LOG("recvfrom failed: %s", strerror(errno));
        return -1;
    }

    return n;
}
