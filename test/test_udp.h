#ifndef TEST_UDP_H
#define TEST_UDP_H

#include "test.h"
#include "udp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void test_udp_init_valid(void)
{
    udp_sender_t sender;
    int result = udp_sender_init(&sender, "127.0.0.1", 12345);
    assert_equal_int(result, 0, "init returns 0");
    assert_true(sender.sock >= 0, "socket valid");
    udp_sender_free(&sender);
}

void test_udp_init_broadcast(void)
{
    udp_sender_t sender;
    int result = udp_sender_init(&sender, "192.168.1.255", 12345);
    assert_equal_int(result, 0, "broadcast init returns 0");
    assert_true(sender.sock >= 0, "broadcast socket valid");
    udp_sender_free(&sender);
}

void test_udp_send_unicast(void)
{
    udp_sender_t sender;
    int init_result = udp_sender_init(&sender, "127.0.0.1", 12346);
    assert_equal_int(init_result, 0, "sender init successful");

    pid_t pid = fork();
    if (pid == 0)
    {
        usleep(100000); // Let parent initialize
        int recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
        assert_true(recv_sock >= 0, "child socket created");

        struct sockaddr_in recv_addr;
        memset(&recv_addr, 0, sizeof(recv_addr));
        recv_addr.sin_family = AF_INET;
        recv_addr.sin_port = htons(12346);
        recv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        int bind_result = bind(recv_sock, (struct sockaddr *)&recv_addr, sizeof(recv_addr));
        assert_equal_int(bind_result, 0, "child bind successful");

        // Set timeout to prevent hanging
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char buf[16];
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        int n = recvfrom(recv_sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&sender_addr, &sender_len);
        close(recv_sock);

        if (n < 0)
        {
            exit(1); // Timeout/failure
        }
        else
        {
            assert_equal_int(n, 4, "received 4 bytes");
            assert_memory(buf, (void *)"test", 4, "data matches");
            exit(0); // Success
        }
    }

    usleep(150000); // Let child bind (increased time)
    udp_sender_send(&sender, "test", 4);

    int status;
    waitpid(pid, &status, 0);
    udp_sender_free(&sender);

    assert_equal_int(WEXITSTATUS(status), 0, "child test passed");
}

void test_udp_send_broadcast(void)
{
    // Skip broadcast test for now - requires proper network setup
    // This would need a real broadcast-capable interface
    assert_true(1, "broadcast test skipped");
}

void test_udp_free(void)
{
    udp_sender_t sender;
    udp_sender_init(&sender, "127.0.0.1", 12345);
    udp_sender_free(&sender);
    assert_equal_int(sender.sock, -1, "socket reset");
}

void test_udp_init_invalid_address(void)
{
    udp_sender_t sender;

    // Empty address
    int result = udp_sender_init(&sender, "", 12345);
    assert_equal_int(result, -1, "empty address rejected");

    // Too long address
    char long_addr[INET_ADDRSTRLEN + 10];
    memset(long_addr, '1', sizeof(long_addr) - 1);
    long_addr[sizeof(long_addr) - 1] = '\0';
    result = udp_sender_init(&sender, long_addr, 12345);
    assert_equal_int(result, -1, "too long address rejected");

    // Malformed addresses (inet_pton will reject these)
    result = udp_sender_init(&sender, "192.168.1", 12345);
    assert_equal_int(result, -1, "malformed address rejected");

    result = udp_sender_init(&sender, "192.168.abc.1", 12345);
    assert_equal_int(result, -1, "non-numeric address rejected");

    result = udp_sender_init(&sender, "999.999.999.999", 12345);
    assert_equal_int(result, -1, "invalid octet values rejected");
}

void test_udp_send_error_handling(void)
{
    udp_sender_t sender;
    int init_result = udp_sender_init(&sender, "127.0.0.1", 12346);
    assert_equal_int(init_result, 0, "sender init successful");

    // Close socket to force send error
    close(sender.sock);
    sender.sock = -1;

    int send_result = udp_sender_send(&sender, "test", 4);
    assert_equal_int(send_result, -1, "send with invalid socket returns error");
    udp_sender_free(&sender);
}

void test_udp_send_broadcast_config(void)
{
    udp_sender_t sender;
    int result = udp_sender_init(&sender, "192.168.1.255", 12349);
    assert_equal_int(result, 0, "broadcast sender init successful");

    // Verify socket was created (can't easily test SO_BROADCAST without network)
    assert_true(sender.sock >= 0, "broadcast socket valid");
    udp_sender_free(&sender);
}

void test_udp_server_init_valid(void)
{
    udp_server_t server;
    int result = udp_server_init(&server, 0);
    assert_equal_int(result, 0, "init returns 0");
    assert_true(server.sock >= 0, "socket valid");
    udp_server_free(&server);
}

void test_udp_server_process_timeout(void)
{
    udp_server_t server;
    udp_server_init(&server, 0);
    char buf[1];
    int result = udp_server_process(&server, buf, 1);
    assert_equal_int(result, 0, "timeout returns 0");
    udp_server_free(&server);
}

void test_udp_server_receive_data(void)
{
    udp_server_t server;
    udp_server_init(&server, 0);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(server.sock, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid == 0)
    {
        usleep(200000);
        int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(caddr));
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(port);
        caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        sendto(client_sock, "test", 4, 0, (struct sockaddr *)&caddr, sizeof(caddr));
        close(client_sock);
        exit(0);
    }

    char buf[16];
    int n = 0;
    int attempts = 0;
    while (n <= 0 && attempts++ < 20)
        n = udp_server_process(&server, buf, sizeof(buf));

    assert_equal_int(n, 4, "read 4 bytes");
    assert_memory(buf, (void *)"test", 4, "data matches");

    waitpid(pid, NULL, 0);
    udp_server_free(&server);
}

void test_udp_server_free(void)
{
    udp_server_t server;
    udp_server_init(&server, 0);
    udp_server_free(&server);
    assert_equal_int(server.sock, -1, "socket reset");
}

#endif
