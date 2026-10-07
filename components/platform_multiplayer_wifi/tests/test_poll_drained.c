// SPDX-License-Identifier: MIT
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "mocks/mock.h"
#include "p4/multiplayer.h"

/* Deterministic socket results exercise the real bounded poll without opening
 * sockets. ESP task/mutex mocks do not prove concurrent-worker behavior. */
enum { TEST_SOCKET = 123456 };
static uint8_t incoming[P4_MP_MAX_DATAGRAM_BYTES + 1];
static size_t incoming_length;
static unsigned incoming_count, receive_calls, delivered;
static int terminal_errno = EAGAIN;
static bool close_from_callback;

static ssize_t test_recvfrom(int fd, void *bytes, size_t capacity, int flags,
    struct sockaddr *address, socklen_t *address_length)
{
    assert(fd == TEST_SOCKET && flags == MSG_DONTWAIT);
    ++receive_calls;
    if (!incoming_count) { errno = terminal_errno; return -1; }
    --incoming_count;
    assert(incoming_length <= capacity);
    memcpy(bytes, incoming, incoming_length);
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(42424),
        .sin_addr.s_addr = htonl(UINT32_C(0x7f000002))};
    assert(*address_length >= sizeof(peer));
    memcpy(address, &peer, sizeof(peer));
    *address_length = sizeof(peer);
    return (ssize_t)incoming_length;
}
static int test_socket(int domain, int type, int protocol)
{ (void)domain; (void)type; (void)protocol; return TEST_SOCKET; }
static int test_bind(int fd, const struct sockaddr *address, socklen_t length)
{ (void)fd; (void)address; (void)length; return 0; }
static int test_setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{ (void)fd; (void)level; (void)option; (void)value; (void)length; return 0; }
static int test_fcntl(int fd, int command, int value)
{ (void)fd; (void)command; (void)value; return 0; }
static int test_close(int fd) { (void)fd; return 0; }
static ssize_t test_recv(int fd, void *bytes, size_t length, int flags)
{ (void)fd; (void)bytes; (void)length; (void)flags; errno = EAGAIN; return -1; }

#define recvfrom test_recvfrom
#define socket test_socket
#define bind test_bind
#define setsockopt test_setsockopt
#define fcntl test_fcntl
#define close test_close
#define recv test_recv
#include "../src/platform_multiplayer_wifi.c"
#undef recvfrom
#undef socket
#undef bind
#undef setsockopt
#undef fcntl
#undef close
#undef recv

esp_err_t platform_ble_host_start(void) { return ESP_OK; }
bool platform_ble_host_ready(void) { return true; }
platform_ble_host_status_t platform_ble_host_status(void)
{ return (platform_ble_host_status_t){.state = PLATFORM_BLE_HOST_READY}; }

static bool drained(void)
{
#ifdef P4_TEST_ASSUME_NO_DRAIN_HINT
    /* Reproduce the pre-seam, conservative unknown result for the red run. */
    return false;
#else
    return platform_multiplayer_wifi_poll_drained();
#endif
}

static void frame(void *context, uint64_t route, const uint8_t *bytes, size_t length)
{
    (void)context; (void)route;
    p4_mp_packet_view_t packet;
    assert(p4_mp_packet_decode(bytes, length, &packet) == P4_MP_OK);
    ++delivered;
    if (close_from_callback) platform_multiplayer_wifi_disable();
}

static void prepare(unsigned count)
{
    s_lock = (void *)1;
    close_link_locked();
    s_socket = TEST_SOCKET;
    s_mode = MODE_HOST;
    s_session = 7;
    s_local_ip = htonl(UINT32_C(0x7f000001));
    s_associated = true;
    s_status.ready = true;
    s_handler = frame;
    s_context = NULL;
    terminal_errno = EAGAIN;
    incoming_count = count;
    receive_calls = delivered = 0;
    close_from_callback = false;
    const uint8_t payload[8] = {0};
    assert(p4_mp_packet_encode(P4_MP_PACKET_PING, 7, 42, 1, 0,
        payload, sizeof(payload), incoming, sizeof(incoming),
        &incoming_length) == P4_MP_OK);
}

static void expect_empty(void)
{
    incoming_count = 0;
    terminal_errno = EAGAIN;
    platform_multiplayer_wifi_poll();
    assert(drained());
}

int main(void)
{
    /* No socket must not reuse a previous EAGAIN from another operation. */
    errno = EAGAIN;
    platform_multiplayer_wifi_poll();
    assert(!drained());
    prepare(0);
    expect_empty();
    assert(receive_calls == 1 && delivered == 0);
    terminal_errno = EWOULDBLOCK;
    platform_multiplayer_wifi_poll();
    assert(drained());

    for (unsigned count = 1; count <= 7; ++count) {
        prepare(count);
        platform_multiplayer_wifi_poll();
        assert(drained() && delivered == count && receive_calls == count + 1);
    }
    for (unsigned count = 8; count <= 9; ++count) {
        prepare(count);
        platform_multiplayer_wifi_poll();
        assert(!drained() && delivered == 8 && receive_calls == 8);
        platform_multiplayer_wifi_poll();
        assert(drained() && delivered == count);
    }

    /* Rejected and zero-length datagrams consume the same bounded budget. */
    const size_t rejected_lengths[] = {0, 1, P4_MP_MAX_DATAGRAM_BYTES + 1};
    for (unsigned i = 0; i < sizeof(rejected_lengths) / sizeof(rejected_lengths[0]); ++i) {
        prepare(8);
        memset(incoming, 0, sizeof(incoming));
        incoming_length = rejected_lengths[i];
        platform_multiplayer_wifi_poll();
        assert(!drained() && delivered == 0 && receive_calls == 8);
        platform_multiplayer_wifi_poll();
        assert(drained() && delivered == 0);
        incoming_count = 7;
        platform_multiplayer_wifi_poll();
        assert(drained() && delivered == 0);
    }

    const int failures[] = {EINTR, EBADF, EIO};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        prepare(0);
        expect_empty();
        terminal_errno = failures[i];
        platform_multiplayer_wifi_poll();
        assert(!drained());
    }
    prepare(0);
    expect_empty();
    s_socket = -1;
    errno = EAGAIN;
    platform_multiplayer_wifi_poll();
    assert(!drained() && receive_calls == 1);
    s_lock = NULL;
    assert(!drained());

    prepare(0); expect_empty(); close_link_locked(); assert(!drained());
    prepare(0); expect_empty();
    assert(open_socket_locked() == ESP_OK); assert(!drained());
    expect_empty(); platform_multiplayer_wifi_reset_route(); assert(!drained());
    expect_empty(); platform_multiplayer_wifi_disable(); assert(!drained());
    prepare(0); expect_empty();
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, NULL);
    assert(!drained());
    expect_empty();
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    assert(!drained());
    prepare(0); expect_empty();
    assert(platform_multiplayer_wifi_host(8, 13) == ESP_OK); assert(!drained());

    /* A callback can change the mode between receives; never resurrect true. */
    prepare(1);
    close_from_callback = true;
    platform_multiplayer_wifi_poll();
    assert(delivered == 1 && !drained());
    puts("Wi-Fi drain hint: empty, budget, rejected frames, errors and lifecycle passed");
    return 0;
}
