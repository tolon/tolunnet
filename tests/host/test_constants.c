/*
 * test_constants.c — Verify Tolunnet definitions match Roadshow SDK 1.8.
 */
#define DEVICES_TIMER_H 1
#include <sys/socket.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/errno.h>

#include "tn_test.h"

TN_TEST(address_families_and_socket_types)
{
    TN_ASSERT_EQ(AF_INET, 2);
    TN_ASSERT_EQ(AF_UNSPEC, 0);
    TN_ASSERT_EQ(SOCK_STREAM, 1);
    TN_ASSERT_EQ(SOCK_DGRAM, 2);
    TN_ASSERT_EQ(SOCK_RAW, 3);
}

TN_TEST(ip_protocols)
{
    TN_ASSERT_EQ(IPPROTO_IP, 0);
    TN_ASSERT_EQ(IPPROTO_ICMP, 1);
    TN_ASSERT_EQ(IPPROTO_TCP, 6);
    TN_ASSERT_EQ(IPPROTO_UDP, 17);
    TN_ASSERT_EQ(IPPROTO_RAW, 255);
}

TN_TEST(socket_options)
{
    TN_ASSERT_EQ(SOL_SOCKET, 0xffff);
    TN_ASSERT_EQ(SO_REUSEADDR, 0x0004);
    TN_ASSERT_EQ(SO_KEEPALIVE, 0x0008);
    TN_ASSERT_EQ(SO_BROADCAST, 0x0020);
    TN_ASSERT_EQ(SO_LINGER, 0x0080);
    TN_ASSERT_EQ(SO_OOBINLINE, 0x0100);
    TN_ASSERT_EQ(SO_SNDBUF, 0x1001);
    TN_ASSERT_EQ(SO_RCVBUF, 0x1002);
    TN_ASSERT_EQ(SO_SNDTIMEO, 0x1005);
    TN_ASSERT_EQ(SO_RCVTIMEO, 0x1006);
    TN_ASSERT_EQ(SO_ERROR, 0x1007);
    TN_ASSERT_EQ(SO_TYPE, 0x1008);
}

TN_TEST(protocol_options)
{
    TN_ASSERT_EQ(TCP_NODELAY, 0x0001);
    TN_ASSERT_EQ(TCP_MAXSEG, 0x0002);
    TN_ASSERT_EQ(TCP_KEEPIDLE, 0x0003);
    TN_ASSERT_EQ(TCP_KEEPINTVL, 0x0004);
    TN_ASSERT_EQ(TCP_KEEPCNT, 0x0005);
    TN_ASSERT_EQ(IP_HDRINCL, 2);
    TN_ASSERT_EQ(IP_TOS, 3);
    TN_ASSERT_EQ(IP_TTL, 4);
    TN_ASSERT_EQ(IP_MULTICAST_TTL, 10);
    TN_ASSERT_EQ(IP_MULTICAST_LOOP, 11);
    TN_ASSERT_EQ(IP_ADD_MEMBERSHIP, 12);
    TN_ASSERT_EQ(IP_DROP_MEMBERSHIP, 13);
}

TN_TEST(ioctls)
{
    TN_ASSERT_EQ((unsigned long)FIONREAD, 0x4004667fUL);
    TN_ASSERT_EQ((unsigned long)FIONBIO,  0x8004667eUL);
    TN_ASSERT_EQ((unsigned long)FIOASYNC, 0x8004667dUL);
}

TN_TEST(errnos)
{
    TN_ASSERT_EQ(EINTR, 4);
    TN_ASSERT_EQ(EBADF, 9);
    TN_ASSERT_EQ(EINVAL, 22);
    TN_ASSERT_EQ(EWOULDBLOCK, 35);
    TN_ASSERT_EQ(EINPROGRESS, 36);
    TN_ASSERT_EQ(ENOTSOCK, 38);
    TN_ASSERT_EQ(ENOPROTOOPT, 42);
    TN_ASSERT_EQ(EPROTONOSUPPORT, 43);
    TN_ASSERT_EQ(ESOCKTNOSUPPORT, 44);
    TN_ASSERT_EQ(EOPNOTSUPP, 45);
    TN_ASSERT_EQ(EADDRINUSE, 48);
    TN_ASSERT_EQ(ECONNRESET, 54);
    TN_ASSERT_EQ(EISCONN, 56);
    TN_ASSERT_EQ(ENOTCONN, 57);
}

int main(void)
{
    TN_TEST_RUN(address_families_and_socket_types);
    TN_TEST_RUN(ip_protocols);
    TN_TEST_RUN(socket_options);
    TN_TEST_RUN(protocol_options);
    TN_TEST_RUN(ioctls);
    TN_TEST_RUN(errnos);
    TN_TEST_PLAN();
    return tn_test_failures();
}
