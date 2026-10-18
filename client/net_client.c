#include "net_client.h"

#include <string.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

static SOCKET sock = INVALID_SOCKET;
static struct sockaddr_in server_addr;
static int winsock_started = 0;
static SOCKET probe_sock = INVALID_SOCKET;
static struct sockaddr_in probe_addr;
static ULONGLONG next_ping_at = 0;
static ULONGLONG ping_sent_at = 0;
static ULONGLONG last_pong_at = 0;
static int ping_pending = 0;
static int last_ping = -1;
static unsigned int ping_sent_total = 0;
static unsigned int ping_received_total = 0;
static unsigned int ping_lost_total = 0;
static ULONGLONG traffic_window_start = 0;
static unsigned long long traffic_in_window = 0;
static unsigned long long traffic_out_window = 0;
static unsigned int traffic_in_rate = 0;
static unsigned int traffic_out_rate = 0;

static void update_traffic_rates(void)
{
    ULONGLONG now = GetTickCount64();
    if (traffic_window_start == 0)
        traffic_window_start = now;
    ULONGLONG elapsed = now - traffic_window_start;
    if (elapsed >= 1000)
    {
        traffic_in_rate = (unsigned int)(traffic_in_window * 1000 / elapsed);
        traffic_out_rate = (unsigned int)(traffic_out_window * 1000 / elapsed);
        traffic_in_window = 0;
        traffic_out_window = 0;
        traffic_window_start = now;
    }
}

static void record_received_bytes(unsigned int bytes)
{
    traffic_in_window += bytes;
    update_traffic_rates();
}

static void record_sent_bytes(unsigned int bytes)
{
    traffic_out_window += bytes;
    update_traffic_rates();
}

static void accept_ping_reply(void)
{
    if (!ping_pending)
        return;
    ULONGLONG now = GetTickCount64();
    last_ping = (int)(now - ping_sent_at);
    last_pong_at = now;
    ping_pending = 0;
    ping_received_total++;
}

int init_network(const char *ip, int port)
{
    if (ip == NULL || ip[0] == '\0' || port < 1 || port > 65535)
        return 0;

    if (!winsock_started)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return 0;

        winsock_started = 1;
    }

    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    char service[6];
    if (snprintf(service, sizeof(service), "%d", port) < 1)
        return 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *addresses = NULL;
    if (getaddrinfo(ip, service, &hints, &addresses) != 0 ||
        addresses == NULL)
    {
        if (addresses != NULL)
            freeaddrinfo(addresses);
        return 0;
    }

    if (addresses->ai_addrlen < sizeof(server_addr))
    {
        freeaddrinfo(addresses);
        return 0;
    }

    memcpy(&server_addr, addresses->ai_addr, sizeof(server_addr));
    freeaddrinfo(addresses);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return 0;

    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
        return 0;
    }

    next_ping_at = 0;
    ping_sent_at = 0;
    last_pong_at = 0;
    ping_pending = 0;
    last_ping = -1;
    ping_sent_total = ping_received_total = ping_lost_total = 0;
    traffic_window_start = GetTickCount64();
    traffic_in_window = traffic_out_window = 0;
    traffic_in_rate = traffic_out_rate = 0;

    return 1;
}

void network_close(void)
{
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    if (winsock_started && probe_sock == INVALID_SOCKET)
    {
        WSACleanup();
        winsock_started = 0;
    }
}

int network_probe_start(const char *ip, int port)
{
    if (ip == NULL || ip[0] == '\0' || port < 1 || port > 65535)
        return 0;

    network_probe_close();
    if (!winsock_started)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return 0;
        winsock_started = 1;
    }
    char service[6];
    if (snprintf(service, sizeof(service), "%d", port) < 1)
        return 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(ip, service, &hints, &addresses) != 0 || addresses == NULL)
    {
        if (addresses != NULL)
            freeaddrinfo(addresses);
        return 0;
    }
    if (addresses->ai_addrlen < sizeof(probe_addr))
    {
        freeaddrinfo(addresses);
        return 0;
    }
    memcpy(&probe_addr, addresses->ai_addr, sizeof(probe_addr));
    freeaddrinfo(addresses);

    probe_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe_sock == INVALID_SOCKET)
        return 0;
    u_long mode = 1;
    if (ioctlsocket(probe_sock, FIONBIO, &mode) != 0)
    {
        closesocket(probe_sock);
        probe_sock = INVALID_SOCKET;
        return 0;
    }

    next_ping_at = 0;
    ping_sent_at = 0;
    last_pong_at = 0;
    ping_pending = 0;
    last_ping = -1;
    ping_sent_total = ping_received_total = ping_lost_total = 0;
    traffic_window_start = GetTickCount64();
    traffic_in_window = traffic_out_window = 0;
    traffic_in_rate = traffic_out_rate = 0;
    return 1;
}

