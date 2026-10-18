#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
#define MAX_PLAYERS 10
#define MAX_BULLETS 20
#define PLAYER_COLOR_COUNT 5
#define WEAPON_COUNT 3
#define NET_PING_MAGIC 0x4E50
#define NET_PING_REQUEST 0x494E
#define NET_PING_RESPONSE 0x4F4B

#define MAP_SIZE 2000.0f
#define PLAYER_RADIUS 20.0f
#define PLAYER_SPEED 300.0f
#define BULLET_SPEED 840.0f
#define BULLET_HIT_RADIUS 20.0f

#define TICK_RATE 60.0
#define TICK_SECONDS (1.0 / TICK_RATE)
#define PLAYER_STALE_SECONDS 0.75
#define PLAYER_TIMEOUT_SECONDS 15.0
#define INPUT_RATE_LIMIT_SECONDS (1.0 / 120.0)
#define MAX_INPUT_PACKETS_PER_PASS 128
#define SHOOT_COOLDOWN_TICKS 12

#define COUNTDOWN_SECONDS 3.0
#define ROUND_SECONDS 180.0
#define RESULTS_SECONDS 10.0
#define KILLS_TO_WIN 10
#define MIN_PLAYERS_TO_START 2

#define PHASE_LOBBY 0
#define PHASE_COUNTDOWN 1
#define PHASE_PLAYING 2
#define PHASE_RESULTS 3

#define INPUT_READY 0x20
#define INPUT_REMATCH 0x40

#define NET_PLAYER_WORDS 11
#define NET_BULLET_WORDS 4
#define STATE_PACKET_WORDS (4 + MAX_PLAYERS * NET_PLAYER_WORDS + MAX_BULLETS * NET_BULLET_WORDS)

struct Player
{
    struct sockaddr_in addr;
    int active;

    uint16_t x;
    uint16_t y;
    int16_t angle;
    uint16_t color;
    uint16_t hp;
    uint16_t kills;
    uint16_t deaths;
    int ready;

    uint8_t input_bits;
    int shoot_cooldown;
    double last_seen;
    double last_input_at;
    uint16_t weapon;
};

struct Bullet
{
    int active;
    float x;
    float y;
    float vx;
    float vy;
    int owner_id;
    int weapon;
    int damage;
};

struct Match
{
    int phase;
    int winner_id;
    double phase_ends_at;
};

static double monotonic_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static float clampf(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}

static void spawn_player(struct Player *player, int slot)
{
    player->x = (uint16_t)(400 + slot * 80);
    player->y = 300;
    player->angle = 0;
    player->hp = 100;
    player->input_bits = 0;
    player->shoot_cooldown = 0;
}

static int find_player(
    struct Player players[MAX_PLAYERS],
    const struct sockaddr_in *client_addr)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].active &&
            players[i].addr.sin_addr.s_addr == client_addr->sin_addr.s_addr &&
            players[i].addr.sin_port == client_addr->sin_port)
        {
            return i;
        }
    }

    return -1;
}

static int find_free_player(struct Player players[MAX_PLAYERS])
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            return i;
    }

    return -1;
}

static void handle_input(
    struct Player players[MAX_PLAYERS],
    const uint16_t net_input[3],
    const struct sockaddr_in *client_addr,
    const struct Match *match,
    double now)
{
    uint16_t input_bits = ntohs(net_input[0]);
    int16_t angle = (int16_t)ntohs(net_input[1]);
    uint16_t packed_loadout = ntohs(net_input[2]);
    uint16_t color = packed_loadout / WEAPON_COUNT;
    uint16_t weapon = packed_loadout % WEAPON_COUNT;

    // Color and weapon are packed into the third input word and range checked.
    if (input_bits > 0x7Fu || angle < -180 || angle > 180 ||
        packed_loadout >= PLAYER_COLOR_COUNT * WEAPON_COUNT)
        return;

    int player_index = find_player(players, client_addr);

    if (player_index >= 0 &&
        players[player_index].last_input_at > 0.0 &&
        now - players[player_index].last_input_at < INPUT_RATE_LIMIT_SECONDS)
    {
        return;
    }

    if (player_index == -1)
    {
        player_index = find_free_player(players);
        if (player_index == -1)
            return;

        memset(&players[player_index], 0, sizeof(players[player_index]));
        players[player_index].active = 1;
        players[player_index].addr = *client_addr;

        spawn_player(&players[player_index], player_index);

        printf("[server] player connected at slot %d (port %d)\n",
               player_index, ntohs(client_addr->sin_port));
    }

    struct Player *player = &players[player_index];

    player->input_bits = (uint8_t)input_bits;
    player->angle = angle;
    player->color = color;
    player->weapon = weapon;
    if (match->phase == PHASE_RESULTS)
        player->ready = (input_bits & INPUT_REMATCH) != 0;
    else if (match->phase == PHASE_LOBBY)
        player->ready = (input_bits & INPUT_READY) != 0;
    else if (match->phase == PHASE_COUNTDOWN)
        player->ready =
            (input_bits & (INPUT_READY | INPUT_REMATCH)) != 0;
    else
        player->ready = 0;
    player->last_seen = now;
    player->last_input_at = now;
}

