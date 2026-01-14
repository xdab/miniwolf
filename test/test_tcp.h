#ifndef TEST_TCP_H
#define TEST_TCP_H

#include "test.h"
#include "tcp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void test_tcp_server_init_valid(void)
{
    tcp_server_t server;
    int result = tcp_server_init(&server, 0);
    assert_equal_int(result, 0, "init returns 0");
    assert_true(server.listen_fd > 2, "listen_fd valid");
    assert_equal_int(server.num_clients, 0, "num_clients 0");
    tcp_server_free(&server);
}

void test_tcp_server_process_timeout(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);
    char buf[1];
    int result = tcp_server_process(&server, buf, 1);
    assert_equal_int(result, 0, "timeout returns 0");
    tcp_server_free(&server);
}

void test_tcp_server_accept_client(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        usleep(200000);
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(caddr));
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(port);
        caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(client_fd, (struct sockaddr *)&caddr, sizeof(caddr));
        close(client_fd);
        exit(0);
    }

    char buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 20)
        tcp_server_process(&server, buf, 1);

    assert_equal_int(server.num_clients, 1, "client accepted");
    assert_true(server.clients[0].fd >= 3, "client fd valid");

    waitpid(pid, NULL, 0);
    tcp_server_free(&server);
}

void test_tcp_server_read_data(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        usleep(200000);
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(caddr));
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(port);
        caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(client_fd, (struct sockaddr *)&caddr, sizeof(caddr));
        write(client_fd, "test", 4);
        close(client_fd);
        exit(0);
    }

    char buf[16];
    int n = 0;
    int attempts = 0;
    while (n <= 0 && attempts++ < 20)
        n = tcp_server_process(&server, buf, sizeof(buf));

    assert_equal_int(n, 4, "read 4 bytes");
    assert_memory(buf, (void *)"test", 4, "data matches");

    waitpid(pid, NULL, 0);
    tcp_server_free(&server);
}

void test_tcp_server_client_disconnect(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        usleep(200000);
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(caddr));
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(port);
        caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(client_fd, (struct sockaddr *)&caddr, sizeof(caddr));
        close(client_fd);
        exit(0);
    }

    char buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 20)
        tcp_server_process(&server, buf, 1);
    assert_equal_int(server.num_clients, 1, "client connected");

    attempts = 0;
    while (server.num_clients == 1 && attempts++ < 20)
        tcp_server_process(&server, buf, 1);
    assert_equal_int(server.num_clients, 0, "client disconnected");

    waitpid(pid, NULL, 0);
    tcp_server_free(&server);
}

void test_tcp_server_broadcast(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        usleep(200000);
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(caddr));
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(port);
        caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(client_fd, (struct sockaddr *)&caddr, sizeof(caddr));
        char buf[16];
        int n = read(client_fd, buf, sizeof(buf));
        close(client_fd);
        exit(0);
    }

    char buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 20)
        tcp_server_process(&server, buf, 1);
    assert_equal_int(server.num_clients, 1, "client connected");

    tcp_server_broadcast(&server, "hello", 5);

    usleep(200000);
    waitpid(pid, NULL, 0);
    tcp_server_free(&server);
}

void test_tcp_server_free(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);
    tcp_server_free(&server);
    assert_equal_int(server.listen_fd, -1, "listen_fd reset");
    assert_equal_int(server.num_clients, 0, "num_clients reset");
}

void test_tcp_client_init_valid(void)
{
    tcp_client_t client;
    int result = tcp_client_init(&client, "127.0.0.1", 12345);
    assert_equal_int(result, 0, "init returns 0");
    assert_true(client.fd >= 0, "socket valid");
    tcp_client_free(&client);
}