void network_probe_close(void)
{
    if (probe_sock != INVALID_SOCKET)
    {
        closesocket(probe_sock);
        probe_sock = INVALID_SOCKET;
    }
    if (winsock_started && sock == INVALID_SOCKET)
    {
        WSACleanup();
        winsock_started = 0;
    }
}

static void poll_probe_reply(void)
{
    if (probe_sock == INVALID_SOCKET)
        return;
    for (;;)
    {
        uint16_t reply[2];
        struct sockaddr_in from_addr;
        int from_len = sizeof(from_addr);
        int bytes = recvfrom(probe_sock, (char *)reply, sizeof(reply), 0,
                             (struct sockaddr *)&from_addr, &from_len);
        if (bytes == SOCKET_ERROR)
            break;
        record_received_bytes((unsigned int)bytes);
        if (bytes == (int)sizeof(reply) &&
            from_addr.sin_addr.s_addr == probe_addr.sin_addr.s_addr &&
            from_addr.sin_port == probe_addr.sin_port &&
            ntohs(reply[0]) == NET_PING_MAGIC &&
            ntohs(reply[1]) == NET_PING_RESPONSE)
            accept_ping_reply();
    }
}

void network_ping_tick(void)
{
    SOCKET ping_sock = sock != INVALID_SOCKET ? sock : probe_sock;
    const struct sockaddr_in *ping_addr = sock != INVALID_SOCKET ? &server_addr : &probe_addr;
    if (ping_sock == INVALID_SOCKET)
        return;

    if (sock == INVALID_SOCKET)
        poll_probe_reply();

    ULONGLONG now = GetTickCount64();
    if (ping_pending && now - ping_sent_at >= 900)
    {
        ping_pending = 0;
        ping_lost_total++;
    }
    if (now < next_ping_at || ping_pending)
        return;

    uint16_t request[2] = {htons(NET_PING_MAGIC), htons(NET_PING_REQUEST)};
    int sent = sendto(ping_sock, (const char *)request, sizeof(request), 0,
                      (const struct sockaddr *)ping_addr, sizeof(*ping_addr));
    if (sent == (int)sizeof(request))
    {
        record_sent_bytes((unsigned int)sent);
        ping_sent_total++;
        ping_sent_at = now;
        ping_pending = 1;
        next_ping_at = now + 1000;
    }
    else
    {
        next_ping_at = now + 1000;
    }
}

int network_ping_ms(void) { return last_ping; }
int network_server_online(void)
{
    if (ping_sent_total == 0)
        return -1;
    return last_pong_at != 0 && GetTickCount64() - last_pong_at < 2600 ? 1 : 0;
}
int network_packet_loss_percent(void)
{
    if (ping_sent_total == 0)
        return 0;
    return (int)(ping_lost_total * 100 / ping_sent_total);
}
unsigned int network_bytes_in_per_second(void) { update_traffic_rates(); return traffic_in_rate; }
unsigned int network_bytes_out_per_second(void) { update_traffic_rates(); return traffic_out_rate; }
void network_get_ping_counters(unsigned int *sent, unsigned int *received)
{
    if (sent) *sent = ping_sent_total;
    if (received) *received = ping_received_total;
}

void send_input(uint8_t input_bits, int16_t angle_deg, uint16_t color_id, uint16_t weapon_id)
{
    if (sock == INVALID_SOCKET)
        return;

    uint16_t net_input[3];
    net_input[0] = htons((uint16_t)input_bits);
    net_input[1] = htons((uint16_t)angle_deg);
    net_input[2] = htons((uint16_t)(color_id * NET_WEAPON_COUNT + weapon_id));

    int sent = sendto(sock, (const char *)net_input, sizeof(net_input), 0,
                      (const struct sockaddr *)&server_addr, sizeof(server_addr));
    if (sent == (int)sizeof(net_input))
        record_sent_bytes((unsigned int)sent);
}