static int spawn_bullet(struct Bullet bullets[MAX_BULLETS],
                        const struct Player *player,
                        int owner_id,
                        float angle_offset,
                        float side_offset,
                        float speed,
                        int damage,
                        int weapon)
{
    float radians = ((float)player->angle *
                     (3.14159265358979323846f / 180.0f)) + angle_offset;
    float side_x = -sinf(radians);
    float side_y = cosf(radians);

    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (bullets[i].active)
            continue;

        bullets[i].active = 1;
        bullets[i].x = player->x + cosf(radians) * 35.0f + side_x * side_offset;
        bullets[i].y = player->y + sinf(radians) * 35.0f + side_y * side_offset;
        bullets[i].vx = cosf(radians) * speed;
        bullets[i].vy = sinf(radians) * speed;
        bullets[i].owner_id = owner_id;
        bullets[i].weapon = weapon;
        bullets[i].damage = damage;
        return 1;
    }
    return 0;
}

static int fire_bullet(struct Bullet bullets[MAX_BULLETS],
                       const struct Player *player,
                       int owner_id)
{
    if (player->weapon == 1)
        return spawn_bullet(bullets, player, owner_id, 0.0f, 0.0f, 610.0f, 55, 1);

    if (player->weapon == 2)
    {
        int fired = 0;
        fired += spawn_bullet(bullets, player, owner_id, -0.17f, 0.0f, 730.0f, 15, 2);
        fired += spawn_bullet(bullets, player, owner_id, 0.0f, 0.0f, 760.0f, 15, 2);
        fired += spawn_bullet(bullets, player, owner_id, 0.17f, 0.0f, 730.0f, 15, 2);
        return fired > 0;
    }

    int fired = 0;
    fired += spawn_bullet(bullets, player, owner_id, 0.0f, -8.0f, 840.0f, 22, 0);
    fired += spawn_bullet(bullets, player, owner_id, 0.0f, 8.0f, 840.0f, 22, 0);
    return fired > 0;
}
static void remove_player(
    struct Player players[MAX_PLAYERS],
    struct Bullet bullets[MAX_BULLETS],
    int player_index)
{
    players[player_index].active = 0;
    players[player_index].ready = 0;
    players[player_index].input_bits = 0;

    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (bullets[i].active &&
            bullets[i].owner_id == player_index)
        {
            bullets[i].active = 0;
        }
    }

    printf("[server] player at slot %d timed out\n", player_index);
}

static int active_player_count(const struct Player players[MAX_PLAYERS])
{
    int count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].active)
            count++;
    }

    return count;
}

static int everyone_is_ready(const struct Player players[MAX_PLAYERS])
{
    int count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        count++;
        if (!players[i].ready)
            return 0;
    }

    return count >= MIN_PLAYERS_TO_START;
}

static int find_winner(const struct Player players[MAX_PLAYERS])
{
    int best_kills = -1;
    int winner = -1;
    int tied = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        if ((int)players[i].kills > best_kills)
        {
            best_kills = players[i].kills;
            winner = i;
            tied = 0;
        }
        else if ((int)players[i].kills == best_kills)
        {
            tied = 1;
        }
    }

    return tied ? -1 : winner;
}

static void finish_round(
    struct Player players[MAX_PLAYERS],
    struct Bullet bullets[MAX_BULLETS],
    struct Match *match,
    double now,
    int forced_winner)
{
    match->phase = PHASE_RESULTS;
    match->winner_id = forced_winner >= 0
                           ? forced_winner
                           : find_winner(players);
    match->phase_ends_at = now + RESULTS_SECONDS;

    for (int i = 0; i < MAX_BULLETS; i++)
        bullets[i].active = 0;

    if (match->winner_id >= 0)
        printf("[server] round over; player %d wins\n", match->winner_id);
    else
        printf("[server] round over; draw\n");
}