void test_tcp_client_init_invalid_address(void)
{
    tcp_client_t client;

    // Empty address
    int result = tcp_client_init(&client, "", 12345);
    assert_equal_int(result, -1, "empty address rejected");

    // Too long address
    char long_addr[INET_ADDRSTRLEN + 10];
    memset(long_addr, '1', sizeof(long_addr) - 1);
    long_addr[sizeof(long_addr) - 1] = '\0';
    result = tcp_client_init(&client, long_addr, 12345);
    assert_equal_int(result, -1, "too long address rejected");

    // Malformed addresses (inet_pton will reject these)
    result = tcp_client_init(&client, "192.168.1", 12345);
    assert_equal_int(result, -1, "malformed address rejected");

    result = tcp_client_init(&client, "192.168.abc.1", 12345);
    assert_equal_int(result, -1, "non-numeric address rejected");

    result = tcp_client_init(&client, "999.999.999.999", 12345);
    assert_equal_int(result, -1, "invalid octet values rejected");
}

void test_tcp_client_process_timeout(void)
{
    tcp_client_t client;
    tcp_client_init(&client, "127.0.0.1", 12345);
    char buf[1];

    // Give connection time to fail (connecting to non-existent server)
    usleep(200000);

    int result = tcp_client_process(&client, buf, 1);
    assert_equal_int(result, -1, "connection failure returns -1");
    tcp_client_free(&client);
}

void test_tcp_client_connect_and_read(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        // Child process: create client and connect
        tcp_client_t client;
        int client_init = tcp_client_init(&client, "127.0.0.1", port);
        assert_equal_int(client_init, 0, "client init successful");

        char buf[16];
        int n = 0;
        int attempts = 0;
        // Wait for connection and data
        while (n <= 0 && attempts++ < 100)
        {
            n = tcp_client_process(&client, buf, sizeof(buf));
            if (n == 0) // Connection in progress or no data
                usleep(10000);
        }

        assert_equal_int(n, 4, "client read 4 bytes");
        assert_memory(buf, (void *)"test", 4, "data matches");

        tcp_client_free(&client);
        exit(0);
    }

    // Parent process: wait for client to connect, then send data
    usleep(200000); // Give child time to start connecting

    char dummy_buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 50)
    {
        tcp_server_process(&server, dummy_buf, 1);
        usleep(10000);
    }

    assert_equal_int(server.num_clients, 1, "server accepted client");

    // Send data to connected client
    tcp_server_broadcast(&server, "test", 4);

    waitpid(pid, NULL, 0);
    tcp_server_free(&server);
}

void test_tcp_client_server_disconnect(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        // Child process: create client and connect
        tcp_client_t client;
        int client_init = tcp_client_init(&client, "127.0.0.1", port);
        assert_equal_int(client_init, 0, "client init successful");

        char buf[16];
        int n = 0;
        int attempts = 0;
        // Wait for connection to be established
        while (n == 0 && attempts++ < 50)
        {
            n = tcp_client_process(&client, buf, sizeof(buf));
            usleep(10000);
        }

        // Now try to read - should eventually detect disconnect
        n = 0;
        attempts = 0;
        while (client.fd >= 0 && attempts++ < 50)
        {
            n = tcp_client_process(&client, buf, sizeof(buf));
            usleep(10000);
        }

        assert_equal_int(n, -1, "client detected disconnect");
        assert_equal_int(client.fd, -1, "client fd reset on disconnect");

        tcp_client_free(&client);
        exit(0);
    }

    // Parent process: accept client, then disconnect server
    usleep(200000); // Give child time to start connecting

    char dummy_buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 50)
    {
        tcp_server_process(&server, dummy_buf, 1);
        usleep(10000);
    }

    assert_equal_int(server.num_clients, 1, "server accepted client");

    // Disconnect the server (this should cause client to detect disconnect)
    tcp_server_free(&server);

    waitpid(pid, NULL, 0);
    tcp_server_free(&server); // Already freed, but safe
}

void test_tcp_client_read_error(void)
{
    tcp_client_t client;
    tcp_client_init(&client, "127.0.0.1", 12345);

    // Close socket to force read error
    close(client.fd);
    client.fd = -1;

    char buf[1];
    int result = tcp_client_process(&client, buf, 1);
    assert_equal_int(result, -1, "read with invalid socket returns error");
    tcp_client_free(&client);
}