int receive_state(
    struct RemotePlayer *out_players,
    struct NetBullet *out_bullets,
    int *out_my_id,
    uint16_t *out_phase,
    uint16_t *out_seconds_left,
    int *out_winner_id)
{
    if (sock == INVALID_SOCKET)
        return 0;

    uint16_t packet[STATE_PACKET_WORDS];
    int got_data = 0;

    for (;;)
    {
        struct sockaddr_in from_addr;
        int from_len = sizeof(from_addr);

        int bytes = recvfrom(
            sock, (char *)packet, sizeof(packet), 0,
            (struct sockaddr *)&from_addr, &from_len);

        if (bytes == SOCKET_ERROR)
            break;

        record_received_bytes((unsigned int)bytes);
        if (bytes == 4)
        {
            uint16_t reply[2];
            memcpy(reply, packet, sizeof(reply));
            if (from_addr.sin_addr.s_addr == server_addr.sin_addr.s_addr &&
                from_addr.sin_port == server_addr.sin_port &&
                ntohs(reply[0]) == NET_PING_MAGIC &&
                ntohs(reply[1]) == NET_PING_RESPONSE)
                accept_ping_reply();
            continue;
        }
        if (bytes != (int)sizeof(packet))
            continue;

        if (from_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr ||
            from_addr.sin_port != server_addr.sin_port)
        {
            continue;
        }

        *out_my_id = ntohs(packet[0]);
        *out_phase = ntohs(packet[1]);
        *out_seconds_left = ntohs(packet[2]);
        uint16_t packet_my_id = ntohs(packet[0]);
        uint16_t packet_phase = ntohs(packet[1]);
        uint16_t packet_seconds = ntohs(packet[2]);
        uint16_t packet_winner = ntohs(packet[3]);

        if (packet_my_id >= MAX_PLAYERS || packet_phase > NET_PHASE_RESULTS ||
            packet_seconds > 180 ||
            (packet_winner != NET_NO_WINNER && packet_winner >= MAX_PLAYERS))
        {
            continue;
        }

        int valid = 1;
        int validation_offset = 4;
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            uint16_t active = ntohs(packet[validation_offset + 0]);
            uint16_t x = ntohs(packet[validation_offset + 1]);
            uint16_t y = ntohs(packet[validation_offset + 2]);
            int16_t angle = (int16_t)ntohs(packet[validation_offset + 3]);
            uint16_t color = ntohs(packet[validation_offset + 4]);
            uint16_t hp = ntohs(packet[validation_offset + 5]);
            uint16_t ready = ntohs(packet[validation_offset + 8]);
            uint16_t connected = ntohs(packet[validation_offset + 9]);
            uint16_t weapon = ntohs(packet[validation_offset + 10]);

            if (active > 1 || ready > 1 || connected > 1 || weapon >= NET_WEAPON_COUNT ||
                (active && (x > NET_MAP_SIZE || y > NET_MAP_SIZE ||
                            angle < -180 || angle > 180 || color >= NET_COLOR_COUNT || hp > 100)))
            {
                valid = 0;
                break;
            }
            validation_offset += NET_PLAYER_WORDS;
        }

        if (valid)
        {
            for (int i = 0; i < MAX_BULLETS; i++)
            {
                uint16_t active = ntohs(packet[validation_offset + 0]);
                uint16_t x = ntohs(packet[validation_offset + 1]);
                uint16_t y = ntohs(packet[validation_offset + 2]);
                uint16_t weapon = ntohs(packet[validation_offset + 3]);
                if (active > 1 || weapon >= NET_WEAPON_COUNT || (active && (x > NET_MAP_SIZE || y > NET_MAP_SIZE)))
                {
                    valid = 0;
                    break;
                }
                validation_offset += NET_BULLET_WORDS;
            }
        }

        if (!valid)
            continue;

        *out_my_id = packet_my_id;
        *out_phase = packet_phase;
        *out_seconds_left = packet_seconds;
        *out_winner_id = packet_winner;

        int offset = 4;

        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            out_players[i].active = ntohs(packet[offset + 0]);
            out_players[i].x = ntohs(packet[offset + 1]);
            out_players[i].y = ntohs(packet[offset + 2]);
            out_players[i].angle = (int16_t)ntohs(packet[offset + 3]);
            out_players[i].color = ntohs(packet[offset + 4]);
            out_players[i].hp = ntohs(packet[offset + 5]);
            out_players[i].kills = ntohs(packet[offset + 6]);
            out_players[i].deaths = ntohs(packet[offset + 7]);
            out_players[i].ready = ntohs(packet[offset + 8]);
            out_players[i].connected = ntohs(packet[offset + 9]);
            out_players[i].weapon = ntohs(packet[offset + 10]);
            offset += NET_PLAYER_WORDS;
        }

        for (int i = 0; i < MAX_BULLETS; i++)
        {
            out_bullets[i].active = ntohs(packet[offset + 0]);
            out_bullets[i].x = ntohs(packet[offset + 1]);
            out_bullets[i].y = ntohs(packet[offset + 2]);
            out_bullets[i].weapon = ntohs(packet[offset + 3]);
            offset += NET_BULLET_WORDS;
        }

        got_data = 1;
    }

    return got_data;
}