static void simulate_tick(
    struct Player players[MAX_PLAYERS],
    struct Bullet bullets[MAX_BULLETS],
    struct Match *match,
    double tick_time)
{
    const float dt = (float)TICK_SECONDS;

    // Keep a disconnected pilot's slot briefly so the same UDP endpoint can rejoin.
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        double silent_seconds = tick_time - players[i].last_seen;
        if (silent_seconds > PLAYER_TIMEOUT_SECONDS)
        {
            remove_player(players, bullets, i);
        }
        else if (silent_seconds > PLAYER_STALE_SECONDS)
        {
            // A stale client must not keep moving, firing, or readying up.
            players[i].input_bits = 0;
            players[i].ready = 0;
        }
    }

    if (match->phase != PHASE_PLAYING)
        return;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        struct Player *player = &players[i];

        if (!player->active)
            continue;

        float move_x = 0.0f;
        float move_y = 0.0f;

        if (player->input_bits & 0x04)
            move_x -= 1.0f;
        if (player->input_bits & 0x08)
            move_x += 1.0f;
        if (player->input_bits & 0x01)
            move_y -= 1.0f;
        if (player->input_bits & 0x02)
            move_y += 1.0f;

        float length = sqrtf(move_x * move_x + move_y * move_y);

        if (length > 0.0f)
        {
            move_x /= length;
            move_y /= length;

            player->x = (uint16_t)clampf(
                (float)player->x + move_x * PLAYER_SPEED * dt,
                PLAYER_RADIUS,
                MAP_SIZE - PLAYER_RADIUS);

            player->y = (uint16_t)clampf(
                (float)player->y + move_y * PLAYER_SPEED * dt,
                PLAYER_RADIUS,
                MAP_SIZE - PLAYER_RADIUS);
        }

        if (player->shoot_cooldown > 0)
            player->shoot_cooldown--;

        if ((player->input_bits & 0x10) &&
            player->shoot_cooldown == 0)
        {
            if (fire_bullet(bullets, player, i))
                player->shoot_cooldown = player->weapon == 1 ? 32 :
                                          player->weapon == 2 ? 22 : SHOOT_COOLDOWN_TICKS;
        }
    }

    for (int b = 0; b < MAX_BULLETS; b++)
    {
        struct Bullet *bullet = &bullets[b];

        if (!bullet->active)
            continue;

        bullet->x += bullet->vx * dt;
        bullet->y += bullet->vy * dt;

        if (bullet->x < 0.0f || bullet->x > MAP_SIZE ||
            bullet->y < 0.0f || bullet->y > MAP_SIZE)
        {
            bullet->active = 0;
            continue;
        }

        for (int p = 0; p < MAX_PLAYERS; p++)
        {
            if (!players[p].active || p == bullet->owner_id)
                continue;

            float dx = bullet->x - players[p].x;
            float dy = bullet->y - players[p].y;

            if (dx * dx + dy * dy >
                BULLET_HIT_RADIUS * BULLET_HIT_RADIUS)
            {
                continue;
            }

            bullet->active = 0;

            if (players[p].hp > bullet->damage)
            {
                players[p].hp -= (uint16_t)bullet->damage;
            }
            else
            {
                players[p].deaths++;

                int owner = bullet->owner_id;
                if (owner >= 0 && owner < MAX_PLAYERS &&
                    players[owner].active && owner != p)
                {
                    players[owner].kills++;
                }

                players[p].hp = 100;
                players[p].x = (uint16_t)(200 + rand() % 1600);
                players[p].y = (uint16_t)(200 + rand() % 1600);

                if (owner >= 0 && owner < MAX_PLAYERS &&
                    players[owner].active &&
                    players[owner].kills >= KILLS_TO_WIN)
                {
                    finish_round(players, bullets, match,
                                 tick_time, owner);
                    return;
                }
            }

            break;
        }
    }
}

static void reset_round(
    struct Player players[MAX_PLAYERS],
    struct Bullet bullets[MAX_BULLETS],
    struct Match *match)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        players[i].kills = 0;
        players[i].deaths = 0;
        players[i].ready = 0;
        players[i].input_bits = 0;
        spawn_player(&players[i], i);
    }

    for (int i = 0; i < MAX_BULLETS; i++)
        bullets[i].active = 0;

    match->phase = PHASE_LOBBY;
    match->winner_id = -1;
    match->phase_ends_at = 0.0;

    printf("[server] returned to lobby\n");
}

