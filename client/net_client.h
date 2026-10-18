#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include <stdint.h>

#define MAX_PLAYERS 10
#define MAX_BULLETS 20
#define NET_COLOR_COUNT 5
#define NET_WEAPON_COUNT 3
#define NET_PLAYER_WORDS 11
#define NET_BULLET_WORDS 4
#define NET_PING_MAGIC 0x4E50
#define NET_PING_REQUEST 0x494E
#define NET_PING_RESPONSE 0x4F4B
#define NET_MIN_PLAYERS 2
#define NET_INPUT_READY 0x20
#define NET_INPUT_REMATCH 0x40
#define NET_MAP_SIZE 2000


#define NET_PHASE_LOBBY 0
#define NET_PHASE_COUNTDOWN 1
#define NET_PHASE_PLAYING 2
#define NET_PHASE_RESULTS 3

#define NET_NO_WINNER 65535
#define STATE_PACKET_WORDS (4 + MAX_PLAYERS * NET_PLAYER_WORDS + MAX_BULLETS * NET_BULLET_WORDS)

struct RemotePlayer
{
    int active;
    uint16_t x;
    uint16_t y;
    int16_t angle;
    uint16_t color;
    uint16_t hp;
    uint16_t kills;
    uint16_t deaths;
    int ready;
    int connected;
    uint16_t weapon;
};

struct NetBullet
{
    int active;
    uint16_t x;
    uint16_t y;
    uint16_t weapon;
};

#ifdef __cplusplus
extern "C"
{
#endif

int init_network(const char *ip, int port);
void network_close(void);
int network_probe_start(const char *ip, int port);
void network_probe_close(void);
void network_ping_tick(void);
int network_ping_ms(void);
int network_packet_loss_percent(void);
unsigned int network_bytes_in_per_second(void);
unsigned int network_bytes_out_per_second(void);
int network_server_online(void);
void network_get_ping_counters(unsigned int *sent, unsigned int *received);

void send_input(uint8_t input_bits, int16_t angle_deg, uint16_t color_id, uint16_t weapon_id);

int receive_state(
    struct RemotePlayer *out_players,
    struct NetBullet *out_bullets,
    int *out_my_id,
    uint16_t *out_phase,
    uint16_t *out_seconds_left,
    int *out_winner_id);

#ifdef __cplusplus
}
#endif

#endif