void test_tcp_client_free(void)
{
    tcp_client_t client;
    tcp_client_init(&client, "127.0.0.1", 12345);
    tcp_client_free(&client);
    assert_equal_int(client.fd, -1, "socket reset");
}

void test_tcp_client_partial_read(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        usleep(200000);
        tcp_client_t client;
        int client_init = tcp_client_init(&client, "127.0.0.1", port);
        assert_equal_int(client_init, 0, "client init successful");

        char buf[8];
        int n1 = 0;
        int attempts = 0;
        while (n1 <= 0 && attempts++ < 50)
        {
            n1 = tcp_client_process(&client, buf, sizeof(buf));
            usleep(10000);
        }
        assert_equal_int(n1, 8, "first read 8 bytes");
        assert_memory(buf, (void *)"hello wo", 8, "first chunk matches");

        int n2 = tcp_client_process(&client, buf, sizeof(buf));
        assert_equal_int(n2, 8, "second read 8 bytes");
        assert_memory(buf, (void *)"rld! tes", 8, "second chunk matches");

        int n3 = tcp_client_process(&client, buf, sizeof(buf));
        assert_equal_int(n3, 8, "third read 8 bytes");
        assert_memory(buf, (void *)"t data", 6, "third chunk matches");

        tcp_client_free(&client);
        exit(0);
    }

    char dummy_buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 50)
    {
        tcp_server_process(&server, dummy_buf, 1);
        usleep(10000);
    }
    assert_equal_int(server.num_clients, 1, "server accepted client");

    tcp_server_broadcast(&server, "hello world! test data", 24);

    waitpid(pid, NULL, 0);
    tcp_server_free(&server);
}

void test_tcp_client_connection_in_progress(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        tcp_client_t client;
        int client_init = tcp_client_init(&client, "127.0.0.1", port);
        assert_equal_int(client_init, 0, "client init successful");

        char buf[16];
        int n = tcp_client_process(&client, buf, sizeof(buf));
        assert_equal_int(n, 0, "connection in progress, no data");

        usleep(200000); // Allow connection to establish

        n = tcp_client_process(&client, buf, sizeof(buf));
        assert_equal_int(n, 4, "read 4 bytes after connection");
        assert_memory(buf, (void *)"test", 4, "data matches");

        tcp_client_free(&client);
        exit(0);
    }

    usleep(100000); // Let child start connecting

    char dummy_buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 50)
    {
        tcp_server_process(&server, dummy_buf, 1);
        usleep(10000);
    }
    assert_equal_int(server.num_clients, 1, "server accepted client");

    tcp_server_broadcast(&server, "test", 4);

    waitpid(pid, NULL, 0);
    tcp_server_free(&server);
}

void test_tcp_client_write_error(void)
{
    tcp_server_t server;
    tcp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        tcp_client_t client;
        int client_init = tcp_client_init(&client, "127.0.0.1", port);
        assert_equal_int(client_init, 0, "client init successful");

        char buf[16];
        int n = 0;
        int attempts = 0;
        while (client.fd >= 0 && attempts++ < 50)
        {
            n = tcp_client_process(&client, buf, sizeof(buf));
            if (n == -1)
                break;
            usleep(10000);
        }

        assert_equal_int(n, -1, "client detected server disconnect");
        assert_equal_int(client.fd, -1, "client fd reset on disconnect");

        tcp_client_free(&client);
        exit(0);
    }

    usleep(200000); // Give child time to start connecting

    char dummy_buf[1];
    int attempts = 0;
    while (server.num_clients == 0 && attempts++ < 50)
    {
        tcp_server_process(&server, dummy_buf, 1);
        usleep(10000);
    }
    assert_equal_int(server.num_clients, 1, "server accepted client");

    tcp_server_free(&server); // Disconnect server immediately

    waitpid(pid, NULL, 0);
}

#endif