static void start_rematch_countdown(
    struct Player players[MAX_PLAYERS],
    struct Bullet bullets[MAX_BULLETS],
    struct Match *match,
    double now)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        players[i].kills = 0;
        players[i].deaths = 0;
        players[i].input_bits = 0;
        spawn_player(&players[i], i);
    }

    for (int i = 0; i < MAX_BULLETS; i++)
        bullets[i].active = 0;

    match->phase = PHASE_COUNTDOWN;
    match->winner_id = -1;
    match->phase_ends_at = now + COUNTDOWN_SECONDS;

    printf("[server] rematch countdown started\n");
}

static void update_match_phase(
    struct Player players[MAX_PLAYERS],
    struct Bullet bullets[MAX_BULLETS],
    struct Match *match,
    double now)
{
    int count = active_player_count(players);
    int everyone_ready = everyone_is_ready(players);

    switch (match->phase)
    {
    case PHASE_LOBBY:
        if (everyone_ready)
        {
            match->phase = PHASE_COUNTDOWN;
            match->phase_ends_at = now + COUNTDOWN_SECONDS;
            match->winner_id = -1;
            printf("[server] countdown started\n");
        }
        break;

    case PHASE_COUNTDOWN:
        if (count == 0 || !everyone_ready)
        {
            match->phase = PHASE_LOBBY;
            match->phase_ends_at = 0.0;
            printf("[server] countdown cancelled\n");
        }
        else if (now >= match->phase_ends_at)
        {
            match->phase = PHASE_PLAYING;
            match->phase_ends_at = now + ROUND_SECONDS;
            printf("[server] round started\n");
        }
        break;

    case PHASE_PLAYING:
        if (count == 0)
        {
            match->phase = PHASE_LOBBY;
            match->phase_ends_at = 0.0;
            match->winner_id = -1;
        }
        else if (now >= match->phase_ends_at)
        {
            finish_round(players, bullets, match, now, -1);
        }
        break;

    case PHASE_RESULTS:
        if (everyone_ready)
        {
            start_rematch_countdown(players, bullets, match, now);
        }
        else if (now >= match->phase_ends_at)
        {
            reset_round(players, bullets, match);
        }
        break;
    }
}

static uint16_t seconds_remaining(const struct Match *match, double now)
{
    if (match->phase == PHASE_LOBBY)
        return 0;

    double remaining = match->phase_ends_at - now;
    if (remaining < 0.0)
        remaining = 0.0;

    return (uint16_t)ceil(remaining);
}

static uint16_t bullet_position(float position)
{
    position = clampf(position, 0.0f, MAP_SIZE);
    return (uint16_t)lroundf(position);
}

static void broadcast_state(
    int sockfd,
    const struct Player players[MAX_PLAYERS],
    const struct Bullet bullets[MAX_BULLETS],
    const struct Match *match,
    double now)
{
    uint16_t packet[STATE_PACKET_WORDS];

    for (int receiver = 0; receiver < MAX_PLAYERS; receiver++)
    {
        if (!players[receiver].active)
            continue;

        packet[0] = htons((uint16_t)receiver);
        packet[1] = htons((uint16_t)match->phase);
        packet[2] = htons(seconds_remaining(match, now));
        packet[3] = htons(match->winner_id >= 0
                              ? (uint16_t)match->winner_id
                              : UINT16_MAX);

        int offset = 4;

        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            const struct Player *player = &players[i];

            packet[offset + 0] = htons((uint16_t)player->active);
            packet[offset + 1] = htons(player->active ? player->x : 0);
            packet[offset + 2] = htons(player->active ? player->y : 0);
            packet[offset + 3] = htons((uint16_t)player->angle);
            packet[offset + 4] = htons(player->active ? player->color : 0);
            packet[offset + 5] = htons(player->active ? player->hp : 0);
            packet[offset + 6] = htons(player->active ? player->kills : 0);
            packet[offset + 7] = htons(player->active ? player->deaths : 0);
            packet[offset + 8] = htons(
                (uint16_t)(player->active && player->ready));
            packet[offset + 9] = htons(
                (uint16_t)(player->active &&
                           now - player->last_seen <= PLAYER_STALE_SECONDS));
            packet[offset + 10] = htons(player->active ? player->weapon : 0);
            offset += NET_PLAYER_WORDS;
        }

        for (int b = 0; b < MAX_BULLETS; b++)
        {
            packet[offset + 0] = htons((uint16_t)bullets[b].active);
            packet[offset + 1] = htons(
                bullets[b].active ? bullet_position(bullets[b].x) : 0);
            packet[offset + 2] = htons(
                bullets[b].active ? bullet_position(bullets[b].y) : 0);
            packet[offset + 3] = htons(bullets[b].active ? (uint16_t)bullets[b].weapon : 0);
            offset += NET_BULLET_WORDS;
        }

        sendto(sockfd, (const char *)packet, sizeof(packet), 0,
               (const struct sockaddr *)&players[receiver].addr,
               sizeof(players[receiver].addr));
    }
}

int main(int argc, char **argv)
{
    uint16_t server_port = PORT;

    if (argc == 3 && strcmp(argv[1], "--port") == 0)
    {
        char *end = NULL;
        errno = 0;
        long requested_port = strtol(argv[2], &end, 10);

        if (errno != 0 || end == argv[2] || *end != '\0' ||
            requested_port < 1 || requested_port > 65535)
        {
            fprintf(stderr, "Usage: %s [--port 1-65535]\n", argv[0]);
            return 2;
        }

        server_port = (uint16_t)requested_port;
    }
    else if (argc != 1)
    {
        fprintf(stderr, "Usage: %s [--port 1-65535]\n", argv[0]);
        return 2;
    }

    struct Player players[MAX_PLAYERS] = {0};
    struct Bullet bullets[MAX_BULLETS] = {0};
    struct Match match = {PHASE_LOBBY, -1, 0.0};

    srand((unsigned int)time(NULL));

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("[server] socket failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0)
    {
        perror("[server] setsockopt failed");
        close(sockfd);
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    if (bind(sockfd, (const struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("[server] bind failed");
        close(sockfd);
        return 1;
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("[server] non-blocking setup failed");
        close(sockfd);
        return 1;
    }

    printf("[server] lobby server listening on UDP port %u\n", (unsigned)server_port);

    double next_tick = monotonic_seconds() + TICK_SECONDS;

    for (;;)
    {
        double now = monotonic_seconds();
        double wait_seconds = next_tick - now;

        if (wait_seconds < 0.0)
            wait_seconds = 0.0;

        struct timeval timeout;
        timeout.tv_sec = (long)wait_seconds;
        timeout.tv_usec =
            (long)((wait_seconds - (double)timeout.tv_sec) * 1000000.0);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        int ready = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0 && errno != EINTR)
        {
            perror("[server] select failed");
            break;
        }

        if (ready > 0 && FD_ISSET(sockfd, &read_fds))
        {
            int packets_processed = 0;

            while (packets_processed < MAX_INPUT_PACKETS_PER_PASS)
            {
                uint16_t net_input[3];
                uint16_t ping_packet[2];
                uint8_t packet_buffer[sizeof(net_input) + 1];
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                ssize_t bytes = recvfrom(
                    sockfd, packet_buffer, sizeof(packet_buffer), 0,
                    (struct sockaddr *)&client_addr, &addr_len);

                if (bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    if (errno == EINTR)
                        continue;

                    perror("[server] recvfrom failed");
                    break;
                }

                packets_processed++;

                // A ping never joins a match or consumes a player slot.
                if (bytes == (ssize_t)sizeof(ping_packet))
                {
                    memcpy(ping_packet, packet_buffer, sizeof(ping_packet));
                    if (ntohs(ping_packet[0]) == NET_PING_MAGIC &&
                        ntohs(ping_packet[1]) == NET_PING_REQUEST)
                    {
                        uint16_t reply[2] = {htons(NET_PING_MAGIC), htons(NET_PING_RESPONSE)};
                        sendto(sockfd, (const char *)reply, sizeof(reply), 0,
                               (const struct sockaddr *)&client_addr, addr_len);
                    }
                }
                else if (bytes == (ssize_t)sizeof(net_input))
                {
                    memcpy(net_input, packet_buffer, sizeof(net_input));
                    handle_input(players, net_input, &client_addr,
                                 &match, monotonic_seconds());
                }
            }
        }

        now = monotonic_seconds();
        int ticks_run = 0;

        while (now >= next_tick)
        {
            simulate_tick(players, bullets, &match, next_tick);
            update_match_phase(players, bullets, &match, next_tick);

            next_tick += TICK_SECONDS;
            ticks_run++;
            now = monotonic_seconds();
        }

        if (ticks_run > 0)
            broadcast_state(sockfd, players, bullets, &match, now);
    }

    close(sockfd);
    return 0;
}