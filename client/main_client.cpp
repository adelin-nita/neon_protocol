#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "raylib.h"
#include "net_client.h"

#define MAP_SIZE 2000

enum GameState
{
    MENU,
    JOIN,
    SINGLEPLAYER,
    SESSION,
    SETTINGS,
    ABOUT
};

#define SOLO_LEVEL_COUNT 5
#define SOLO_WAVE_COUNT 3
#define SOLO_MAX_ENEMIES 12
#define SOLO_MAX_PROJECTILES 64
#define SOLO_MAX_PARTICLES 96
#define SOLO_MAX_SHOCKWAVES 12
#define MENU_MAX_SHOTS 16
#define MENU_MAX_SPARKS 36
#define HANGAR_MAX_BOLTS 48

enum SoloEnemyType
{
    SOLO_SCOUT,
    SOLO_HUNTER,
    SOLO_GUARDIAN
};

struct SoloEnemy
{
    bool active;
    SoloEnemyType type;
    Vector2 position;
    float angle;
    float hp;
    float maxHp;
    float fireCooldown;
    float orbitPhase;
    float hitFlash;
};

struct SoloProjectile
{
    bool active;
    bool hostile;
    Vector2 position;
    Vector2 velocity;
    float life;
    int damage;
    int weapon;
};

struct SoloParticle
{
    bool active;
    Vector2 position;
    Vector2 velocity;
    float life;
    float maxLife;
    float radius;
    Color color;
};

struct SoloShockwave
{
    bool active;
    Vector2 position;
    float age;
};

struct SoloCampaign
{
    Vector2 playerPosition;
    float playerAngle;
    float playerFireCooldown;
    float invulnerability;
    float damageFlash;
    float spawnCooldown;
    float intermission;
    float waveBannerTimer;
    float controlsHintTimer;
    float shakeTime;
    float shakeStrength;
    float elapsed;
    int level;
    int wave;
    int lives;
    int playerHp;
    int score;
    int highScore;
    int kills;
    int enemiesToSpawn;
    int enemiesSpawned;
    bool betweenWaves;
    bool paused;
    bool finished;
    bool campaignWon;
    SoloEnemy enemies[SOLO_MAX_ENEMIES];
    SoloProjectile projectiles[SOLO_MAX_PROJECTILES];
    SoloParticle particles[SOLO_MAX_PARTICLES];
    SoloShockwave shockwaves[SOLO_MAX_SHOCKWAVES];
};

struct OnlineJuice
{
    SoloParticle particles[SOLO_MAX_PARTICLES];
    SoloShockwave shockwaves[SOLO_MAX_SHOCKWAVES];
    float hitFlash[MAX_PLAYERS];
    Vector2 lastPosition[MAX_PLAYERS];
    uint16_t lastHp[MAX_PLAYERS];
    uint16_t lastDeaths[MAX_PLAYERS];
    bool observed[MAX_PLAYERS];
    float firePulseCooldown;
    float shakeTime;
    float shakeStrength;
};

enum ConnectionStatus
{
    CONNECTION_NONE,
    CONNECTION_CONNECTING,
    CONNECTION_CONNECTED,
    CONNECTION_RECONNECTING,
    CONNECTION_FAILED
};

static Color playerColors[NET_COLOR_COUNT] = {
    Color{78, 211, 255, 255},
    Color{255, 92, 185, 255},
    Color{167, 242, 101, 255},
    Color{255, 172, 76, 255},
    Color{181, 132, 255, 255}};

static const char *playerColorNames[NET_COLOR_COUNT] = {
    "CYAN", "MAGENTA", "LIME", "SOLAR", "VIOLET"};

struct WeaponPreset
{
    const char *name;
    const char *subtitle;
    Color color;
    float fireRate;
    float damage;
    float velocity;
    float shield;
};

static const WeaponPreset weaponPresets[NET_WEAPON_COUNT] = {
    {"TWIN LASER", "FAST / BALANCED", Color{92, 220, 255, 255}, 82.0f, 55.0f, 88.0f, 66.0f},
    {"PLASMA", "HEAVY / SLOW", Color{255, 174, 83, 255}, 38.0f, 100.0f, 56.0f, 82.0f},
    {"SPREAD", "WIDE / CLOSE RANGE", Color{255, 105, 194, 255}, 66.0f, 72.0f, 74.0f, 58.0f}};

static const char *weaponStatNames[4] = {
    "FIRE RATE", "DAMAGE", "VELOCITY", "SHIELD"};
static float displayedWeaponStats[4] = {82.0f, 55.0f, 88.0f, 66.0f};
static int selectedWeapon = 0;

struct MenuShot
{
    bool active;
    Vector2 position;
    Vector2 velocity;
    float life;
};

struct MenuSpark
{
    bool active;
    Vector2 position;
    Vector2 velocity;
    float life;
    Color color;
};

struct MenuEffects
{
    float shipAngle;
    float recoil;
    float fireCooldown;
    MenuShot shots[MENU_MAX_SHOTS];
    MenuSpark sparks[MENU_MAX_SPARKS];
};

struct HangarBolt
{
    bool active;
    Vector2 position;
    Vector2 velocity;
    float life;
    int weapon;
};

static MenuEffects menuEffects = {};
static HangarBolt hangarBolts[HANGAR_MAX_BOLTS] = {};
static float hangarFireTimer = 0.35f;
static float hangarDummyFlash = 0.0f;
static float hangarDummyImpact = 0.0f;
static OnlineJuice onlineJuice = {};

static Color GetPlayerColor(int id)
{
    if (id < 0)
        id = 0;

    return playerColors[id % NET_COLOR_COUNT];
}

static Rectangle GetColorSwatchRectangle(int index, float centerX, float top)
{
    return {
        centerX - 166.0f + index * 70.0f,
        top,
        52.0f,
        52.0f};
}
static Font uiFont = {};
static bool uiFontLoaded = false;

static void LoadUiFont()
{
#ifdef _WIN32
    const char *windowsDirectory = getenv("WINDIR");
    if (windowsDirectory == NULL || windowsDirectory[0] == '\0')
        windowsDirectory = "C:/Windows";

    const char *fontFiles[] = {
        "bahnschrift.ttf",
        "seguisb.ttf",
        "segoeui.ttf"};
    char fontPath[512];

    for (size_t i = 0; i < sizeof(fontFiles) / sizeof(fontFiles[0]); i++)
    {
        int pathLength = snprintf(
            fontPath,
            sizeof(fontPath),
            "%s/Fonts/%s",
            windowsDirectory,
            fontFiles[i]);

        if (pathLength < 0 ||
            (size_t)pathLength >= sizeof(fontPath) ||
            !FileExists(fontPath))
            continue;

        Font loadedFont = LoadFontEx(fontPath, 64, NULL, 0);
        if (loadedFont.texture.id == 0)
            continue;

        uiFont = loadedFont;
        uiFontLoaded = true;
        SetTextureFilter(uiFont.texture, TEXTURE_FILTER_BILINEAR);
        break;
    }
#endif
}

static void UnloadUiFont()
{
    if (uiFontLoaded)
    {
        UnloadFont(uiFont);
        uiFontLoaded = false;
    }
}

static void DrawUiText(
    const char *text,
    int posX,
    int posY,
    int fontSize,
    Color color)
{
    if (text == NULL)
        return;

    Font font = uiFontLoaded ? uiFont : GetFontDefault();
    DrawTextEx(
        font,
        text,
        {(float)posX, (float)posY},
        (float)fontSize,
        1.0f,
        color);
}

static int MeasureUiText(const char *text, int fontSize)
{
    if (text == NULL)
        return 0;

    Font font = uiFontLoaded ? uiFont : GetFontDefault();
    Vector2 size = MeasureTextEx(font, text, (float)fontSize, 1.0f);
    return (int)ceilf(size.x);
}

static void DrawUiTextSpaced(const char *text,
                             int posX,
                             int posY,
                             int fontSize,
                             float letterSpacing,
                             Color color)
{
    if (text == NULL)
        return;

    int cursorX = posX;
    for (const char *p = text; *p != '\0'; p++)
    {
        char character[2] = {*p, '\0'};
        DrawUiText(character, cursorX, posY, fontSize, color);
        cursorX += MeasureUiText(character, fontSize) + (int)letterSpacing;
    }
}
static float ClampValue(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}

static bool ParseServerAddress(
    const char *address,
    char *host,
    size_t hostCapacity,
    int *port)
{
    if (address == NULL || host == NULL || port == NULL ||
        hostCapacity == 0)
        return false;

    size_t addressLength = strlen(address);
    if (addressLength == 0 || addressLength >= 96)
        return false;

    const char *separator = strrchr(address, ':');
    size_t hostLength = separator
                            ? (size_t)(separator - address)
                            : addressLength;
    int parsedPort = 8080;

    if (separator != NULL)
    {
        if (separator == address || separator[1] == '\0' ||
            strchr(address, ':') != separator)
            return false;

        parsedPort = 0;
        int digits = 0;
        for (const char *p = separator + 1; *p != '\0'; p++)
        {
            if (*p < '0' || *p > '9' || digits >= 5)
                return false;

            parsedPort = parsedPort * 10 + (*p - '0');
            digits++;
        }

        if (digits == 0 || parsedPort < 1 || parsedPort > 65535)
            return false;
    }

    if (hostLength == 0 || hostLength >= hostCapacity)
        return false;

    for (size_t i = 0; i < hostLength; i++)
    {
        char c = address[i];
        bool allowed =
            (c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '.' || c == '-';

        if (!allowed)
            return false;
    }

    memcpy(host, address, hostLength);
    host[hostLength] = '\0';
    *port = parsedPort;
    return true;
}

static bool ConnectToServerAddress(const char *address)
{
    char host[96];
    int port = 8080;

    return ParseServerAddress(address, host, sizeof(host), &port) &&
           init_network(host, port);
}
static bool GetServerAddressPreferencePath(char *path, size_t capacity)
{
    if (path == NULL || capacity == 0)
        return false;

    const char *appData = getenv("APPDATA");
    if (appData != NULL && appData[0] != '\0')
    {
        char directory[512];
        int directoryLength = snprintf(
            directory,
            sizeof(directory),
            "%s/NeonProtocol",
            appData);

        if (directoryLength < 0 ||
            (size_t)directoryLength >= sizeof(directory))
            return false;

#ifdef _WIN32
        (void)_mkdir(directory);
#else
        (void)mkdir(directory, 0700);
#endif

        int pathLength = snprintf(
            path,
            capacity,
            "%s/server_address.txt",
            directory);
        return pathLength >= 0 && (size_t)pathLength < capacity;
    }

    const char *applicationDirectory = GetApplicationDirectory();
    if (applicationDirectory == NULL || applicationDirectory[0] == '\0')
        return false;

    size_t directoryLength = strlen(applicationDirectory);
    bool needsSeparator =
        directoryLength > 0 &&
        applicationDirectory[directoryLength - 1] != '/' &&
        applicationDirectory[directoryLength - 1] != '\\';

    int pathLength = snprintf(
        path,
        capacity,
        "%s%sserver_address.txt",
        applicationDirectory,
        needsSeparator ? "/" : "");
    return pathLength >= 0 && (size_t)pathLength < capacity;
}

static bool GetSoloHighScorePath(char *path, size_t capacity)
{
    if (path == NULL || capacity == 0 ||
        !GetServerAddressPreferencePath(path, capacity))
        return false;

    char *separator = strrchr(path, '/');
    char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    if (separator == NULL)
        return false;

    size_t directoryLength = (size_t)(separator - path + 1);
    const char *filename = "singleplayer_highscore.txt";
    size_t filenameLength = strlen(filename);
    if (directoryLength + filenameLength >= capacity)
        return false;

    memcpy(path + directoryLength, filename, filenameLength + 1);
    return true;
}

static int LoadSoloHighScore()
{
    char path[1024];
    if (!GetSoloHighScorePath(path, sizeof(path)))
        return 0;

    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;

    int score = 0;
    if (fscanf(file, "%d", &score) != 1 || score < 0)
        score = 0;
    fclose(file);
    return score;
}

static void SaveSoloHighScore(int score)
{
    char path[1024];
    if (score < 0 || !GetSoloHighScorePath(path, sizeof(path)))
        return;

    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return;

    fprintf(file, "%d\n", score);
    fclose(file);
}
static bool LoadSavedServerAddress(char *address, size_t capacity)
{
    if (address == NULL || capacity == 0)
        return false;

    char path[1024];
    if (!GetServerAddressPreferencePath(path, sizeof(path)))
        return false;

    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;

    char savedAddress[96] = {};
    bool readAddress = fgets(savedAddress, sizeof(savedAddress), file) != NULL;
    int nextCharacter = fgetc(file);
    bool tooLong =
        readAddress &&
        nextCharacter != EOF &&
        nextCharacter != '\r' &&
        nextCharacter != '\n';
    fclose(file);

    if (!readAddress || tooLong)
        return false;

    savedAddress[strcspn(savedAddress, "\r\n")] = '\0';

    char host[96];
    int port = 0;
    size_t addressLength = strlen(savedAddress);
    if (addressLength == 0 ||
        addressLength >= capacity ||
        !ParseServerAddress(savedAddress, host, sizeof(host), &port))
        return false;

    memcpy(address, savedAddress, addressLength + 1);
    return true;
}

static bool SaveServerAddress(const char *address)
{
    if (address == NULL || address[0] == '\0')
        return false;

    char path[1024];
    if (!GetServerAddressPreferencePath(path, sizeof(path)))
        return false;

    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return false;

    size_t addressLength = strlen(address);
    bool saved =
        fwrite(address, 1, addressLength, file) == addressLength &&
        fputc('\n', file) != EOF;

    if (fclose(file) != 0)
        saved = false;

    return saved;
}

static bool BeginServerConnection(
    const char *address,
    RemotePlayer *players,
    NetBullet *bullets,
    int *myId,
    uint16_t *myHp,
    uint16_t *matchPhase,
    uint16_t *secondsLeft,
    int *winnerId,
    bool *localReady,
    ConnectionStatus *connectionStatus,
    double *connectionStartedAt,
    double *lastServerPacketAt)
{
    if (!ConnectToServerAddress(address))
        return false;

    memset(players, 0, sizeof(RemotePlayer) * MAX_PLAYERS);
    memset(bullets, 0, sizeof(NetBullet) * MAX_BULLETS);

    *myId = -1;
    *myHp = 100;
    *matchPhase = NET_PHASE_LOBBY;
    *secondsLeft = 0;
    *winnerId = NET_NO_WINNER;
    *localReady = false;

    *connectionStatus = CONNECTION_CONNECTING;
    *connectionStartedAt = GetTime();
    *lastServerPacketAt = *connectionStartedAt;
    return true;
}
static void ClampCameraToArena(Camera2D *camera)
{
    float halfWidth = GetScreenWidth() / (2.0f * camera->zoom);
    float halfHeight = GetScreenHeight() / (2.0f * camera->zoom);

    if (halfWidth * 2.0f >= MAP_SIZE)
        camera->target.x = MAP_SIZE / 2.0f;
    else
        camera->target.x = ClampValue(
            camera->target.x,
            halfWidth,
            MAP_SIZE - halfWidth);

    if (halfHeight * 2.0f >= MAP_SIZE)
        camera->target.y = MAP_SIZE / 2.0f;
    else
        camera->target.y = ClampValue(
            camera->target.y,
            halfHeight,
            MAP_SIZE - halfHeight);
}

static void DrawButton(Rectangle rect,
                       const char *label,
                       Color baseColor,
                       Vector2 mousePoint,
                       bool backButton = false)
{
    bool hover = CheckCollisionPointRec(mousePoint, rect);
    bool pressed = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 6.0f);
    float hoverShift = hover ? 3.0f + pulse : 0.0f;
    Color outline = hover ? WHITE : Fade(baseColor, 0.82f);
    float cut = fminf(12.0f, rect.height * 0.28f);

    if (hover)
    {
        Rectangle glow = {rect.x - 5.0f, rect.y - 5.0f,
                          rect.width + 10.0f, rect.height + 10.0f};
        DrawRectangleRec(glow, Fade(baseColor, 0.025f + pulse * 0.04f));
    }

    Vector2 topLeft = {rect.x, rect.y};
    Vector2 topRight = {rect.x + rect.width, rect.y};
    Vector2 rightBottom = {rect.x + rect.width, rect.y + rect.height - cut};
    Vector2 bottomCut = {rect.x + rect.width - cut, rect.y + rect.height};
    Vector2 bottomLeft = {rect.x, rect.y + rect.height};
    float fillAlpha = pressed ? 0.24f : hover ? 0.17f : 0.055f;
    Color fill = Fade(baseColor, fillAlpha);

    DrawTriangle(topLeft, topRight, rightBottom, fill);
    DrawTriangle(topLeft, rightBottom, bottomCut, fill);
    DrawTriangle(topLeft, bottomCut, bottomLeft, fill);

    float lineWidth = pressed ? 3.0f : hover ? 2.4f : 1.4f;
    DrawLineEx(topLeft, topRight, lineWidth, outline);
    DrawLineEx(topRight, rightBottom, lineWidth, outline);
    DrawLineEx(rightBottom, bottomCut, lineWidth, outline);
    DrawLineEx(bottomCut, bottomLeft, lineWidth, outline);
    DrawLineEx(bottomLeft, topLeft, lineWidth, outline);

    DrawRectangle((int)rect.x, (int)rect.y + 5,
                  hover ? 6 : 4, (int)rect.height - 10, baseColor);

    int fontSize = rect.height <= 44.0f ? 18 : 21;
    int leftPadding = backButton ? 43 : 12;
    int rightPadding = backButton ? 12 : 42;
    float textSpace = rect.width - (float)(leftPadding + rightPadding);
    while (fontSize > 14 && MeasureUiText(label, fontSize) > (int)textSpace)
        fontSize--;

    int textWidth = MeasureUiText(label, fontSize);
    float textX = rect.x + leftPadding + (textSpace - textWidth) * 0.5f;
    DrawUiText(label, (int)textX,
               (int)(rect.y + rect.height * 0.5f - fontSize * 0.5f),
               fontSize, hover ? WHITE : baseColor);

    const char *arrow = backButton ? "<" : ">";
    float arrowX = backButton
                       ? rect.x + 13.0f - hoverShift
                       : rect.x + rect.width - 28.0f + hoverShift;
    DrawUiText(arrow, (int)arrowX,
               (int)(rect.y + rect.height * 0.5f - 10.0f),
               20, Fade(outline, hover ? 0.98f : 0.66f));
}

static Vector2 DirectionBetween(Vector2 from, Vector2 to)
{
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f)
        return {1.0f, 0.0f};
    return {dx / length, dy / length};
}

static void FireMenuShot(Vector2 ship, Vector2 cursor, Color color)
{
    Vector2 direction = DirectionBetween(ship, cursor);
    Vector2 muzzle = {ship.x + direction.x * 37.0f, ship.y + direction.y * 37.0f};
    for (int i = 0; i < MENU_MAX_SHOTS; i++)
    {
        if (menuEffects.shots[i].active)
            continue;
        menuEffects.shots[i] = {true, muzzle,
                                {direction.x * 620.0f, direction.y * 620.0f}, 1.5f};
        break;
    }
    for (int i = 0; i < MENU_MAX_SPARKS; i++)
    {
        if (menuEffects.sparks[i].active)
            continue;
        float angle = atan2f(direction.y, direction.x) +
                      (float)GetRandomValue(-100, 100) * 0.009f;
        float speed = (float)GetRandomValue(45, 180);
        menuEffects.sparks[i] = {true, muzzle,
                                 {cosf(angle) * speed, sinf(angle) * speed},
                                 0.18f + (float)GetRandomValue(0, 12) * 0.01f,
                                 i % 3 == 0 ? WHITE : color};
    }
    menuEffects.recoil = 8.0f;
    menuEffects.fireCooldown = 0.12f;
}

static void UpdateMenuEffects(float dt, Vector2 ship, Vector2 cursor)
{
    dt = ClampValue(dt, 0.0f, 0.05f);
    Vector2 direction = DirectionBetween(ship, cursor);
    float targetAngle = atan2f(direction.y, direction.x);
    float difference = atan2f(sinf(targetAngle - menuEffects.shipAngle),
                              cosf(targetAngle - menuEffects.shipAngle));
    menuEffects.shipAngle += difference * ClampValue(dt * 9.0f, 0.0f, 1.0f);
    menuEffects.recoil = fmaxf(0.0f, menuEffects.recoil - dt * 52.0f);
    menuEffects.fireCooldown = fmaxf(0.0f, menuEffects.fireCooldown - dt);

    for (int i = 0; i < MENU_MAX_SHOTS; i++)
    {
        MenuShot *shot = &menuEffects.shots[i];
        if (!shot->active)
            continue;
        shot->position.x += shot->velocity.x * dt;
        shot->position.y += shot->velocity.y * dt;
        shot->life -= dt;
        if (shot->life <= 0.0f || shot->position.x < 0.0f ||
            shot->position.y < 0.0f || shot->position.x > GetScreenWidth() ||
            shot->position.y > GetScreenHeight())
            shot->active = false;
    }
    for (int i = 0; i < MENU_MAX_SPARKS; i++)
    {
        MenuSpark *spark = &menuEffects.sparks[i];
        if (!spark->active)
            continue;
        spark->position.x += spark->velocity.x * dt;
        spark->position.y += spark->velocity.y * dt;
        spark->velocity.x *= 0.94f;
        spark->velocity.y *= 0.94f;
        spark->life -= dt;
        if (spark->life <= 0.0f)
            spark->active = false;
    }
}

static void DrawMenuEffects(void)
{
    for (int i = 0; i < MENU_MAX_SHOTS; i++)
    {
        const MenuShot &shot = menuEffects.shots[i];
        if (!shot.active)
            continue;
        Vector2 tail = {shot.position.x - shot.velocity.x * 0.035f,
                        shot.position.y - shot.velocity.y * 0.035f};
        DrawLineEx(tail, shot.position, 8.0f, Fade(Color{83, 220, 255, 255}, 0.22f));
        DrawLineEx(tail, shot.position, 3.0f, Color{133, 238, 255, 255});
        DrawCircleV(shot.position, 3.0f, WHITE);
    }
    for (int i = 0; i < MENU_MAX_SPARKS; i++)
    {
        const MenuSpark &spark = menuEffects.sparks[i];
        if (spark.active)
            DrawCircleV(spark.position, 1.6f, Fade(spark.color, ClampValue(spark.life * 4.0f, 0.0f, 1.0f)));
    }
}

static void AddHangarBolt(Vector2 position, Vector2 velocity, int weapon)
{
    for (int i = 0; i < HANGAR_MAX_BOLTS; i++)
    {
        if (!hangarBolts[i].active)
        {
            hangarBolts[i] = {true, position, velocity, 2.5f, weapon};
            return;
        }
    }
}

static void UpdateHangarPreview(float dt, Vector2 ship, Vector2 target)
{
    dt = ClampValue(dt, 0.0f, 0.05f);
    hangarFireTimer -= dt;
    hangarDummyFlash = fmaxf(0.0f, hangarDummyFlash - dt);
    hangarDummyImpact = fmaxf(0.0f, hangarDummyImpact - dt);
    if (hangarFireTimer <= 0.0f)
    {
        float startX = ship.x + 36.0f;
        float baseY = ship.y;
        if (selectedWeapon == 1)
            AddHangarBolt({startX, baseY}, {510.0f, 0.0f}, selectedWeapon);
        else if (selectedWeapon == 2)
        {
            AddHangarBolt({startX, baseY}, {670.0f, -95.0f}, selectedWeapon);
            AddHangarBolt({startX, baseY}, {710.0f, 0.0f}, selectedWeapon);
            AddHangarBolt({startX, baseY}, {670.0f, 95.0f}, selectedWeapon);
        }
        else
        {
            AddHangarBolt({startX, baseY - 8.0f}, {760.0f, 0.0f}, selectedWeapon);
            AddHangarBolt({startX, baseY + 8.0f}, {760.0f, 0.0f}, selectedWeapon);
        }
        hangarFireTimer = selectedWeapon == 1 ? 0.78f : selectedWeapon == 2 ? 0.58f : 0.34f;
    }

    for (int i = 0; i < HANGAR_MAX_BOLTS; i++)
    {
        HangarBolt *bolt = &hangarBolts[i];
        if (!bolt->active)
            continue;
        bolt->position.x += bolt->velocity.x * dt;
        bolt->position.y += bolt->velocity.y * dt;
        bolt->life -= dt;
        float dx = bolt->position.x - target.x;
        float dy = bolt->position.y - target.y;
        if (dx * dx + dy * dy < 32.0f * 32.0f)
        {
            bolt->active = false;
            hangarDummyFlash = 0.12f;
            hangarDummyImpact = 0.24f;
        }
        else if (bolt->life <= 0.0f)
            bolt->active = false;
    }
}

static void DrawDrone(Vector2 position, float angleDegrees, Color color, bool isLocal, float scale = 1.0f);

static void DrawHangarPreview(Rectangle panel, Vector2 ship, Vector2 target, int selectedColor)
{
    DrawRectangleRec(panel, Fade(Color{7, 12, 28, 255}, 0.94f));
    DrawRectangleLinesEx(panel, 1.2f, Fade(Color{111, 211, 247, 255}, 0.38f));
    DrawRectangle((int)panel.x, (int)panel.y, 3, (int)panel.height,
                  Fade(Color{111, 211, 247, 255}, 0.65f));
    DrawUiText("LIVE TEST RANGE", (int)panel.x + 18, (int)panel.y + 16, 14,
               Color{111, 211, 247, 255});
    DrawUiText("AUTO-FIRE PREVIEW", (int)(panel.x + panel.width - 160),
               (int)panel.y + 18, 11, Fade(WHITE, 0.54f));

    float gridTop = panel.y + 56.0f;
    float gridBottom = panel.y + panel.height - 50.0f;
    for (int i = 0; i < 6; i++)
    {
        float x = panel.x + 24.0f + (panel.width - 48.0f) * i / 5.0f;
        DrawLineEx({x, gridTop}, {x, gridBottom}, 1.0f, Fade(Color{100, 149, 190, 255}, 0.07f));
    }
    DrawLineEx({panel.x + 18.0f, ship.y}, {panel.x + panel.width - 18.0f, ship.y},
               1.0f, Fade(Color{100, 149, 190, 255}, 0.12f));

    for (int i = 0; i < HANGAR_MAX_BOLTS; i++)
    {
        const HangarBolt &bolt = hangarBolts[i];
        if (!bolt.active)
            continue;
        Color boltColor = weaponPresets[bolt.weapon].color;
        Vector2 tail = {bolt.position.x - bolt.velocity.x * 0.035f,
                        bolt.position.y - bolt.velocity.y * 0.035f};
        float thickness = bolt.weapon == 1 ? 8.0f : 3.0f;
        DrawLineEx(tail, bolt.position, thickness + 5.0f, Fade(boltColor, 0.2f));
        DrawLineEx(tail, bolt.position, thickness, boltColor);
        if (bolt.weapon == 1)
            DrawCircleV(bolt.position, 7.0f, Fade(WHITE, 0.86f));
    }

    DrawCircleV(target, 39.0f + hangarDummyImpact * 12.0f,
                Fade(Color{255, 113, 135, 255}, 0.08f + hangarDummyImpact * 0.2f));
    DrawCircleLines((int)target.x, (int)target.y, 33.0f, Fade(WHITE, 0.35f));
    DrawPoly(target, 6, 25.0f, 30.0f,
             hangarDummyFlash > 0.0f ? WHITE : Color{29, 31, 52, 255});
    DrawPolyLinesEx(target, 6, 25.0f, 30.0f, 3.0f,
                    hangarDummyFlash > 0.0f ? WHITE : Color{255, 116, 141, 255});
    DrawCircleV(target, 6.0f, Fade(Color{255, 116, 141, 255}, 0.8f));
    DrawDrone(ship, 0.0f, playerColors[selectedColor], true, 0.92f);
    DrawUiText("YOUR SHIP", (int)ship.x - 34, (int)ship.y + 44, 11, Fade(WHITE, 0.62f));
    DrawUiText("DUMMY TARGET", (int)target.x - 47, (int)target.y + 44, 11,
               Color{255, 139, 160, 255});
    DrawUiText(TextFormat("DPS PROFILE  /  %s", weaponPresets[selectedWeapon].name),
               (int)panel.x + 18, (int)(panel.y + panel.height - 28), 12,
               weaponPresets[selectedWeapon].color);
}
static void DrawBackdrop()
{
    int width = GetScreenWidth();
    int height = GetScreenHeight();

    DrawRectangleGradientV(
        0,
        0,
        width,
        height,
        Color{8, 12, 26, 255},
        Color{7, 8, 19, 255});

    float time = (float)GetTime();
    float drift = sinf(time * 0.12f) * 18.0f;
    float largeRadius = (float)fmaxf(width, height) * 0.62f;

    DrawCircleGradient(
        (int)(width * 0.77f + drift),
        (int)(height * 0.43f),
        largeRadius,
        Fade(Color{0, 92, 138, 255}, 0.13f),
        Fade(Color{7, 8, 19, 255}, 0.0f));

    DrawCircleGradient(
        (int)(width * 0.12f),
        (int)(height * 0.94f),
        (float)height * 0.48f,
        Fade(Color{86, 36, 124, 255}, 0.055f),
        Fade(Color{7, 8, 19, 255}, 0.0f));

    Vector2 mouse = GetMousePosition();
    float mouseParallaxX = ((mouse.x / (float)width) - 0.5f) * 18.0f;
    float mouseParallaxY = ((mouse.y / (float)height) - 0.5f) * 12.0f;
    const float layerDepth[3] = {0.22f, 0.52f, 1.0f};
    const float layerSpeed[3] = {2.0f, 4.0f, 7.0f};

    for (int layer = 0; layer < 3; layer++)
    {
        for (int i = 0; i < 18; i++)
        {
            unsigned int seed = (unsigned int)(i + layer * 37);
            unsigned int hash = seed * 2654435761u + 2246822519u;
            float baseX = (float)(hash % (unsigned int)width);
            float baseY = (float)((hash >> 8) % (unsigned int)height);
            float x = fmodf(baseX + mouseParallaxX * layerDepth[layer] + (float)width, (float)width);
            float y = fmodf(baseY + time * layerSpeed[layer] + mouseParallaxY * layerDepth[layer] + (float)height,
                            (float)height);
            float pulse = 0.5f + 0.5f * sinf(time * (0.45f + layer * 0.12f) + (float)(i + layer * 11));
            Color star = (i % 7 == 0) ? Color{187, 151, 255, 255} : Color{111, 211, 247, 255};
            float radius = layer == 2 && i % 9 == 0 ? 1.8f : (layer == 0 ? 0.8f : 1.1f);
            float alpha = 0.055f + pulse * (layer == 2 ? 0.13f : 0.075f);
            DrawCircleV({x, y}, radius, Fade(star, alpha));
        }
    }
}
static void DrawArena(int sector = 0)
{
    const Color sectorColors[SOLO_LEVEL_COUNT] = {
        Color{76, 194, 255, 255},
        Color{107, 224, 190, 255},
        Color{183, 148, 255, 255},
        Color{255, 161, 112, 255},
        Color{255, 112, 146, 255}};
    Color accent = sector > 0
                       ? sectorColors[(sector - 1) % SOLO_LEVEL_COUNT]
                       : Color{76, 194, 255, 255};

    DrawRectangle(0, 0, MAP_SIZE, MAP_SIZE, Color{7, 11, 23, 255});
    DrawRectangleGradientV(0, 0, MAP_SIZE, MAP_SIZE, Color{10, 17, 32, 255}, Color{7, 10, 21, 255});
    DrawCircleGradient(MAP_SIZE / 2, MAP_SIZE / 2, 940.0f, Fade(accent, sector > 0 ? 0.07f : 0.045f), Fade(Color{7, 11, 23, 255}, 0.0f));
    DrawCircleGradient(520, 1450, 520.0f, Fade(Color{143, 86, 189, 255}, 0.025f), Fade(Color{7, 11, 23, 255}, 0.0f));
    DrawCircleGradient(1540, 540, 420.0f, Fade(accent, 0.035f), Fade(Color{7, 11, 23, 255}, 0.0f));
    DrawCircleLines(MAP_SIZE / 2, MAP_SIZE / 2, 360.0f, Fade(accent, 0.07f));
    DrawCircleLines(MAP_SIZE / 2, MAP_SIZE / 2, 760.0f, Fade(Color{183, 148, 255, 255}, 0.04f));

    float time = (float)GetTime();
    for (int i = 0; i < 120; i++)
    {
        unsigned int hash = (unsigned int)i * 374761393u + 668265263u;
        hash = (hash ^ (hash >> 13)) * 1274122517u;
        float x = (float)(hash % MAP_SIZE);
        hash = (hash ^ (hash >> 16)) * 2246822519u;
        float baseY = (float)(hash % MAP_SIZE);
        float y = fmodf(baseY + time * (5.0f + (float)(i % 7) * 1.6f), (float)MAP_SIZE);
        float twinkle = 0.5f + 0.5f * sinf(time * (0.8f + i % 4) + i);
        float radius = (i % 13 == 0) ? 2.0f : 1.0f;
        Color starColor = (i % 9 == 0) ? Color{183, 148, 255, 255} : accent;
        DrawCircleV({x, y}, radius, Fade(starColor, (i % 13 == 0 ? 0.13f : 0.07f) + twinkle * 0.09f));
    }

    DrawRectangleLinesEx({2, 2, (float)MAP_SIZE - 4, (float)MAP_SIZE - 4}, 2, Fade(accent, 0.55f));
    DrawRectangleLinesEx({18, 18, (float)MAP_SIZE - 36, (float)MAP_SIZE - 36}, 1, Fade(Color{183, 148, 255, 255}, 0.28f));

    const int inset = 30;
    const int length = 64;
    Color corner = Fade(accent, 0.78f);
    DrawLineEx({(float)inset, (float)inset}, {(float)(inset + length), (float)inset}, 3, corner);
    DrawLineEx({(float)inset, (float)inset}, {(float)inset, (float)(inset + length)}, 3, corner);
    DrawLineEx({(float)(MAP_SIZE-inset), (float)inset}, {(float)(MAP_SIZE-inset-length), (float)inset}, 3, corner);
    DrawLineEx({(float)(MAP_SIZE-inset), (float)inset}, {(float)(MAP_SIZE-inset), (float)(inset+length)}, 3, corner);
    DrawLineEx({(float)inset, (float)(MAP_SIZE-inset)}, {(float)(inset+length), (float)(MAP_SIZE-inset)}, 3, corner);
    DrawLineEx({(float)inset, (float)(MAP_SIZE-inset)}, {(float)inset, (float)(MAP_SIZE-inset-length)}, 3, corner);
    DrawLineEx({(float)(MAP_SIZE-inset), (float)(MAP_SIZE-inset)}, {(float)(MAP_SIZE-inset-length), (float)(MAP_SIZE-inset)}, 3, corner);
    DrawLineEx({(float)(MAP_SIZE-inset), (float)(MAP_SIZE-inset)}, {(float)(MAP_SIZE-inset), (float)(MAP_SIZE-inset-length)}, 3, corner);
}
static void DrawDrone(Vector2 position,
                      float angleDegrees,
                      Color color,
                      bool isLocal,
                      float scale)
{
    const float pi = 3.14159265f;
    float angle = angleDegrees * (pi / 180.0f);

    Vector2 direction = {cosf(angle), sinf(angle)};
    Vector2 side = {-direction.y, direction.x};
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 10.0f);

    Vector2 engineCenter = {
        position.x - direction.x * 18.0f * scale,
        position.y - direction.y * 18.0f * scale};

    DrawCircleV(position, (isLocal ? 36.0f : 32.0f) * scale,
                Fade(color, isLocal ? 0.12f : 0.075f));

    for (int s = -1; s <= 1; s += 2)
    {
        Vector2 engine = {
            engineCenter.x + side.x * 9.0f * scale * s,
            engineCenter.y + side.y * 9.0f * scale * s};
        Vector2 flameEnd = {
            engine.x - direction.x * (13.0f + pulse * 8.0f) * scale,
            engine.y - direction.y * (13.0f + pulse * 8.0f) * scale};

        DrawLineEx(engine, flameEnd, 5.0f * scale,
                   Fade(color, 0.65f + pulse * 0.25f));
        DrawCircleV(engine, 4.0f * scale, Fade(WHITE, 0.75f));
    }

    Vector2 barrelStart = {
        position.x + direction.x * 8.0f * scale,
        position.y + direction.y * 8.0f * scale};
    Vector2 barrelEnd = {
        position.x + direction.x * 39.0f * scale,
        position.y + direction.y * 39.0f * scale};

    DrawLineEx(barrelStart, barrelEnd, 9.0f * scale, Color{22, 30, 48, 255});
    DrawLineEx(barrelStart, barrelEnd, 4.0f * scale, Fade(color, 0.9f));
    DrawPoly(position, 6, 23.0f * scale, angleDegrees + 30.0f,
             Color{17, 28, 48, 255});
    DrawPolyLinesEx(position, 6, 23.0f * scale, angleDegrees + 30.0f,
                    3.0f * scale, color);

    Vector2 nose = {
        position.x + direction.x * 23.0f * scale,
        position.y + direction.y * 23.0f * scale};
    Vector2 noseLeft = {
        position.x + direction.x * 7.0f * scale + side.x * 9.0f * scale,
        position.y + direction.y * 7.0f * scale + side.y * 9.0f * scale};
    Vector2 noseRight = {
        position.x + direction.x * 7.0f * scale - side.x * 9.0f * scale,
        position.y + direction.y * 7.0f * scale - side.y * 9.0f * scale};
    DrawTriangle(nose, noseLeft, noseRight, Fade(color, 0.95f));

    for (int s = -1; s <= 1; s += 2)
    {
        Vector2 pod = {
            position.x + side.x * 14.0f * scale * s,
            position.y + side.y * 14.0f * scale * s};
        DrawCircleV(pod, 4.5f * scale, Color{8, 14, 26, 255});
        DrawCircleLines((int)pod.x, (int)pod.y, (int)(4.0f * scale), color);
    }

    DrawCircleV(position, 7.0f * scale, Fade(color, 0.4f));
    DrawCircleV(position, 3.0f * scale, WHITE);
}

static void DrawHealthBar(float x,
                          float y,
                          float width,
                          uint16_t hp,
                          Color color)
{
    float health = ClampValue((float)hp / 100.0f, 0.0f, 1.0f);

    DrawRectangle(
        (int)x, (int)y, (int)width, 7, Fade(BLACK, 0.7f));

    DrawRectangle(
        (int)x, (int)y, (int)(width * health), 7, color);

    DrawRectangleLinesEx(
        {x, y, width, 7},
        1,
        Fade(WHITE, 0.4f));
}

static void DrawCrosshair(Vector2 position)
{
    Color color = Color{255, 80, 120, 230};

    DrawCircleLines((int)position.x, (int)position.y, 10, color);

    DrawLineEx(
        {position.x - 17, position.y},
        {position.x - 5, position.y},
        2,
        color);

    DrawLineEx(
        {position.x + 5, position.y},
        {position.x + 17, position.y},
        2,
        color);

    DrawLineEx(
        {position.x, position.y - 17},
        {position.x, position.y - 5},
        2,
        color);

    DrawLineEx(
        {position.x, position.y + 5},
        {position.x, position.y + 17},
        2,
        color);

    DrawCircleV(position, 2, WHITE);
}

static void DrawMinimap(const RemotePlayer players[MAX_PLAYERS],
                        int myId,
                        int screenWidth,
                        int screenHeight)
{
    const float width = 166.0f;
    const float height = 166.0f;

    float x = (float)screenWidth - width - 20.0f;
    float y = (float)screenHeight - height - 20.0f;

    DrawRectangle(
        (int)x, (int)y, (int)width, (int)height,
        Fade(Color{5, 10, 22, 255}, 0.9f));

    DrawRectangleLinesEx(
        {x, y, width, height},
        2,
        Fade(SKYBLUE, 0.8f));

    DrawUiText("ARENA MAP", (int)x + 10, (int)y + 8, 14, SKYBLUE);

    float innerX = x + 10.0f;
    float innerY = y + 30.0f;
    float innerWidth = width - 20.0f;
    float innerHeight = height - 40.0f;

    DrawRectangleLinesEx(
        {innerX, innerY, innerWidth, innerHeight},
        1,
        Fade(WHITE, 0.25f));

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        float px = innerX + ((float)players[i].x / MAP_SIZE) * innerWidth;
        float py = innerY + ((float)players[i].y / MAP_SIZE) * innerHeight;
        Color color = GetPlayerColor(players[i].color);

        if (i == myId)
        {
            DrawCircleV({px, py}, 6, WHITE);
            DrawCircleLines((int)px, (int)py, 8, color);
        }
        else
        {
            DrawCircleV({px, py}, 4, color);
        }
    }
}

static void DrawReactiveOnlineGrid(const OnlineJuice &juice);
static void DrawOnlineParticles(const OnlineJuice &juice);

static void DrawWorld(const Camera2D &camera,
                      const RemotePlayer players[MAX_PLAYERS],
                      const NetBullet bullets[MAX_BULLETS],
                      int myId,
                      uint16_t targetX,
                      uint16_t targetY,
                      uint16_t myHp,
                      float localAngle,
                      int selectedColor,
                      Vector2 worldMouse,
                      bool showCrosshair,
                      const OnlineJuice &juice)
{
    BeginMode2D(camera);

    DrawArena();
    DrawReactiveOnlineGrid(juice);
    DrawOnlineParticles(juice);

    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 12.0f);

    for (int b = 0; b < MAX_BULLETS; b++)
    {
        if (!bullets[b].active)
            continue;

        Vector2 position = {
            (float)bullets[b].x,
            (float)bullets[b].y};

        Color bulletColor = bullets[b].weapon == 1 ? Color{255, 174, 83, 255}
                           : bullets[b].weapon == 2 ? Color{255, 105, 194, 255}
                                                    : Color{92, 220, 255, 255};
        DrawCircleV(position, 13.0f + pulse * 3.0f, Fade(bulletColor, 0.2f));
        DrawCircleV(position, bullets[b].weapon == 1 ? 8.0f : 5.5f, bulletColor);
        DrawCircleV(position, 2.5f, WHITE);
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active || i == myId)
            continue;

        Vector2 position = {
            (float)players[i].x,
            (float)players[i].y};

        Color color = GetPlayerColor(players[i].color);

        DrawDrone(position, (float)players[i].angle,
                  juice.hitFlash[i] > 0.0f ? WHITE : color, false);
        DrawHealthBar(
            position.x - 24,
            position.y - 42,
            48,
            players[i].hp,
            RED);

        DrawUiText(
            TextFormat("PILOT %02d", i),
            (int)position.x - 32,
            (int)position.y - 60,
            10,
            Fade(WHITE, 0.8f));

        if (!players[i].connected)
        {
            DrawUiText(
                "LINK LOST",
                (int)position.x - 32,
                (int)position.y - 75,
                10,
                ORANGE);
        }
    }

    Vector2 localPosition = {
        (float)targetX,
        (float)targetY};

    DrawDrone(
        localPosition,
        localAngle,
        myId >= 0 && myId < MAX_PLAYERS && juice.hitFlash[myId] > 0.0f
            ? WHITE
            : playerColors[selectedColor],
        true);

    DrawHealthBar(
        localPosition.x - 28,
        localPosition.y - 47,
        56,
        myHp,
        LIME);

    if (showCrosshair)
        DrawCrosshair(worldMouse);

    EndMode2D();
}

static float SoloDistance(Vector2 a, Vector2 b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return sqrtf(dx * dx + dy * dy);
}

static Vector2 SoloDirection(Vector2 from, Vector2 to)
{
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f)
        return {1.0f, 0.0f};
    return {dx / length, dy / length};
}

static void AddOnlineShockwave(OnlineJuice *juice, Vector2 position)
{
    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        if (juice->shockwaves[i].active)
            continue;

        juice->shockwaves[i].active = true;
        juice->shockwaves[i].position = position;
        juice->shockwaves[i].age = 0.0f;
        return;
    }

    int oldest = 0;
    for (int i = 1; i < SOLO_MAX_SHOCKWAVES; i++)
        if (juice->shockwaves[i].age > juice->shockwaves[oldest].age)
            oldest = i;
    juice->shockwaves[oldest] = {true, position, 0.0f};
}

static void AddOnlineBurst(OnlineJuice *juice, Vector2 position, Color color,
                           int count, Vector2 bias)
{
    float baseAngle = (fabsf(bias.x) + fabsf(bias.y) > 0.001f)
                          ? atan2f(bias.y, bias.x)
                          : 0.0f;
    for (int i = 0; i < count; i++)
    {
        int slot = -1;
        for (int p = 0; p < SOLO_MAX_PARTICLES; p++)
            if (!juice->particles[p].active)
            {
                slot = p;
                break;
            }
        if (slot < 0)
            return;

        float angle = baseAngle + (6.2831853f * (float)i / (float)count) +
                      (float)GetRandomValue(-25, 25) * 0.01f;
        float speed = (float)GetRandomValue(85, 250);
        SoloParticle particle{};
        particle.active = true;
        particle.position = position;
        particle.velocity = {cosf(angle) * speed, sinf(angle) * speed};
        particle.life = 0.24f + (float)GetRandomValue(0, 26) * 0.01f;
        particle.maxLife = particle.life;
        particle.radius = 1.5f + (float)GetRandomValue(0, 25) * 0.1f;
        particle.color = color;
        juice->particles[slot] = particle;
    }
}

static void AddOnlineShake(OnlineJuice *juice, float strength, float duration)
{
    if (juice->shakeTime <= 0.0f)
        juice->shakeStrength = strength;
    else
        juice->shakeStrength = fmaxf(juice->shakeStrength, strength);
    juice->shakeTime = fmaxf(juice->shakeTime, duration);
}

static void UpdateOnlineJuice(OnlineJuice *juice,
                              const RemotePlayer players[MAX_PLAYERS],
                              int myId, float dt, bool firing, int weapon)
{
    dt = ClampValue(dt, 0.0f, 0.05f);
    juice->shakeTime = fmaxf(0.0f, juice->shakeTime - dt);
    juice->firePulseCooldown = fmaxf(0.0f, juice->firePulseCooldown - dt);

    for (int i = 0; i < MAX_PLAYERS; i++)
        juice->hitFlash[i] = fmaxf(0.0f, juice->hitFlash[i] - dt);

    for (int i = 0; i < SOLO_MAX_PARTICLES; i++)
    {
        SoloParticle *particle = &juice->particles[i];
        if (!particle->active)
            continue;
        particle->life -= dt;
        if (particle->life <= 0.0f)
        {
            particle->active = false;
            continue;
        }
        particle->position.x += particle->velocity.x * dt;
        particle->position.y += particle->velocity.y * dt;
        float drag = fmaxf(0.0f, 1.0f - dt * 2.8f);
        particle->velocity.x *= drag;
        particle->velocity.y *= drag;
    }

    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        SoloShockwave *wave = &juice->shockwaves[i];
        if (!wave->active)
            continue;
        wave->age += dt;
        if (wave->age >= 0.48f)
            wave->active = false;
    }

    if (firing && juice->firePulseCooldown <= 0.0f)
    {
        AddOnlineShake(juice, 0.65f, 0.035f);
        juice->firePulseCooldown = weapon == 1 ? 0.46f : weapon == 2 ? 0.29f : 0.14f;
    }

    Vector2 localPosition = {0.0f, 0.0f};
    bool hasLocalPlayer = myId >= 0 && myId < MAX_PLAYERS &&
                          players[myId].active;
    if (hasLocalPlayer)
        localPosition = {(float)players[myId].x, (float)players[myId].y};

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
        {
            juice->observed[i] = false;
            continue;
        }

        Vector2 position = {(float)players[i].x, (float)players[i].y};
        if (!juice->observed[i])
        {
            juice->observed[i] = true;
            juice->lastPosition[i] = position;
            juice->lastHp[i] = players[i].hp;
            juice->lastDeaths[i] = players[i].deaths;
            continue;
        }

        if (players[i].hp < juice->lastHp[i])
        {
            Vector2 burstDirection = hasLocalPlayer && i != myId
                                         ? SoloDirection(localPosition, position)
                                         : Vector2{0.0f, 0.0f};
            Color impactColor = i == myId
                                    ? Color{255, 105, 126, 255}
                                    : GetPlayerColor(players[i].color);
            juice->hitFlash[i] = 0.055f;
            AddOnlineBurst(juice, position, impactColor, 8, burstDirection);

            float distance = hasLocalPlayer
                                 ? SoloDistance(localPosition, position)
                                 : 99999.0f;
            if (i == myId)
                AddOnlineShake(juice, 3.8f, 0.13f);
            else if (distance < 560.0f)
                AddOnlineShake(juice, 1.35f, 0.075f);
        }

        if (players[i].deaths > juice->lastDeaths[i])
        {
            Vector2 deathPosition = juice->lastPosition[i];
            AddOnlineShockwave(juice, deathPosition);
            AddOnlineBurst(juice, deathPosition, Color{170, 220, 255, 255},
                           18, {0.0f, 0.0f});
            float distance = hasLocalPlayer
                                 ? SoloDistance(localPosition, deathPosition)
                                 : 99999.0f;
            if (i == myId || distance < 900.0f)
                AddOnlineShake(juice, 7.0f, 0.19f);
        }

        juice->lastPosition[i] = position;
        juice->lastHp[i] = players[i].hp;
        juice->lastDeaths[i] = players[i].deaths;
    }
}

static Vector2 DisplaceOnlineGridPoint(Vector2 point, const OnlineJuice &juice)
{
    Vector2 displaced = point;
    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        const SoloShockwave &wave = juice.shockwaves[i];
        if (!wave.active)
            continue;
        float dx = point.x - wave.position.x;
        float dy = point.y - wave.position.y;
        float distance = sqrtf(dx * dx + dy * dy);
        if (distance < 1.0f)
            continue;

        float envelope = expf(-distance / 250.0f) *
                         ClampValue(1.0f - wave.age / 0.48f, 0.0f, 1.0f);
        float ripple = sinf((distance - wave.age * 660.0f) * 0.055f) *
                       envelope * 18.0f;
        displaced.x += dx / distance * ripple;
        displaced.y += dy / distance * ripple;
    }
    return displaced;
}

static void DrawReactiveOnlineGrid(const OnlineJuice &juice)
{
    const int pointsPerSide = 15;
    Vector2 points[pointsPerSide][pointsPerSide];
    for (int y = 0; y < pointsPerSide; y++)
        for (int x = 0; x < pointsPerSide; x++)
            points[y][x] = DisplaceOnlineGridPoint(
                {(float)x * MAP_SIZE / (pointsPerSide - 1),
                 (float)y * MAP_SIZE / (pointsPerSide - 1)}, juice);

    Color grid = Fade(Color{102, 153, 194, 255}, 0.075f);
    for (int y = 0; y < pointsPerSide; y++)
        for (int x = 0; x < pointsPerSide - 1; x++)
            DrawLineV(points[y][x], points[y][x + 1], grid);
    for (int x = 0; x < pointsPerSide; x++)
        for (int y = 0; y < pointsPerSide - 1; y++)
            DrawLineV(points[y][x], points[y + 1][x], grid);

    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        const SoloShockwave &wave = juice.shockwaves[i];
        if (wave.active)
            DrawCircleLines((int)wave.position.x, (int)wave.position.y,
                            wave.age * 660.0f,
                            Fade(Color{135, 226, 255, 255},
                                 1.0f - wave.age / 0.48f));
    }
}

static void DrawOnlineParticles(const OnlineJuice &juice)
{
    for (int i = 0; i < SOLO_MAX_PARTICLES; i++)
    {
        const SoloParticle &particle = juice.particles[i];
        if (!particle.active)
            continue;
        float alpha = ClampValue(particle.life / particle.maxLife, 0.0f, 1.0f);
        DrawCircleV(particle.position,
                    particle.radius * (0.72f + (1.0f - alpha) * 0.5f),
                    Fade(particle.color, alpha));
    }
}

static void BeginSoloWave(SoloCampaign *game)
{
    game->enemiesToSpawn = 2 + game->level + game->wave;
    game->enemiesSpawned = 0;
    game->spawnCooldown = 0.7f;
    game->betweenWaves = false;
    game->intermission = 0.0f;
    game->waveBannerTimer = 2.4f;
    game->controlsHintTimer = 5.0f;
    memset(game->enemies, 0, sizeof(game->enemies));
    memset(game->projectiles, 0, sizeof(game->projectiles));
}

static void StartSoloCampaign(SoloCampaign *game)
{
    int highScore = game->highScore;
    *game = SoloCampaign{};
    game->highScore = highScore;
    game->playerPosition = {(float)MAP_SIZE / 2.0f, (float)MAP_SIZE / 2.0f};
    game->playerAngle = -90.0f;
    game->playerHp = 100;
    game->lives = 3;
    game->level = 1;
    game->wave = 1;
    BeginSoloWave(game);
}

static void SpawnSoloEnemy(SoloCampaign *game)
{
    int slot = -1;
    for (int i = 0; i < SOLO_MAX_ENEMIES; i++)
    {
        if (!game->enemies[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    bool guardian = game->wave == SOLO_WAVE_COUNT && game->enemiesSpawned == 0;
    SoloEnemy enemy{};
    enemy.active = true;
    enemy.type = guardian
                     ? SOLO_GUARDIAN
                     : (game->level >= 2 && GetRandomValue(0, 100) < 38)
                           ? SOLO_HUNTER
                           : SOLO_SCOUT;

    int edge = GetRandomValue(0, 3);
    float along = (float)GetRandomValue(100, MAP_SIZE - 100);
    if (edge == 0)
        enemy.position = {along, 70.0f};
    else if (edge == 1)
        enemy.position = {(float)MAP_SIZE - 70.0f, along};
    else if (edge == 2)
        enemy.position = {along, (float)MAP_SIZE - 70.0f};
    else
        enemy.position = {70.0f, along};

    float scale = 1.0f + (float)(game->level - 1) * 0.16f;
    if (enemy.type == SOLO_SCOUT)
        enemy.maxHp = 55.0f * scale;
    else if (enemy.type == SOLO_HUNTER)
        enemy.maxHp = 105.0f * scale;
    else
        enemy.maxHp = 390.0f * scale;

    enemy.hp = enemy.maxHp;
    enemy.angle = (float)GetRandomValue(-180, 180);
    enemy.fireCooldown = guardian ? 1.1f : 0.55f + (float)GetRandomValue(0, 100) / 100.0f;
    enemy.orbitPhase = (float)GetRandomValue(0, 628) / 100.0f;
    game->enemies[slot] = enemy;
    game->enemiesToSpawn--;
    game->enemiesSpawned++;
}

static void AddSoloProjectile(SoloCampaign *game,
                              bool hostile,
                              Vector2 position,
                              Vector2 direction,
                              float speed,
                              int damage,
                              int weapon = 0)
{
    for (int i = 0; i < SOLO_MAX_PROJECTILES; i++)
    {
        if (game->projectiles[i].active)
            continue;

        game->projectiles[i].active = true;
        game->projectiles[i].hostile = hostile;
        game->projectiles[i].position = position;
        game->projectiles[i].velocity = {direction.x * speed, direction.y * speed};
        game->projectiles[i].life = 2.2f;
        game->projectiles[i].damage = damage;
        game->projectiles[i].weapon = weapon;
        return;
    }
}

static void SpawnSoloBurst(SoloCampaign *game, Vector2 position,
                           Color color, int count, Vector2 bias)
{
    float biasAngle = atan2f(bias.y, bias.x);
    for (int i = 0; i < SOLO_MAX_PARTICLES && count > 0; i++)
    {
        if (game->particles[i].active)
            continue;
        float angle = biasAngle + (float)GetRandomValue(-314, 314) * 0.01f;
        float speed = (float)GetRandomValue(55, 260);
        float life = 0.16f + (float)GetRandomValue(0, 28) * 0.01f;
        game->particles[i] = {true, position,
                              {cosf(angle) * speed, sinf(angle) * speed},
                              life, life, (float)GetRandomValue(1, 3), color};
        count--;
    }
}

static void AddSoloShockwave(SoloCampaign *game, Vector2 position)
{
    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        if (!game->shockwaves[i].active)
        {
            game->shockwaves[i] = {true, position, 0.0f};
            return;
        }
    }
    game->shockwaves[0] = {true, position, 0.0f};
}

static Vector2 DisplaceGridPoint(Vector2 point, const SoloCampaign &game)
{
    Vector2 displaced = point;
    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        const SoloShockwave &wave = game.shockwaves[i];
        if (!wave.active)
            continue;
        float dx = point.x - wave.position.x;
        float dy = point.y - wave.position.y;
        float distance = sqrtf(dx * dx + dy * dy);
        float envelope = expf(-distance / 230.0f) * ClampValue(1.0f - wave.age / 0.46f, 0.0f, 1.0f);
        float ripple = sinf((distance - wave.age * 640.0f) * 0.055f) * envelope * 20.0f;
        if (distance > 1.0f)
        {
            displaced.x += dx / distance * ripple;
            displaced.y += dy / distance * ripple;
        }
    }
    return displaced;
}


static void DrawReactiveGrid(const SoloCampaign &game)
{
    const int pointsPerSide = 15;
    Vector2 points[pointsPerSide][pointsPerSide];
    for (int y = 0; y < pointsPerSide; y++)
    {
        for (int x = 0; x < pointsPerSide; x++)
        {
            Vector2 point = {(float)x * MAP_SIZE / (pointsPerSide - 1),
                             (float)y * MAP_SIZE / (pointsPerSide - 1)};
            points[y][x] = DisplaceGridPoint(point, game);
        }
    }
    Color grid = Fade(Color{102, 153, 194, 255}, 0.075f);
    for (int y = 0; y < pointsPerSide; y++)
        for (int x = 0; x < pointsPerSide - 1; x++)
            DrawLineV(points[y][x], points[y][x + 1], grid);
    for (int x = 0; x < pointsPerSide; x++)
        for (int y = 0; y < pointsPerSide - 1; y++)
            DrawLineV(points[y][x], points[y + 1][x], grid);

    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        const SoloShockwave &wave = game.shockwaves[i];
        if (!wave.active)
            continue;
        float radius = wave.age * 640.0f;
        DrawCircleLines((int)wave.position.x, (int)wave.position.y, radius,
                        Fade(Color{135, 226, 255, 255}, 1.0f - wave.age / 0.46f));
    }
}
static void DamageSoloPlayer(SoloCampaign *game, int damage)
{
    if (game->invulnerability > 0.0f || game->finished)
        return;

    game->playerHp -= damage;
    game->damageFlash = 0.32f;
    game->shakeTime = 0.17f;
    game->shakeStrength = 6.0f;
    game->invulnerability = 0.8f;
    if (game->playerHp > 0)
        return;

    game->lives--;
    if (game->lives <= 0)
    {
        game->lives = 0;
        game->playerHp = 0;
        game->finished = true;
        game->campaignWon = false;
        SaveSoloHighScore(game->highScore);
        return;
    }

    game->playerHp = 100;
    game->playerPosition = {(float)MAP_SIZE / 2.0f, (float)MAP_SIZE / 2.0f};
    game->invulnerability = 1.8f;
    for (int i = 0; i < SOLO_MAX_PROJECTILES; i++)
        if (game->projectiles[i].hostile)
            game->projectiles[i].active = false;
}

static void AdvanceSoloWave(SoloCampaign *game)
{
    if (game->wave < SOLO_WAVE_COUNT)
    {
        game->wave++;
        BeginSoloWave(game);
        return;
    }

    if (game->level < SOLO_LEVEL_COUNT)
    {
        game->level++;
        game->wave = 1;
        BeginSoloWave(game);
        return;
    }

    game->finished = true;
    game->campaignWon = true;
    game->score += game->lives * 500;
    if (game->score > game->highScore)
    {
        game->highScore = game->score;
        SaveSoloHighScore(game->highScore);
    }
}

static void UpdateSoloCampaign(SoloCampaign *game,
                               float dt,
                               Vector2 worldMouse)
{
    dt = ClampValue(dt, 0.0f, 0.05f);
    game->elapsed += dt;
    game->waveBannerTimer = fmaxf(0.0f, game->waveBannerTimer - dt);
    game->shakeTime = fmaxf(0.0f, game->shakeTime - dt);
    if (game->shakeTime <= 0.0f) game->shakeStrength = 0.0f;
    for (int i = 0; i < SOLO_MAX_ENEMIES; i++)
        game->enemies[i].hitFlash = fmaxf(0.0f, game->enemies[i].hitFlash - dt);
    for (int i = 0; i < SOLO_MAX_PARTICLES; i++)
    {
        SoloParticle *particle = &game->particles[i];
        if (!particle->active) continue;
        particle->position.x += particle->velocity.x * dt;
        particle->position.y += particle->velocity.y * dt;
        particle->velocity.x *= 0.96f;
        particle->velocity.y *= 0.96f;
        particle->life -= dt;
        if (particle->life <= 0.0f) particle->active = false;
    }
    for (int i = 0; i < SOLO_MAX_SHOCKWAVES; i++)
    {
        SoloShockwave *wave = &game->shockwaves[i];
        if (!wave->active) continue;
        wave->age += dt;
        if (wave->age >= 0.46f) wave->active = false;
    }
    game->controlsHintTimer = fmaxf(0.0f, game->controlsHintTimer - dt);
    game->damageFlash = fmaxf(0.0f, game->damageFlash - dt);
    game->invulnerability = fmaxf(0.0f, game->invulnerability - dt);
    game->playerFireCooldown = fmaxf(0.0f, game->playerFireCooldown - dt);

    if (game->finished || game->paused)
        return;

    Vector2 aim = SoloDirection(game->playerPosition, worldMouse);
    game->playerAngle = atan2f(aim.y, aim.x) * (180.0f / 3.14159265f);

    Vector2 movement = {0.0f, 0.0f};
    if (IsKeyDown(KEY_W))
        movement.y -= 1.0f;
    if (IsKeyDown(KEY_S))
        movement.y += 1.0f;
    if (IsKeyDown(KEY_A))
        movement.x -= 1.0f;
    if (IsKeyDown(KEY_D))
        movement.x += 1.0f;

    float moveLength = sqrtf(movement.x * movement.x + movement.y * movement.y);
    if (moveLength > 0.0f)
    {
        movement.x /= moveLength;
        movement.y /= moveLength;
        float boost = IsKeyDown(KEY_LEFT_SHIFT) ? 1.35f : 1.0f;
        game->playerPosition.x += movement.x * 340.0f * boost * dt;
        game->playerPosition.y += movement.y * 340.0f * boost * dt;
    }
    game->playerPosition.x = ClampValue(game->playerPosition.x, 44.0f, (float)MAP_SIZE - 44.0f);
    game->playerPosition.y = ClampValue(game->playerPosition.y, 44.0f, (float)MAP_SIZE - 44.0f);

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && game->playerFireCooldown <= 0.0f)
    {
        Vector2 muzzle = {game->playerPosition.x + aim.x * 34.0f,
                          game->playerPosition.y + aim.y * 34.0f};
        if (selectedWeapon == 1)
        {
            AddSoloProjectile(game, false, muzzle, aim, 540.0f, 74, selectedWeapon);
            game->playerFireCooldown = 0.46f;
        }
        else if (selectedWeapon == 2)
        {
            for (int pellet = -1; pellet <= 1; pellet++)
            {
                float angle = atan2f(aim.y, aim.x) + pellet * 0.19f;
                Vector2 direction = {cosf(angle), sinf(angle)};
                AddSoloProjectile(game, false, muzzle, direction, 690.0f, 24, selectedWeapon);
            }
            game->playerFireCooldown = 0.34f;
        }
        else
        {
            Vector2 side = {-aim.y, aim.x};
            Vector2 upper = {muzzle.x + side.x * 8.0f, muzzle.y + side.y * 8.0f};
            Vector2 lower = {muzzle.x - side.x * 8.0f, muzzle.y - side.y * 8.0f};
            AddSoloProjectile(game, false, upper, aim, 820.0f, 24, selectedWeapon);
            AddSoloProjectile(game, false, lower, aim, 820.0f, 24, selectedWeapon);
            game->playerFireCooldown = 0.16f;
        }
        game->shakeTime = fmaxf(game->shakeTime, 0.045f);
        game->shakeStrength = fmaxf(game->shakeStrength, 0.9f);
        SpawnSoloBurst(game, muzzle, weaponPresets[selectedWeapon].color, 3, aim);
    }

    if (game->betweenWaves)
    {
        game->intermission -= dt;
        if (game->intermission <= 0.0f)
            AdvanceSoloWave(game);
        return;
    }

    game->spawnCooldown -= dt;
    float spawnInterval = ClampValue(0.9f - game->level * 0.065f - game->wave * 0.035f, 0.42f, 0.9f);
    if (game->enemiesToSpawn > 0 && game->spawnCooldown <= 0.0f)
    {
        SpawnSoloEnemy(game);
        game->spawnCooldown = spawnInterval;
    }

    for (int i = 0; i < SOLO_MAX_PROJECTILES; i++)
    {
        SoloProjectile *projectile = &game->projectiles[i];
        if (!projectile->active)
            continue;

        projectile->position.x += projectile->velocity.x * dt;
        projectile->position.y += projectile->velocity.y * dt;
        projectile->life -= dt;
        if (projectile->life <= 0.0f ||
            projectile->position.x < 0.0f || projectile->position.y < 0.0f ||
            projectile->position.x > MAP_SIZE || projectile->position.y > MAP_SIZE)
        {
            projectile->active = false;
            continue;
        }

        if (!projectile->hostile)
        {
            for (int e = 0; e < SOLO_MAX_ENEMIES; e++)
            {
                SoloEnemy *enemy = &game->enemies[e];
                if (!enemy->active)
                    continue;
                float radius = enemy->type == SOLO_GUARDIAN ? 42.0f : 25.0f;
                if (SoloDistance(projectile->position, enemy->position) < radius)
                {
                    projectile->active = false;
                    enemy->hp -= (float)projectile->damage;
                    enemy->hitFlash = 0.055f;
                    Vector2 impactBias = {-projectile->velocity.x, -projectile->velocity.y};
                    SpawnSoloBurst(game, projectile->position, weaponPresets[projectile->weapon].color, 7, impactBias);
                    if (enemy->hp <= 0.0f)
                    {
                        enemy->active = false;
                        AddSoloShockwave(game, enemy->position);
                        SpawnSoloBurst(game, enemy->position, Color{255, 196, 112, 255}, 15, impactBias);
                        game->shakeTime = 0.18f;
                        game->shakeStrength = 7.5f;
                        game->kills++;
                        int baseScore = enemy->type == SOLO_GUARDIAN ? 650 :
                                        enemy->type == SOLO_HUNTER ? 180 : 100;
                        game->score += baseScore * game->level;
                        if (game->score > game->highScore)
                        {
                            game->highScore = game->score;
                            SaveSoloHighScore(game->highScore);
                        }
                    }
                    break;
                }
            }
        }
        else if (game->invulnerability <= 0.0f &&
                 SoloDistance(projectile->position, game->playerPosition) < 25.0f)
        {
            projectile->active = false;
            DamageSoloPlayer(game, projectile->damage);
        }
    }

    int activeEnemies = 0;
    for (int i = 0; i < SOLO_MAX_ENEMIES; i++)
    {
        SoloEnemy *enemy = &game->enemies[i];
        if (!enemy->active)
            continue;

        activeEnemies++;
        Vector2 toPlayer = SoloDirection(enemy->position, game->playerPosition);
        float distance = SoloDistance(enemy->position, game->playerPosition);
        Vector2 velocity = toPlayer;
        float speed = 110.0f;

        if (enemy->type == SOLO_HUNTER)
        {
            float side = sinf(game->elapsed * 1.4f + enemy->orbitPhase) >= 0.0f ? 1.0f : -1.0f;
            Vector2 tangent = {-toPlayer.y * side, toPlayer.x * side};
            velocity.x = toPlayer.x * 0.35f + tangent.x * 0.94f;
            velocity.y = toPlayer.y * 0.35f + tangent.y * 0.94f;
            float length = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
            if (length > 0.0f)
            {
                velocity.x /= length;
                velocity.y /= length;
            }
            if (distance < 210.0f)
            {
                velocity.x -= toPlayer.x * 0.75f;
                velocity.y -= toPlayer.y * 0.75f;
            }
            speed = 145.0f;
        }
        else if (enemy->type == SOLO_GUARDIAN)
        {
            speed = 62.0f;
            if (distance < 310.0f)
                velocity = {-toPlayer.x * 0.35f, -toPlayer.y * 0.35f};
        }
        else
        {
            if (distance < 170.0f)
                velocity = {-toPlayer.x * 0.65f, -toPlayer.y * 0.65f};
            speed = 125.0f;
        }

        enemy->position.x = ClampValue(enemy->position.x + velocity.x * speed * dt, 55.0f, (float)MAP_SIZE - 55.0f);
        enemy->position.y = ClampValue(enemy->position.y + velocity.y * speed * dt, 55.0f, (float)MAP_SIZE - 55.0f);
        enemy->angle = atan2f(toPlayer.y, toPlayer.x) * (180.0f / 3.14159265f);

        enemy->fireCooldown -= dt;
        float fireRate = enemy->type == SOLO_GUARDIAN ? 0.9f :
                         enemy->type == SOLO_HUNTER ? 1.15f : 1.55f;
        if (enemy->fireCooldown <= 0.0f && distance < 850.0f)
        {
            float spread = enemy->type == SOLO_GUARDIAN
                               ? (float)GetRandomValue(-12, 12) * 0.015f
                               : (float)GetRandomValue(-8, 8) * 0.012f;
            float a = enemy->angle * (3.14159265f / 180.0f) + spread;
            Vector2 direction = {cosf(a), sinf(a)};
            Vector2 muzzle = {enemy->position.x + direction.x * 24.0f,
                              enemy->position.y + direction.y * 24.0f};
            int damage = enemy->type == SOLO_GUARDIAN ? 17 :
                         enemy->type == SOLO_HUNTER ? 12 : 9;
            float shotSpeed = enemy->type == SOLO_GUARDIAN ? 310.0f : 390.0f;
            AddSoloProjectile(game, true, muzzle, direction, shotSpeed, damage);
            enemy->fireCooldown = fireRate + (float)GetRandomValue(0, 50) / 100.0f;
        }

        float contactRadius = enemy->type == SOLO_GUARDIAN ? 52.0f : 34.0f;
        if (distance < contactRadius && game->invulnerability <= 0.0f)
            DamageSoloPlayer(game, enemy->type == SOLO_GUARDIAN ? 24 : 16);
    }

    if (game->enemiesToSpawn == 0 && activeEnemies == 0)
    {
        game->betweenWaves = true;
        game->intermission = 2.3f;
        game->waveBannerTimer = 0.0f;
    }
}
static void DrawSoloEnemy(const SoloEnemy &enemy)
{
    Color hull = enemy.hitFlash > 0.0f ? WHITE : enemy.type == SOLO_GUARDIAN
                     ? Color{255, 120, 126, 255}
                     : enemy.type == SOLO_HUNTER
                           ? Color{196, 139, 255, 255}
                           : Color{255, 177, 107, 255};
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f + enemy.orbitPhase);

    DrawCircleV(enemy.position, enemy.type == SOLO_GUARDIAN ? 49.0f : 31.0f,
                Fade(hull, 0.08f + pulse * 0.035f));
    if (enemy.type == SOLO_GUARDIAN)
    {
        DrawCircleLines((int)enemy.position.x, (int)enemy.position.y, 43.0f, Fade(hull, 0.55f));
        DrawCircleLines((int)enemy.position.x, (int)enemy.position.y, 34.0f, Fade(hull, 0.25f));
        DrawPoly(enemy.position, 8, 31.0f, enemy.angle + 22.5f, Color{31, 23, 39, 255});
        DrawPolyLinesEx(enemy.position, 8, 31.0f, enemy.angle + 22.5f, 3.5f, hull);
        DrawLineEx({enemy.position.x - 14.0f, enemy.position.y},
                   {enemy.position.x + 14.0f, enemy.position.y}, 5.0f, Fade(hull, 0.9f));
    }
    else if (enemy.type == SOLO_HUNTER)
    {
        DrawPoly(enemy.position, 4, 22.0f, enemy.angle + 45.0f, Color{30, 24, 43, 255});
        DrawPolyLinesEx(enemy.position, 4, 22.0f, enemy.angle + 45.0f, 3.0f, hull);
        float a = enemy.angle * 0.0174533f;
        DrawLineEx({enemy.position.x - cosf(a) * 13.0f, enemy.position.y - sinf(a) * 13.0f},
                   {enemy.position.x + cosf(a) * 24.0f, enemy.position.y + sinf(a) * 24.0f},
                   4.0f, Fade(hull, 0.9f));
    }
    else
    {
        DrawPoly(enemy.position, 3, 20.0f, enemy.angle, Color{37, 28, 31, 255});
        DrawPolyLinesEx(enemy.position, 3, 20.0f, enemy.angle, 3.0f, hull);
        DrawCircleV(enemy.position, 5.0f, Fade(hull, 0.9f));
    }

    DrawCircleV(enemy.position, enemy.type == SOLO_GUARDIAN ? 7.0f : 4.0f, WHITE);

    float barWidth = enemy.type == SOLO_GUARDIAN ? 78.0f : 48.0f;
    float barY = enemy.position.y - (enemy.type == SOLO_GUARDIAN ? 58.0f : 39.0f);
    Rectangle bar = {enemy.position.x - barWidth / 2.0f, barY, barWidth, 6.0f};
    DrawRectangleRec(bar, Fade(BLACK, 0.75f));
    Rectangle fill = bar;
    fill.width *= ClampValue(enemy.hp / enemy.maxHp, 0.0f, 1.0f);
    DrawRectangleRec(fill, hull);

    if (enemy.type == SOLO_GUARDIAN)
        DrawUiText("GUARDIAN", (int)enemy.position.x - 30, (int)barY - 19, 11, hull);
}

static void DrawSoloWorld(const Camera2D &camera,
                          const SoloCampaign &game,
                          int selectedColor,
                          Vector2 worldMouse,
                          Vector2 mousePoint,
                          Rectangle restartButton,
                          Rectangle menuButton)
{
    BeginMode2D(camera);
    DrawArena(game.level);
    DrawReactiveGrid(game);

    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 12.0f);
    for (int i = 0; i < SOLO_MAX_PROJECTILES; i++)
    {
        const SoloProjectile &projectile = game.projectiles[i];
        if (!projectile.active)
            continue;

        Color shot = projectile.hostile
                         ? Color{255, 114, 125, 255}
                         : weaponPresets[projectile.weapon].color;
        Vector2 tail = {projectile.position.x - projectile.velocity.x * 0.035f,
                        projectile.position.y - projectile.velocity.y * 0.035f};
        DrawLineEx(tail, projectile.position, projectile.hostile ? 3.0f : 4.0f, Fade(shot, 0.6f));
        DrawCircleV(projectile.position, 9.0f + pulse * 2.0f, Fade(shot, 0.18f));
        DrawCircleV(projectile.position, projectile.hostile ? 4.5f : 5.5f, shot);
        DrawCircleV(projectile.position, 2.0f, WHITE);
    }

    for (int i = 0; i < SOLO_MAX_ENEMIES; i++)
        if (game.enemies[i].active)
            DrawSoloEnemy(game.enemies[i]);

    for (int i = 0; i < SOLO_MAX_PARTICLES; i++)
    {
        const SoloParticle &particle = game.particles[i];
        if (!particle.active) continue;
        float alpha = ClampValue(particle.life / particle.maxLife, 0.0f, 1.0f);
        DrawCircleV(particle.position, particle.radius, Fade(particle.color, alpha));
    }

    bool visible = game.invulnerability <= 0.0f || ((int)(GetTime() * 12.0) % 2 == 0);
    if (visible)
        DrawDrone(game.playerPosition, game.playerAngle, playerColors[selectedColor], true);
    DrawCrosshair(worldMouse);
    EndMode2D();

    int width = GetScreenWidth();
    int height = GetScreenHeight();
    DrawRectangle(20, 20, 285, 176, Fade(Color{8, 13, 27, 255}, 0.91f));
    DrawRectangleLinesEx({20, 20, 285, 176}, 1.5f, Fade(Color{92, 205, 244, 255}, 0.42f));
    DrawRectangle(20, 20, 3, 176, Fade(Color{92, 205, 244, 255}, 0.88f));
    DrawUiText("NEON PROTOCOL", 38, 34, 16, Color{116, 213, 248, 255});
    DrawUiText(TextFormat("SECTOR  %02d / %02d", game.level, SOLO_LEVEL_COUNT), 38, 63, 18, WHITE);
    DrawUiText(TextFormat("WAVE  %02d / %02d", game.wave, SOLO_WAVE_COUNT), 38, 91, 15, Color{196, 183, 255, 255});
    DrawUiText(TextFormat("HULL  %03d%%", game.playerHp), 38, 118, 14, LIGHTGRAY);
    DrawRectangle(38, 142, 242, 7, Fade(WHITE, 0.12f));
    DrawRectangle(38, 142, (int)(242.0f * ClampValue((float)game.playerHp / 100.0f, 0.0f, 1.0f)),
                  7, game.playerHp > 55 ? Color{128, 230, 183, 255} : Color{255, 127, 127, 255});
    DrawUiText(TextFormat("LIVES  %d", game.lives), 38, 160, 13, Fade(WHITE, 0.8f));

    Rectangle scorePanel = {(float)width - 242.0f, 20.0f, 222.0f, 86.0f};
    DrawRectangleRec(scorePanel, Fade(Color{8, 13, 27, 255}, 0.91f));
    DrawRectangleLinesEx(scorePanel, 1.0f, Fade(Color{187, 151, 255, 255}, 0.36f));
    DrawUiText("RUN SCORE", (int)scorePanel.x + 15, (int)scorePanel.y + 12, 12, Fade(LIGHTGRAY, 0.78f));
    DrawUiText(TextFormat("%07d", game.score), (int)scorePanel.x + 15, (int)scorePanel.y + 30, 25, WHITE);
    DrawUiText(TextFormat("BEST  %07d", game.highScore), (int)scorePanel.x + 15, (int)scorePanel.y + 62, 12,
               Color{187, 151, 255, 255});

    if (game.controlsHintTimer > 0.0f)
    {
        Rectangle hintPanel = {(width - 710.0f) * 0.5f, (float)height - 124.0f, 710.0f, 78.0f};
        DrawRectangleRec(hintPanel, Fade(Color{8, 13, 27, 255}, 0.95f));
        DrawRectangleLinesEx(hintPanel, 1.0f, Fade(Color{92, 205, 244, 255}, 0.55f));
        DrawUiText("FLIGHT CONTROLS", (int)hintPanel.x + 16, (int)hintPanel.y + 8, 12,
                   Color{116, 213, 248, 255});

        const char *keys[] = {"WASD", "SHIFT", "MOUSE", "LMB", "P"};
        const char *actions[] = {"MOVE", "BOOST", "AIM", "FIRE", "PAUSE"};
        float cellWidth = 126.0f;
        float gap = 8.0f;
        float rowWidth = 5.0f * cellWidth + 4.0f * gap;
        float startX = hintPanel.x + (hintPanel.width - rowWidth) * 0.5f;
        for (int i = 0; i < 5; i++)
        {
            Rectangle keyCell = {startX + i * (cellWidth + gap), hintPanel.y + 34.0f, cellWidth, 30.0f};
            DrawRectangleRec(keyCell, Fade(Color{111, 211, 247, 255}, 0.07f));
            DrawRectangleLinesEx(keyCell, 1.0f, Fade(Color{111, 211, 247, 255}, 0.28f));
            DrawUiText(keys[i], (int)keyCell.x + 8, (int)keyCell.y + 8, 11, WHITE);
            int actionWidth = MeasureUiText(actions[i], 11);
            DrawUiText(actions[i], (int)(keyCell.x + keyCell.width - actionWidth - 8),
                       (int)keyCell.y + 8, 11, Color{143, 231, 196, 255});
        }
    }

    const char *controls = "WASD MOVE   SHIFT BOOST   MOUSE AIM   LMB FIRE   P PAUSE   ESC MENU";
    DrawUiText(controls, 20, height - 25, 12, Fade(WHITE, 0.55f));

    if (game.damageFlash > 0.0f)
        DrawRectangle(0, 0, width, height, Fade(Color{255, 77, 93, 255}, game.damageFlash * 0.22f));

    if (game.waveBannerTimer > 0.0f && !game.paused && !game.finished)
    {
        float alpha = ClampValue(game.waveBannerTimer, 0.0f, 1.0f);
        const char *banner = TextFormat("SECTOR %02d  /  WAVE %02d", game.level, game.wave);
        DrawUiText(banner, (int)(width / 2.0f - MeasureUiText(banner, 28) / 2.0f),
                   (int)(height * 0.19f), 28, Fade(WHITE, alpha));
    }
    else if (game.betweenWaves && !game.finished)
    {
        const char *banner = game.wave == SOLO_WAVE_COUNT && game.level < SOLO_LEVEL_COUNT
                                 ? "SECTOR CLEAR" : "WAVE CLEAR";
        DrawUiText(banner, (int)(width / 2.0f - MeasureUiText(banner, 30) / 2.0f),
                   (int)(height * 0.2f), 30, Color{143, 231, 196, 255});
        const char *next = TextFormat("NEXT WAVE IN  %d", (int)ceilf(game.intermission));
        DrawUiText(next, (int)(width / 2.0f - MeasureUiText(next, 15) / 2.0f),
                   (int)(height * 0.2f) + 42, 15, Fade(WHITE, 0.8f));
    }

    if (game.paused && !game.finished)
    {
        DrawRectangle(0, 0, width, height, Fade(Color{3, 7, 16, 255}, 0.74f));
        DrawUiText("PAUSED", (int)(width / 2.0f - MeasureUiText("PAUSED", 46) / 2.0f),
                   (int)(height / 2.0f - 70.0f), 46, WHITE);
        const char *hint = "PRESS P TO RESUME   /   ESC TO MENU";
        DrawUiText(hint, (int)(width / 2.0f - MeasureUiText(hint, 16) / 2.0f),
                   (int)(height / 2.0f + 4.0f), 16, Fade(WHITE, 0.75f));
    }

    if (game.finished)
    {
        DrawRectangle(0, 0, width, height, Fade(Color{3, 7, 16, 255}, 0.82f));
        Rectangle panel = {(width - 520.0f) / 2.0f, (height - 370.0f) / 2.0f, 520.0f, 370.0f};
        DrawRectangleRec(panel, Fade(Color{9, 15, 30, 255}, 0.98f));
        Color resultColor = game.campaignWon ? Color{143, 231, 196, 255} : Color{255, 127, 145, 255};
        DrawRectangleLinesEx(panel, 2.0f, resultColor);

        const char *title = game.campaignWon ? "CAMPAIGN COMPLETE" : "RUN OVER";
        int titleSize = game.campaignWon ? 34 : 42;
        DrawUiText(title, (int)(width / 2.0f - MeasureUiText(title, titleSize) / 2.0f),
                   (int)panel.y + 45, titleSize, resultColor);
        const char *finalScore = TextFormat("FINAL SCORE   %07d", game.score);
        DrawUiText(finalScore, (int)(width / 2.0f - MeasureUiText(finalScore, 22) / 2.0f),
                   (int)panel.y + 112, 22, WHITE);
        const char *best = TextFormat("PERSONAL BEST   %07d", game.highScore);
        DrawUiText(best, (int)(width / 2.0f - MeasureUiText(best, 16) / 2.0f),
                   (int)panel.y + 153, 16, Color{187, 151, 255, 255});
        int cleared = game.campaignWon ? SOLO_LEVEL_COUNT : game.level - (game.wave == 1 ? 1 : 0);
        const char *progress = TextFormat("SECTORS CLEARED  %d / %d", cleared, SOLO_LEVEL_COUNT);
        DrawUiText(progress, (int)(width / 2.0f - MeasureUiText(progress, 14) / 2.0f),
                   (int)panel.y + 193, 14, Fade(WHITE, 0.76f));

        Rectangle restart = restartButton;
        Rectangle back = menuButton;
        restart.y = panel.y + 252.0f;
        back.y = restart.y + 58.0f;
        DrawButton(restart, "PLAY AGAIN", Color{92, 205, 244, 255}, mousePoint);
        DrawButton(back, "MAIN MENU", Color{187, 151, 255, 255}, mousePoint);
    }
}
static void DrawScoreboard(const RemotePlayer players[MAX_PLAYERS],
                           int myId,
                           int screenWidth)
{
    int activeCount = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].active)
            activeCount++;
    }

    float x = (float)screenWidth - 270.0f;
    float y = 16.0f;
    float height = 42.0f + activeCount * 22.0f;

    DrawRectangle(
        (int)x, (int)y, 252, (int)height,
        Fade(Color{5, 10, 22, 255}, 0.88f));

    DrawRectangleLinesEx(
        {x, y, 252, height},
        2,
        Fade(PURPLE, 0.75f));

    DrawUiText("SCOREBOARD", (int)x + 12, (int)y + 8, 16, PURPLE);
    DrawUiText("K / D", (int)x + 185, (int)y + 8, 13, LIGHTGRAY);

    int row = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        Color color = GetPlayerColor(players[i].color);
        int rowY = (int)y + 31 + row * 22;

        DrawCircleV({x + 17, (float)rowY + 7}, 5, color);

        DrawUiText(
            TextFormat("PILOT %02d%s", i, i == myId ? " YOU" : ""),
            (int)x + 30,
            rowY,
            13,
            i == myId ? WHITE : LIGHTGRAY);

        DrawUiText(
            TextFormat("%u / %u",
                       (unsigned)players[i].kills,
                       (unsigned)players[i].deaths),
            (int)x + 185,
            rowY,
            13,
            color);

        row++;
    }
}

static void DrawLobby(const RemotePlayer players[MAX_PLAYERS],
                      int myId,
                      bool localReady,
                      Vector2 mousePoint,
                      float cx,
                      float cy,
                      Rectangle readyButton,
                      Rectangle leaveButton)
{
    DrawBackdrop();

    Rectangle panel = {
        cx - 340,
        cy - 250,
        680,
        500};

    DrawRectangleRec(panel, Fade(Color{5, 10, 22, 255}, 0.94f));
    DrawRectangleLinesEx(panel, 2, Fade(SKYBLUE, 0.8f));

    DrawUiText(
        "PILOT LOBBY",
        (int)(cx - MeasureUiText("PILOT LOBBY", 38) / 2),
        (int)cy - 220,
        38,
        SKYBLUE);

    DrawUiText(
        "CONNECTED TO SERVER",
        (int)(cx - MeasureUiText("CONNECTED TO SERVER", 16) / 2),
        (int)cy - 175,
        16,
        LIME);

    int playerCount = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].active)
            playerCount++;
    }

    DrawUiText(
        TextFormat("PILOTS IN SESSION: %d / %d",
                   playerCount, MAX_PLAYERS),
        (int)cx - 290,
        (int)cy - 140,
        18,
        LIGHTGRAY);

    int row = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].active)
            continue;

        int rowY = (int)cy - 110 + row * 22;
        Color color = GetPlayerColor(players[i].color);

        DrawCircleV({cx - 275, (float)rowY + 8}, 6, color);

        DrawUiText(
            TextFormat("PILOT %02d%s",
                       i,
                       i == myId ? "  (YOU)" : ""),
            (int)cx - 258,
            rowY,
            16,
            WHITE);

        const char *status = !players[i].connected
                                 ? "REJOINING"
                                 : (players[i].ready ? "READY" : "NOT READY");
        Color statusColor = !players[i].connected
                                ? ORANGE
                                : (players[i].ready ? LIME : GRAY);
        DrawUiText(status, (int)cx + 145, rowY, 15, statusColor);

        row++;
    }

    if (playerCount == 0)
    {
        DrawUiText(
            "Waiting for the server...",
            (int)(cx - MeasureUiText("Waiting for the server...", 16) / 2),
            (int)cy - 60,
            16,
            GRAY);
    }

    DrawButton(
        readyButton,
        localReady ? "CANCEL READY" : "READY UP",
        localReady ? LIME : SKYBLUE,
        mousePoint);

    DrawButton(
        leaveButton,
        "LEAVE SESSION",
        RED,
        mousePoint);

    const char *lobbyHint = playerCount < NET_MIN_PLAYERS
                               ? TextFormat("%d PLAYERS NEEDED TO START", NET_MIN_PLAYERS)
                               : "PRESS ENTER OR SPACE TO TOGGLE READY";
    DrawUiText(
        lobbyHint,
        (int)(cx - MeasureUiText(lobbyHint, 15) / 2),
        (int)cy + 118,
        15,
        Fade(WHITE, 0.72f));
}

static void DrawNetgraph(const int history[80], int writeIndex)
{
    Rectangle panel = {20.0f, 180.0f, 270.0f, 148.0f};
    DrawRectangleRec(panel, Fade(Color{5, 10, 22, 255}, 0.93f));
    DrawRectangleLinesEx(panel, 1.0f, Fade(Color{111, 211, 247, 255}, 0.52f));
    DrawRectangle((int)panel.x, (int)panel.y, 3, (int)panel.height,
                  Fade(Color{111, 211, 247, 255}, 0.85f));
    DrawUiText("NETGRAPH / F3", (int)panel.x + 12, (int)panel.y + 8, 12,
               Color{111, 211, 247, 255});

    Rectangle chart = {panel.x + 12.0f, panel.y + 30.0f, panel.width - 24.0f, 56.0f};
    DrawRectangleLinesEx(chart, 1.0f, Fade(WHITE, 0.12f));
    for (int line = 1; line < 4; line++)
    {
        float y = chart.y + chart.height * line / 4.0f;
        DrawLineV({chart.x, y}, {chart.x + chart.width, y}, Fade(WHITE, 0.07f));
    }
    for (int i = 1; i < 80; i++)
    {
        int a = history[(writeIndex + i - 1) % 80];
        int b = history[(writeIndex + i) % 80];
        float x1 = chart.x + chart.width * (i - 1) / 79.0f;
        float x2 = chart.x + chart.width * i / 79.0f;
        float y1 = chart.y + chart.height - ClampValue((float)a / 200.0f, 0.0f, 1.0f) * chart.height;
        float y2 = chart.y + chart.height - ClampValue((float)b / 200.0f, 0.0f, 1.0f) * chart.height;
        DrawLineEx({x1, y1}, {x2, y2}, 1.7f, Fade(Color{111, 211, 247, 255}, 0.88f));
    }

    int ping = network_ping_ms();
    const char *pingText = ping >= 0 ? TextFormat("PING  %d ms", ping) : "PING  --";
    DrawUiText(pingText, (int)panel.x + 12, (int)panel.y + 95, 11, WHITE);
    unsigned int pingsSent = 0;
    network_get_ping_counters(&pingsSent, NULL);
    const char *lossText = pingsSent > 0
                               ? TextFormat("PING LOSS  %d%%", network_packet_loss_percent())
                               : "PING LOSS  --";
    Color lossColor = pingsSent > 0 && network_packet_loss_percent() > 5
                          ? Color{255, 142, 133, 255}
                          : Fade(Color{143, 231, 196, 255}, pingsSent > 0 ? 1.0f : 0.65f);
    DrawUiText(lossText, (int)panel.x + 112, (int)panel.y + 95, 11, lossColor);
    DrawUiText(TextFormat("IN  %u B/s   OUT  %u B/s",
                          network_bytes_in_per_second(), network_bytes_out_per_second()),
               (int)panel.x + 12, (int)panel.y + 118, 10, Fade(WHITE, 0.74f));
}
int main()
{
    InitWindow(1920, 1080, "Neon Protocol");
    ToggleFullscreen();
    SetTargetFPS(60);
    SetExitKey(0);
    LoadUiFont();

    SoloCampaign solo{};
    solo.highScore = LoadSoloHighScore();

    GameState currentState = MENU;
    ConnectionStatus connectionStatus = CONNECTION_NONE;

    bool exitGame = false;
    bool localReady = false;
    bool aboutProbeReady = false;
    bool aboutProbeAvailable = false;
    bool showNetgraph = false;
    int netgraphPingHistory[80] = {};
    int netgraphWriteIndex = 0;
    double lastNetgraphSampleAt = 0.0;

    char serverAddress[96] = "127.0.0.1:8080";
    bool hasSavedServerAddress =
        LoadSavedServerAddress(serverAddress, sizeof(serverAddress));
    int serverAddressLength = (int)strlen(serverAddress);
    const char *joinError = NULL;

    double connectionStartedAt = 0.0;
    double lastServerPacketAt = 0.0;

    uint16_t targetX = 400;
    uint16_t targetY = 300;
    uint16_t myHp = 100;

    uint16_t matchPhase = NET_PHASE_LOBBY;
    uint16_t secondsLeft = 0;

    int winnerId = NET_NO_WINNER;
    int myId = -1;
    int selectedColor = 0;

    RemotePlayer players[MAX_PLAYERS]{};
    NetBullet bullets[MAX_BULLETS]{};

    Camera2D camera{};
    camera.target = {(float)targetX, (float)targetY};
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    while (!WindowShouldClose() && !exitGame)
    {
        if (IsKeyPressed(KEY_F11))
        {
            if (IsWindowFullscreen())
            {
                ToggleFullscreen();
                SetWindowSize(960, 540);
                SetWindowPosition(100, 100);
            }
            else
            {
                SetWindowSize(1920, 1080);
                ToggleFullscreen();
            }
        }

        float cx = GetScreenWidth() / 2.0f;
        float cy = GetScreenHeight() / 2.0f;

        camera.offset = {cx, cy};

        Vector2 mousePoint = GetMousePosition();
        Vector2 worldMouse = GetScreenToWorld2D(mousePoint, camera);

        float angle = atan2f(
            worldMouse.y - targetY,
            worldMouse.x - targetX);

        int16_t angleDegrees =
            (int16_t)(angle * (180.0f / 3.14159265f));

        float menuX = ClampValue(GetScreenWidth() * 0.075f, 32.0f, 150.0f);
        float menuGap = 14.0f;
        float menuButtonWidth = ClampValue(
            (GetScreenWidth() - menuX * 2.0f - menuGap) * 0.5f,
            220.0f,
            310.0f);

        Rectangle btnSingleplayer = {menuX, cy - 30, menuButtonWidth, 64};
        Rectangle btnPlay = {menuX + menuButtonWidth + menuGap, cy - 30, menuButtonWidth, 64};
        Rectangle btnSettings = {menuX, cy + 54, 160, 42};
        Rectangle btnAbout = {menuX + 172, cy + 54, 142, 42};
        Rectangle btnExit = {menuX + 326, cy + 54, 142, 42};
        float hangarLeftX = GetScreenWidth() * 0.06f;
        float hangarLeftWidth = GetScreenWidth() * 0.42f;
        float hangarLeftCenter = hangarLeftX + hangarLeftWidth * 0.5f;
        float weaponCardGap = 8.0f;
        float weaponCardWidth = (hangarLeftWidth - weaponCardGap * 2.0f) / NET_WEAPON_COUNT;
        Rectangle hangarWeaponCards[NET_WEAPON_COUNT];
        for (int i = 0; i < NET_WEAPON_COUNT; i++)
            hangarWeaponCards[i] = {hangarLeftX + i * (weaponCardWidth + weaponCardGap),
                                    cy - 28.0f, weaponCardWidth, 58.0f};
        Rectangle hangarPanel = {GetScreenWidth() * 0.54f, cy - 220.0f,
                                 GetScreenWidth() * 0.40f, 440.0f};
        Vector2 hangarShip = {hangarPanel.x + 68.0f, hangarPanel.y + hangarPanel.height * 0.52f};
        Vector2 hangarTarget = {hangarPanel.x + hangarPanel.width - 70.0f, hangarShip.y};
        Rectangle btnBack = {hangarLeftX, cy + 205.0f, 180.0f, 44.0f};

        float aboutPanelWidth = fminf(1040.0f, GetScreenWidth() - 32.0f);
        float aboutPanelHeight = fminf(570.0f, GetScreenHeight() - 24.0f);
        Rectangle aboutPanel = {
            (GetScreenWidth() - aboutPanelWidth) / 2.0f,
            (GetScreenHeight() - aboutPanelHeight) / 2.0f,
            aboutPanelWidth,
            aboutPanelHeight};
        Rectangle btnAboutBack = {
            aboutPanel.x + 24.0f,
            aboutPanel.y + aboutPanel.height - 48.0f,
            144.0f,
            36.0f};

        Rectangle ipBox = {cx - 190, cy - 45, 380, 60};
        Rectangle btnConnect = {cx - 150, cy + 45, 300, 56};
        Rectangle btnJoinBack = {cx - 150, cy + 115, 300, 46};

        Rectangle btnReady = {cx - 155, cy + 138, 310, 52};
        Rectangle btnLeave = {cx - 155, cy + 202, 310, 38};
        Rectangle btnResultsRematch = {cx - 230, cy + 106, 220, 46};
        Rectangle btnResultsMenu = {cx + 10, cy + 106, 220, 46};

        Rectangle btnSoloRestart = {cx - 150, cy + 67, 300, 50};
        Rectangle btnSoloMenu = {cx - 150, cy + 125, 300, 50};
        Rectangle btnRetry = {cx - 150, cy + 40, 300, 56};
        Rectangle btnChangeServer = {cx - 150, cy + 110, 300, 50};

        if (currentState == MENU && GetScreenWidth() > 1150 && GetScreenHeight() >= 700)
        {
            Vector2 heroShip = {GetScreenWidth() * 0.76f, cy - 48.0f};
            UpdateMenuEffects(GetFrameTime(), heroShip, mousePoint);
            bool overMenuButton = CheckCollisionPointRec(mousePoint, btnSingleplayer) ||
                                  CheckCollisionPointRec(mousePoint, btnPlay) ||
                                  CheckCollisionPointRec(mousePoint, btnSettings) ||
                                  CheckCollisionPointRec(mousePoint, btnAbout) ||
                                  CheckCollisionPointRec(mousePoint, btnExit);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overMenuButton &&
                menuEffects.fireCooldown <= 0.0f)
                FireMenuShot(heroShip, mousePoint, playerColors[selectedColor]);
        }

        if (currentState == MENU)
        {
            if (IsKeyPressed(KEY_ENTER))
            {
                char savedAddress[96];
                if (LoadSavedServerAddress(savedAddress, sizeof(savedAddress)))
                {
                    memcpy(serverAddress, savedAddress, strlen(savedAddress) + 1);
                    serverAddressLength = (int)strlen(serverAddress);
                    hasSavedServerAddress = true;
                }
                else
                {
                    hasSavedServerAddress = false;
                }

                if (hasSavedServerAddress &&
                    BeginServerConnection(
                        serverAddress,
                        players,
                        bullets,
                        &myId,
                        &myHp,
                        &matchPhase,
                        &secondsLeft,
                        &winnerId,
                        &localReady,
                        &connectionStatus,
                        &connectionStartedAt,
                        &lastServerPacketAt))
                {
                    currentState = SESSION;
                    joinError = NULL;
                }
                else
                {
                    currentState = JOIN;
                    joinError = hasSavedServerAddress
                                    ? "Saved server address could not be opened. Check it below."
                                    : NULL;
                }
            }

            if (CheckCollisionPointRec(mousePoint, btnSingleplayer) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                StartSoloCampaign(&solo);
                currentState = SINGLEPLAYER;
                camera.target = solo.playerPosition;
                ClampCameraToArena(&camera);
            }

            if (CheckCollisionPointRec(mousePoint, btnPlay) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                currentState = JOIN;
                joinError = NULL;
            }

            if (CheckCollisionPointRec(mousePoint, btnSettings) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                currentState = SETTINGS;
            }

            if (CheckCollisionPointRec(mousePoint, btnAbout) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                currentState = ABOUT;
            }

            if (CheckCollisionPointRec(mousePoint, btnExit) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                exitGame = true;
            }
        }
        else if (currentState == JOIN)
        {
            if (IsKeyPressed(KEY_BACKSPACE) && serverAddressLength > 0)
            {
                serverAddressLength--;
                serverAddress[serverAddressLength] = '\0';
                hasSavedServerAddress = false;
                joinError = NULL;
            }

            int character = 0;

            while ((character = GetCharPressed()) > 0)
            {
                bool isDigit = character >= '0' && character <= '9';
                bool isLower = character >= 'a' && character <= 'z';
                bool isUpper = character >= 'A' && character <= 'Z';
                bool isAddressPunctuation =
                    character == '.' || character == '-' || character == ':';

                if ((isDigit || isLower || isUpper || isAddressPunctuation) &&
                    serverAddressLength < (int)sizeof(serverAddress) - 1)
                {
                    serverAddress[serverAddressLength++] = (char)character;
                    serverAddress[serverAddressLength] = '\0';
                    hasSavedServerAddress = false;
                    joinError = NULL;
                }
            }

            bool clickedConnect =
                CheckCollisionPointRec(mousePoint, btnConnect) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

            if (clickedConnect || IsKeyPressed(KEY_ENTER))
            {
                char parsedHost[96];
                int parsedPort = 0;
                if (ParseServerAddress(
                        serverAddress,
                        parsedHost,
                        sizeof(parsedHost),
                        &parsedPort))
                {
                    hasSavedServerAddress = SaveServerAddress(serverAddress);
                }

                if (BeginServerConnection(
                        serverAddress,
                        players,
                        bullets,
                        &myId,
                        &myHp,
                        &matchPhase,
                        &secondsLeft,
                        &winnerId,
                        &localReady,
                        &connectionStatus,
                        &connectionStartedAt,
                        &lastServerPacketAt))
                {
                    currentState = SESSION;
                    joinError = NULL;
                }
                else
                {
                    joinError =
                        "Invalid address or hostname could not be resolved";
                }
            }

            if ((CheckCollisionPointRec(mousePoint, btnJoinBack) &&
                 IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ||
                IsKeyPressed(KEY_ESCAPE))
            {
                currentState = MENU;
                joinError = NULL;
            }
        }
        else if (currentState == SETTINGS)
        {
            if (CheckCollisionPointRec(mousePoint, btnBack) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                currentState = MENU;
            }

            if (IsKeyPressed(KEY_ESCAPE))
                currentState = MENU;

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                for (int i = 0; i < NET_COLOR_COUNT; i++)
                {
                    Rectangle colorBox = GetColorSwatchRectangle(i, hangarLeftCenter, cy - 150.0f);
                    if (CheckCollisionPointRec(mousePoint, colorBox))
                        selectedColor = i;
                }
                for (int i = 0; i < NET_WEAPON_COUNT; i++)
                    if (CheckCollisionPointRec(mousePoint, hangarWeaponCards[i]))
                        selectedWeapon = i;
            }

            if (IsKeyPressed(KEY_RIGHT))
                selectedColor = (selectedColor + 1) % NET_COLOR_COUNT;
            if (IsKeyPressed(KEY_LEFT))
                selectedColor = (selectedColor + NET_COLOR_COUNT - 1) % NET_COLOR_COUNT;
            if (IsKeyPressed(KEY_DOWN))
                selectedWeapon = (selectedWeapon + 1) % NET_WEAPON_COUNT;
            if (IsKeyPressed(KEY_UP))
                selectedWeapon = (selectedWeapon + NET_WEAPON_COUNT - 1) % NET_WEAPON_COUNT;
        }
        else if (currentState == ABOUT)
        {
            if ((CheckCollisionPointRec(mousePoint, btnAboutBack) &&
                 IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || IsKeyPressed(KEY_ESCAPE))
            {
                network_probe_close();
                aboutProbeReady = false;
                aboutProbeAvailable = false;
                currentState = MENU;
            }
        }
        else if (currentState == SINGLEPLAYER)
        {
            if (solo.finished)
            {
                bool clickRestart = CheckCollisionPointRec(mousePoint, btnSoloRestart) &&
                                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
                if (clickRestart || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R))
                {
                    StartSoloCampaign(&solo);
                    camera.target = solo.playerPosition;
                }

                if ((CheckCollisionPointRec(mousePoint, btnSoloMenu) &&
                     IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ||
                    IsKeyPressed(KEY_ESCAPE))
                {
                    currentState = MENU;
                }
            }
            else
            {
                if (IsKeyPressed(KEY_P))
                    solo.paused = !solo.paused;

                if (IsKeyPressed(KEY_ESCAPE))
                    currentState = MENU;

                if (!solo.paused)
                    UpdateSoloCampaign(&solo, GetFrameTime(), worldMouse);
            }

            camera.target.x += (solo.playerPosition.x - camera.target.x) * 0.12f;
            camera.target.y += (solo.playerPosition.y - camera.target.y) * 0.12f;
            ClampCameraToArena(&camera);
        }
        else if (currentState == SESSION)
        {
            if (IsKeyPressed(KEY_F3))
                showNetgraph = !showNetgraph;

            if (connectionStatus == CONNECTION_FAILED)
            {
                bool clickedRetry =
                    (CheckCollisionPointRec(mousePoint, btnRetry) &&
                     IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ||
                    IsKeyPressed(KEY_ENTER);

                bool clickedChangeServer =
                    CheckCollisionPointRec(mousePoint, btnChangeServer) &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

                if (clickedRetry)
                {
                    if (ConnectToServerAddress(serverAddress))
                    {
                        myId = -1;
                        memset(players, 0, sizeof(players));
                        memset(bullets, 0, sizeof(bullets));

                        connectionStatus = CONNECTION_CONNECTING;
                        connectionStartedAt = GetTime();
                        lastServerPacketAt = connectionStartedAt;
                    }
                }

                if (clickedChangeServer)
                {
                    network_close();
                    connectionStatus = CONNECTION_NONE;
                    currentState = JOIN;
                }

                if (IsKeyPressed(KEY_ESCAPE))
                {
                    network_close();
                    connectionStatus = CONNECTION_NONE;
                    currentState = MENU;
                }
            }
            else
            {
                if (connectionStatus == CONNECTION_CONNECTED &&
                    (matchPhase == NET_PHASE_LOBBY ||
                     matchPhase == NET_PHASE_RESULTS))
                {
                    Rectangle readyButton = matchPhase == NET_PHASE_RESULTS
                                                ? btnResultsRematch
                                                : btnReady;
                    bool clickedReady =
                        CheckCollisionPointRec(mousePoint, readyButton) &&
                        IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

                    if (IsKeyPressed(KEY_ENTER) ||
                        IsKeyPressed(KEY_SPACE) ||
                        clickedReady)
                    {
                        localReady = !localReady;
                    }
                }

                if (connectionStatus == CONNECTION_CONNECTED &&
                    matchPhase == NET_PHASE_LOBBY)
                {
                    bool clickedLeave =
                        CheckCollisionPointRec(mousePoint, btnLeave) &&
                        IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

                    if (clickedLeave)
                    {
                        network_close();
                        connectionStatus = CONNECTION_NONE;
                        localReady = false;
                        currentState = MENU;
                    }
                }

                if (connectionStatus == CONNECTION_CONNECTED &&
                    matchPhase == NET_PHASE_RESULTS &&
                    CheckCollisionPointRec(mousePoint, btnResultsMenu) &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    network_close();
                    connectionStatus = CONNECTION_NONE;
                    localReady = false;
                    currentState = MENU;
                }

                if (currentState == SESSION)
                {
                    uint8_t input = 0;

                    if (connectionStatus == CONNECTION_CONNECTED)
                    {
                        if (localReady)
                        {
                            if (matchPhase == NET_PHASE_RESULTS)
                                input |= NET_INPUT_REMATCH;
                            else if (matchPhase == NET_PHASE_LOBBY ||
                                     matchPhase == NET_PHASE_COUNTDOWN)
                                input |= NET_INPUT_READY;
                        }

                        if (matchPhase == NET_PHASE_PLAYING)
                        {
                            if (IsKeyDown(KEY_W))
                                input |= 0x01;
                            if (IsKeyDown(KEY_S))
                                input |= 0x02;
                            if (IsKeyDown(KEY_A))
                                input |= 0x04;
                            if (IsKeyDown(KEY_D))
                                input |= 0x08;

                            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                                input |= 0x10;
                        }
                    }

                    send_input(
                        input,
                        angleDegrees,
                        (uint16_t)selectedColor,
                        (uint16_t)selectedWeapon);

                    uint16_t previousMatchPhase = matchPhase;
                    if (receive_state(
                            players,
                            bullets,
                            &myId,
                            &matchPhase,
                            &secondsLeft,
                            &winnerId))
                    {
                        double packetReceivedAt = GetTime();
                        lastServerPacketAt = packetReceivedAt;

                        if (myId >= 0 && myId < MAX_PLAYERS &&
                            players[myId].active)
                        {
                            if (players[myId].connected)
                            {
                                connectionStatus = CONNECTION_CONNECTED;
                            }
                            else if (connectionStatus == CONNECTION_CONNECTED)
                            {
                                connectionStatus = CONNECTION_RECONNECTING;
                                connectionStartedAt = packetReceivedAt;
                                localReady = false;
                            }

                            targetX = players[myId].x;
                            targetY = players[myId].y;
                            myHp = players[myId].hp;
                        }

                        if ((matchPhase == NET_PHASE_PLAYING &&
                             previousMatchPhase != NET_PHASE_PLAYING) ||
                            (matchPhase == NET_PHASE_RESULTS &&
                             previousMatchPhase != NET_PHASE_RESULTS) ||
                            (matchPhase == NET_PHASE_LOBBY &&
                             previousMatchPhase == NET_PHASE_RESULTS))
                        {
                            localReady = false;
                        }
                    }

                    double now = GetTime();

                    if (connectionStatus == CONNECTION_CONNECTING &&
                        now - connectionStartedAt > 5.0)
                    {
                        connectionStatus = CONNECTION_FAILED;
                        network_close();
                    }
                    else if (connectionStatus == CONNECTION_CONNECTED &&
                             now - lastServerPacketAt > 1.25)
                    {
                        // Keep the same UDP socket while trying to resume. If
                        // the server still remembers us, our player slot survives.
                        connectionStatus = CONNECTION_RECONNECTING;
                        connectionStartedAt = now;
                        localReady = false;
                        myId = -1;
                    }
                    else if (connectionStatus == CONNECTION_RECONNECTING &&
                             now - connectionStartedAt > 12.0)
                    {
                        connectionStatus = CONNECTION_FAILED;
                        network_close();
                    }

                    if (connectionStatus == CONNECTION_CONNECTED)
                    {
                        camera.target.x +=
                            (targetX - camera.target.x) * 0.1f;

                        camera.target.y +=
                            (targetY - camera.target.y) * 0.1f;

                        ClampCameraToArena(&camera);
                    }

                    if (IsKeyPressed(KEY_ESCAPE))
                    {
                        network_close();
                        connectionStatus = CONNECTION_NONE;
                        localReady = false;
                        currentState = MENU;
                    }
                }
            }
        }

        if (currentState == SESSION &&
            connectionStatus == CONNECTION_CONNECTED)
        {
            UpdateOnlineJuice(
                &onlineJuice,
                players,
                myId,
                GetFrameTime(),
                matchPhase == NET_PHASE_PLAYING &&
                    IsMouseButtonDown(MOUSE_BUTTON_LEFT),
                selectedWeapon);
        }

        if (currentState == SETTINGS)
        {
            float dt = GetFrameTime();
            const WeaponPreset &preset = weaponPresets[selectedWeapon];
            float targetStats[4] = {preset.fireRate, preset.damage, preset.velocity, preset.shield};
            float blend = 1.0f - expf(-dt * 7.5f);
            for (int i = 0; i < 4; i++)
                displayedWeaponStats[i] += (targetStats[i] - displayedWeaponStats[i]) * blend;
            UpdateHangarPreview(dt, hangarShip, hangarTarget);
        }

        if (currentState == ABOUT && !aboutProbeReady)
        {
            char probeHost[96];
            int probePort = 0;
            aboutProbeReady = true;
            if (hasSavedServerAddress &&
                ParseServerAddress(serverAddress, probeHost, sizeof(probeHost), &probePort))
                aboutProbeAvailable = network_probe_start(probeHost, probePort) != 0;
        }
        if (currentState == ABOUT || currentState == SESSION)
            network_ping_tick();

        if (currentState == SESSION && GetTime() - lastNetgraphSampleAt >= 0.25)
        {
            int ping = network_ping_ms();
            netgraphPingHistory[netgraphWriteIndex] = ping < 0 ? 0 : ping;
            netgraphWriteIndex = (netgraphWriteIndex + 1) % 80;
            lastNetgraphSampleAt = GetTime();
        }

        camera.offset = {cx, cy};
        if (currentState == SINGLEPLAYER && solo.shakeTime > 0.0f)
        {
            float fade = ClampValue(solo.shakeTime / 0.18f, 0.0f, 1.0f);
            float amplitude = solo.shakeStrength * fade;
            float time = (float)GetTime();
            camera.offset.x += sinf(time * 113.0f) * amplitude;
            camera.offset.y += cosf(time * 157.0f) * amplitude * 0.72f;
        }
        else if (currentState == SESSION &&
                 connectionStatus == CONNECTION_CONNECTED &&
                 onlineJuice.shakeTime > 0.0f)
        {
            float fade = ClampValue(onlineJuice.shakeTime / 0.19f, 0.0f, 1.0f);
            float amplitude = onlineJuice.shakeStrength * fade;
            float time = (float)GetTime();
            camera.offset.x += sinf(time * 117.0f) * amplitude;
            camera.offset.y += cosf(time * 163.0f) * amplitude * 0.72f;
        }

        BeginDrawing();
        ClearBackground(Color{5, 8, 19, 255});

        if (currentState == MENU)
        {
            DrawBackdrop();

            float titleSize = GetScreenWidth() < 1200 ? 48.0f : 72.0f;
            int titleFont = (int)titleSize;
            const char *menuTitle = "NEON PROTOCOL";
            int titleX = (int)menuX - 1;
            DrawUiTextSpaced(menuTitle, titleX - 1, (int)cy - 240,
                             titleFont + 2, 3.0f, Fade(Color{111, 211, 247, 255}, 0.16f));
            DrawUiTextSpaced(menuTitle, titleX, (int)cy - 238,
                             titleFont, 3.0f, Color{111, 211, 247, 255});
            DrawUiText("PICK A MODE", (int)menuX + 3, (int)cy - 145,
                       17, Fade(WHITE, 0.76f));
            DrawUiText("A quiet sector. A hostile fleet. Your call.",
                       (int)menuX + 3, (int)cy - 112, 15, Fade(LIGHTGRAY, 0.66f));

            DrawButton(btnSingleplayer, "CAMPAIGN", Color{132, 224, 185, 255}, mousePoint);
            DrawButton(btnPlay, "MULTIPLAYER", Color{111, 211, 247, 255}, mousePoint);
            DrawButton(btnSettings, "LOADOUT", Color{187, 151, 255, 255}, mousePoint);
            DrawButton(btnAbout, "ABOUT", Color{111, 211, 247, 255}, mousePoint);
            DrawButton(btnExit, "EXIT", Color{255, 127, 145, 255}, mousePoint);

            Rectangle enterKey = {menuX + 3.0f, cy + 112.0f, 94.0f, 30.0f};
            Color enterAccent = hasSavedServerAddress
                                    ? Color{111, 211, 247, 255}
                                    : Color{187, 151, 255, 255};
            DrawRectangleRec(enterKey, Fade(enterAccent, 0.12f));
            DrawRectangleLinesEx(enterKey, 1.4f, Fade(enterAccent, 0.76f));
            int enterWidth = MeasureUiText("ENTER", 14);
            DrawUiText("ENTER", (int)(enterKey.x + (enterKey.width - enterWidth) * 0.5f),
                       (int)enterKey.y + 7, 14, WHITE);
            const char *quickConnectHint = hasSavedServerAddress
                                               ? "QUICK CONNECT TO SAVED SERVER"
                                               : "ENTER SERVER ADDRESS TO PLAY ONLINE";
            DrawUiText(quickConnectHint, (int)enterKey.x + 108, (int)enterKey.y + 8, 13,
                       hasSavedServerAddress ? Color{111, 211, 247, 255} : Fade(LIGHTGRAY, 0.72f));

            if (GetScreenWidth() > 1150 && GetScreenHeight() >= 700)
            {
                Vector2 hero = {GetScreenWidth() * 0.76f, cy - 48.0f};
                float time = (float)GetTime();
                float radarAngle = time * 0.62f;
                DrawCircleGradient((int)hero.x, (int)hero.y, 292.0f,
                                   Fade(Color{73, 192, 234, 255}, 0.10f),
                                   Fade(Color{7, 8, 19, 255}, 0.0f));
                DrawCircleLines((int)hero.x, (int)hero.y, 246.0f,
                                Fade(Color{111, 211, 247, 255}, 0.17f));
                DrawCircleLines((int)hero.x, (int)hero.y, 178.0f,
                                Fade(Color{187, 151, 255, 255}, 0.14f));

                float sweepCos = cosf(radarAngle);
                float sweepSin = sinf(radarAngle);
                Vector2 radarTip = {hero.x + sweepCos * 246.0f, hero.y + sweepSin * 246.0f};
                DrawLineEx(hero, radarTip, 6.0f, Fade(Color{78, 211, 255, 255}, 0.075f));
                DrawLineEx(hero, radarTip, 1.4f, Fade(Color{156, 231, 255, 255}, 0.44f));
                for (int i = 0; i < 5; i++)
                {
                    float markerAngle = i * (6.2831853f / 5.0f);
                    float radius = i % 2 == 0 ? 246.0f : 178.0f;
                    Vector2 marker = {hero.x + cosf(markerAngle) * radius,
                                      hero.y + sinf(markerAngle) * radius};
                    float difference = fabsf(atan2f(sinf(radarAngle - markerAngle),
                                                    cosf(radarAngle - markerAngle)));
                    float pulse = expf(-(difference * difference) / 0.018f);
                    Color markerColor = i % 2 == 0 ? Color{132, 224, 185, 255}
                                                   : Color{187, 151, 255, 255};
                    DrawCircleV(marker, 5.0f + pulse * 4.0f, Fade(markerColor, 0.12f + pulse * 0.24f));
                    DrawCircleV(marker, 2.5f + pulse * 1.8f, Fade(markerColor, 0.62f + pulse * 0.38f));
                }

                Vector2 shipDirection = {cosf(menuEffects.shipAngle), sinf(menuEffects.shipAngle)};
                Vector2 shipPosition = {hero.x - shipDirection.x * menuEffects.recoil,
                                        hero.y - shipDirection.y * menuEffects.recoil};
                float shipDegrees = menuEffects.shipAngle * (180.0f / 3.14159265f);
                DrawDrone(shipPosition, shipDegrees, playerColors[selectedColor], true, 1.25f);
                DrawMenuEffects();

                Rectangle heroInfo = {hero.x - 190.0f, hero.y + 252.0f, 380.0f, 78.0f};
                DrawRectangleRec(heroInfo, Fade(Color{5, 9, 21, 255}, 0.86f));
                DrawRectangleLinesEx(heroInfo, 1.0f, Fade(Color{111, 211, 247, 255}, 0.18f));
                const char *heroTitle = "SOLO CAMPAIGN";
                DrawUiText(heroTitle,
                           (int)(hero.x - MeasureUiText(heroTitle, 16) * 0.5f),
                           (int)heroInfo.y + 9, 16, WHITE);
                const char *heroSubtitle = "5 SECTORS  /  15 WAVES";
                DrawUiText(heroSubtitle,
                           (int)(hero.x - MeasureUiText(heroSubtitle, 13) * 0.5f),
                           (int)heroInfo.y + 34, 13, Fade(LIGHTGRAY, 0.82f));
                const char *bestText = TextFormat("PERSONAL BEST  %07d", solo.highScore);
                DrawUiText(bestText,
                           (int)(hero.x - MeasureUiText(bestText, 13) * 0.5f),
                           (int)heroInfo.y + 55, 13, Color{187, 151, 255, 255});
            }

            DrawLine((int)menuX, GetScreenHeight() - 52,
                     (int)(menuX + 330.0f), GetScreenHeight() - 52,
                     Fade(Color{111, 211, 247, 255}, 0.32f));
            DrawUiText("MADE BY NITA ADELIN-ALEXANDRU  |  UPB",
                       (int)menuX, GetScreenHeight() - 39, 12, Fade(WHITE, 0.66f));
        }
        else if (currentState == JOIN)
        {
            DrawBackdrop();

            DrawUiText(
                "JOIN SERVER",
                (int)(cx - MeasureUiText("JOIN SERVER", 48) / 2),
                (int)cy - 180,
                48,
                SKYBLUE);

            DrawUiText(
                "SERVER ADDRESS (IP OR HOSTNAME)",
                (int)(cx - MeasureUiText("SERVER ADDRESS (IP OR HOSTNAME)", 16) / 2),
                (int)cy - 90,
                16,
                LIGHTGRAY);

            DrawRectangleRec(
                ipBox,
                Fade(Color{5, 10, 22, 255}, 0.92f));

            DrawRectangleLinesEx(ipBox, 2, SKYBLUE);

            DrawUiText(
                serverAddress,
                (int)ipBox.x + 16,
                (int)ipBox.y + 17,
                24,
                WHITE);

            if (((int)GetTime() % 2) == 0)
            {
                int cursorX =
                    (int)ipBox.x + 16 + MeasureUiText(serverAddress, 20);

                DrawLine(
                    cursorX,
                    (int)ipBox.y + 14,
                    cursorX,
                    (int)ipBox.y + 45,
                    SKYBLUE);
            }

            DrawUiText(
                "Default UDP port: 8080 (include :PORT for tunnels)",
                (int)(cx - MeasureUiText("Default UDP port: 8080 (include :PORT for tunnels)", 15) / 2),
                (int)cy + 25,
                15,
                Fade(WHITE, 0.65f));

            DrawButton(btnConnect, "CONNECT", SKYBLUE, mousePoint);
            DrawButton(btnJoinBack, "BACK", RED, mousePoint, true);

            if (joinError != NULL)
            {
                DrawUiText(
                    joinError,
                    (int)(cx - MeasureUiText(joinError, 15) / 2),
                    (int)cy + 175,
                    15,
                    RED);
            }

            const char *addressHint = hasSavedServerAddress
                                          ? "SERVER SAVED ON THIS DEVICE  /  PRESS ENTER TO RECONNECT"
                                          : "ENTER THE SERVER ADDRESS ONCE  /  PRESS ENTER";
            DrawUiText(
                addressHint,
                (int)(cx - MeasureUiText(addressHint, 14) / 2),
                (int)cy + 220,
                14,
                Fade(WHITE, 0.68f));
        }
        else if (currentState == SETTINGS)
        {
            DrawBackdrop();
            const char *hangarEyebrow = "ENGINEERING BAY  /  LOADOUT";
            DrawUiText(hangarEyebrow, (int)hangarLeftX, (int)cy - 244, 13,
                       Color{111, 211, 247, 255});
            DrawUiText("SHIP HANGAR", (int)hangarLeftX - 2, (int)cy - 218, 42, WHITE);
            DrawUiText("HULL COLOR  /  NEON ENGINE TRAIL", (int)hangarLeftX, (int)cy - 184, 12,
                       Fade(LIGHTGRAY, 0.74f));

            for (int i = 0; i < NET_COLOR_COUNT; i++)
            {
                Rectangle colorBox = GetColorSwatchRectangle(i, hangarLeftCenter, cy - 150.0f);
                bool selected = selectedColor == i;
                bool hover = CheckCollisionPointRec(mousePoint, colorBox);
                if (selected || hover)
                {
                    Rectangle glow = {colorBox.x - 5.0f, colorBox.y - 5.0f,
                                      colorBox.width + 10.0f, colorBox.height + 10.0f};
                    DrawRectangleRec(glow, Fade(playerColors[i], selected ? 0.18f : 0.08f));
                }
                DrawRectangleRec(colorBox, playerColors[i]);
                DrawRectangleLinesEx(colorBox, selected ? 3.0f : (hover ? 2.0f : 1.0f),
                                     selected ? WHITE : Fade(WHITE, 0.4f));
                DrawUiText(playerColorNames[i],
                           (int)(colorBox.x + (colorBox.width - MeasureUiText(playerColorNames[i], 10)) * 0.5f),
                           (int)colorBox.y + 60, 10,
                           selected ? WHITE : Fade(LIGHTGRAY, 0.74f));
            }

            DrawUiText("WEAPON SYSTEM", (int)hangarLeftX, (int)cy - 64, 12,
                       Color{187, 151, 255, 255});
            for (int i = 0; i < NET_WEAPON_COUNT; i++)
            {
                Rectangle card = hangarWeaponCards[i];
                bool selected = selectedWeapon == i;
                bool hover = CheckCollisionPointRec(mousePoint, card);
                Color accent = weaponPresets[i].color;
                DrawRectangleRec(card, Fade(accent, selected ? 0.16f : (hover ? 0.09f : 0.035f)));
                DrawRectangleLinesEx(card, selected ? 2.0f : 1.0f,
                                     Fade(accent, selected ? 0.9f : 0.32f));
                DrawRectangle((int)card.x, (int)card.y, 3, (int)card.height, accent);
                int nameWidth = MeasureUiText(weaponPresets[i].name, 12);
                DrawUiText(weaponPresets[i].name,
                           (int)(card.x + (card.width - nameWidth) * 0.5f),
                           (int)card.y + 11, 12, selected ? WHITE : accent);
                int subWidth = MeasureUiText(weaponPresets[i].subtitle, 9);
                DrawUiText(weaponPresets[i].subtitle,
                           (int)(card.x + (card.width - subWidth) * 0.5f),
                           (int)card.y + 34, 9, Fade(LIGHTGRAY, 0.64f));
            }

            const float statsBarX = hangarLeftX + 112.0f;
            const float statsBarWidth = fmaxf(110.0f, hangarLeftWidth - 166.0f);
            for (int i = 0; i < 4; i++)
            {
                float y = cy + 34.0f + i * 38.0f;
                DrawUiText(weaponStatNames[i], (int)hangarLeftX, (int)y, 11,
                           Fade(LIGHTGRAY, 0.8f));
                DrawUiText(TextFormat("%02d", (int)displayedWeaponStats[i]),
                           (int)(statsBarX + statsBarWidth + 8.0f), (int)y, 11,
                           weaponPresets[selectedWeapon].color);
                Rectangle track = {statsBarX, y + 18.0f, statsBarWidth, 5.0f};
                DrawRectangleRec(track, Fade(WHITE, 0.1f));
                DrawRectangle((int)track.x, (int)track.y,
                              (int)(track.width * ClampValue(displayedWeaponStats[i] / 100.0f, 0.0f, 1.0f)),
                              (int)track.height, Fade(weaponPresets[selectedWeapon].color, 0.9f));
            }

            DrawHangarPreview(hangarPanel, hangarShip, hangarTarget, selectedColor);
            DrawButton(btnBack, "BACK", SKYBLUE, mousePoint, true);
            DrawUiText("ARROWS  /  COLOR + WEAPON", (int)hangarLeftX + 198,
                       (int)btnBack.y + 15, 11, Fade(WHITE, 0.58f));
        }
        else if (currentState == ABOUT)
        {
            DrawBackdrop();

            DrawRectangleRec(
                aboutPanel,
                Fade(Color{6, 10, 22, 255}, 0.97f));
            DrawRectangleLinesEx(
                aboutPanel,
                1,
                Fade(Color{140, 176, 208, 255}, 0.22f));
            DrawRectangle(
                (int)aboutPanel.x,
                (int)aboutPanel.y,
                3,
                (int)aboutPanel.height,
                Fade(SKYBLUE, 0.72f));

            const float aboutPad = 32.0f;
            const char *aboutLabel = "ABOUT";
            const char *brand = "NEON PROTOCOL";

            DrawUiText(
                aboutLabel,
                (int)aboutPanel.x + (int)aboutPad,
                (int)aboutPanel.y + 20,
                15,
                SKYBLUE);
            DrawUiText(
                brand,
                (int)(aboutPanel.x + aboutPanel.width - aboutPad -
                      MeasureUiText(brand, 14)),
                (int)aboutPanel.y + 21,
                14,
                Fade(WHITE, 0.54f));
            DrawLine(
                (int)aboutPanel.x + (int)aboutPad,
                (int)aboutPanel.y + 52,
                (int)(aboutPanel.x + aboutPanel.width - aboutPad),
                (int)aboutPanel.y + 52,
                Fade(WHITE, 0.13f));

            const char *creatorLabel = "CREATED BY";
            DrawUiText(
                creatorLabel,
                (int)(aboutPanel.x +
                      (aboutPanel.width - MeasureUiText(creatorLabel, 14)) / 2.0f),
                (int)aboutPanel.y + 72,
                14,
                PURPLE);

            const char *creatorName = "Nita Adelin-Alexandru";
            DrawUiText(
                creatorName,
                (int)(aboutPanel.x +
                      (aboutPanel.width - MeasureUiText(creatorName, 38)) / 2.0f),
                (int)aboutPanel.y + 94,
                38,
                WHITE);

            const char *creatorSchool =
                "@ UPB  /  Automatica si Calculatoare";
            DrawUiText(
                creatorSchool,
                (int)(aboutPanel.x +
                      (aboutPanel.width - MeasureUiText(creatorSchool, 18)) / 2.0f),
                (int)aboutPanel.y + 141,
                18,
                Fade(LIGHTGRAY, 0.92f));

            const char *aboutDescription =
                "An arcade space-combat game with a solo campaign and fast matches with friends.";
            DrawUiText(
                aboutDescription,
                (int)(aboutPanel.x +
                      (aboutPanel.width - MeasureUiText(aboutDescription, 16)) / 2.0f),
                (int)aboutPanel.y + 180,
                16,
                Fade(WHITE, 0.72f));

            DrawUiText(
                "BUILT WITH",
                (int)aboutPanel.x + (int)aboutPad,
                (int)aboutPanel.y + 222,
                14,
                SKYBLUE);
            DrawLine(
                (int)aboutPanel.x + 132,
                (int)aboutPanel.y + 232,
                (int)(aboutPanel.x + aboutPanel.width - aboutPad),
                (int)aboutPanel.y + 232,
                Fade(SKYBLUE, 0.28f));

            const char *techLabels[] = {
                "CLIENT", "SERVER", "NETWORK",
                "SOCKETS", "TOOLCHAIN", "BUILD"};
            const char *techValues[] = {
                "C++17 / raylib 5.0",
                "C11 / Linux",
                "UDP / IPv4",
                "Winsock2 / POSIX",
                "GCC / MinGW-w64",
                "Makefile / Bash"};

            float contentWidth = aboutPanel.width - aboutPad * 2.0f;
            float techColumnWidth = contentWidth / 3.0f;
            float techTop = aboutPanel.y + 252.0f;

            for (int column = 1; column < 3; column++)
            {
                float dividerX =
                    aboutPanel.x + aboutPad + techColumnWidth * column - 12.0f;
                DrawLine(
                    (int)dividerX,
                    (int)techTop - 2,
                    (int)dividerX,
                    (int)techTop + 112,
                    Fade(WHITE, 0.1f));
            }

            for (int i = 0; i < 6; i++)
            {
                int column = i % 3;
                int row = i / 3;
                int cellX = (int)(aboutPanel.x + aboutPad +
                                  techColumnWidth * column);
                int cellY = (int)techTop + row * 60;

                DrawUiText(
                    techLabels[i],
                    cellX,
                    cellY,
                    11,
                    Fade(SKYBLUE, 0.84f));
                DrawUiText(
                    techValues[i],
                    cellX,
                    cellY + 18,
                    17,
                    WHITE);
            }

            DrawLine(
                (int)aboutPanel.x + (int)aboutPad,
                (int)aboutPanel.y + 380,
                (int)(aboutPanel.x + aboutPanel.width - aboutPad),
                (int)aboutPanel.y + 380,
                Fade(WHITE, 0.13f));

            const char *matchFacts[] = {
                "5 SOLO SECTORS",
                "15 CAMPAIGN WAVES",
                "ONLINE MATCHES"};
            float factWidth = contentWidth / 3.0f;
            for (int i = 0; i < 3; i++)
            {
                int textWidth = MeasureUiText(matchFacts[i], 14);
                DrawUiText(
                    matchFacts[i],
                    (int)(aboutPanel.x + aboutPad + factWidth * i +
                          (factWidth - textWidth) / 2.0f),
                    (int)aboutPanel.y + 397,
                    14,
                    i == 1 ? PURPLE : Fade(WHITE, 0.82f));

                if (i < 2)
                {
                    int dividerX =
                        (int)(aboutPanel.x + aboutPad + factWidth * (i + 1));
                    DrawLine(
                        dividerX,
                        (int)aboutPanel.y + 394,
                        dividerX,
                        (int)aboutPanel.y + 416,
                        Fade(WHITE, 0.12f));
                }
            }

            float telemetryY = aboutPanel.y + (aboutPanel.height < 550.0f ? 418.0f : 440.0f);
            int serverOnline = network_server_online();
            const char *serverStatus = !hasSavedServerAddress
                                           ? "SERVER STATUS  /  NO SAVED ADDRESS"
                                           : !aboutProbeAvailable
                                                 ? "SERVER STATUS  /  ADDRESS UNAVAILABLE"
                                                 : serverOnline < 0
                                                 ? "SERVER STATUS  /  CHECKING..."
                                                 : serverOnline == 1
                                                       ? TextFormat("SERVER STATUS  /  ONLINE  /  %d ms", network_ping_ms())
                                                       : "SERVER STATUS  /  OFFLINE";
            Color serverStatusColor = serverOnline == 1 ? Color{143, 231, 196, 255}
                                      : serverOnline == 0 ? Color{255, 142, 133, 255}
                                                          : Fade(LIGHTGRAY, 0.78f);
            DrawUiText(serverStatus,
                       (int)(aboutPanel.x + (aboutPanel.width - MeasureUiText(serverStatus, 12)) * 0.5f),
                       (int)telemetryY, 12, serverStatusColor);
            const char *technicalLine = TextFormat("FPS %d   /   SERVER TICKRATE 60 HZ   /   CUSTOM UDP",
                                                   GetFPS());
            DrawUiText(technicalLine,
                       (int)(aboutPanel.x + (aboutPanel.width - MeasureUiText(technicalLine, 11)) * 0.5f),
                       (int)telemetryY + 21, 11, Fade(WHITE, 0.68f));
            if (aboutPanel.height >= 550.0f)
            {
                unsigned int pingsSent = 0;
                unsigned int pingsReceived = 0;
                network_get_ping_counters(&pingsSent, &pingsReceived);
                char lossText[12];
                if (pingsSent == 0)
                    snprintf(lossText, sizeof(lossText), "--");
                else
                    snprintf(lossText, sizeof(lossText), "%d%%",
                             network_packet_loss_percent());

                const char *networkTelemetry = TextFormat(
                    "PING LOSS %s    /    UDP IN %u B/S    /    OUT %u B/S",
                    lossText,
                    network_bytes_in_per_second(),
                    network_bytes_out_per_second());
                DrawUiText(networkTelemetry,
                           (int)(aboutPanel.x +
                                 (aboutPanel.width - MeasureUiText(networkTelemetry, 10)) * 0.5f),
                           (int)telemetryY + 42, 10, Fade(LIGHTGRAY, 0.72f));
            }
            DrawButton(btnAboutBack, "BACK", SKYBLUE, mousePoint, true);

            Rectangle escKey = {
                aboutPanel.x + aboutPanel.width - 178.0f,
                aboutPanel.y + aboutPanel.height - 43.0f,
                48.0f,
                25.0f};
            DrawRectangleRec(escKey, Fade(Color{111, 211, 247, 255}, 0.08f));
            DrawRectangleLinesEx(escKey, 1.0f, Fade(Color{111, 211, 247, 255}, 0.4f));
            DrawUiText("ESC", (int)escKey.x + 8, (int)escKey.y + 5, 12, WHITE);
            DrawUiText("TO RETURN", (int)escKey.x + 57, (int)escKey.y + 6, 11,
                       Fade(LIGHTGRAY, 0.76f));
        }
        else if (currentState == SINGLEPLAYER)
        {
            DrawSoloWorld(
                camera,
                solo,
                selectedColor,
                worldMouse,
                mousePoint,
                btnSoloRestart,
                btnSoloMenu);
        }
        else if (currentState == SESSION &&
                 connectionStatus == CONNECTION_CONNECTING)
        {
            DrawBackdrop();

            DrawUiText(
                "CONNECTING...",
                (int)(cx - MeasureUiText("CONNECTING...", 40) / 2),
                (int)cy - 45,
                40,
                SKYBLUE);

            DrawUiText(
                TextFormat("SERVER %s", serverAddress),
                (int)(cx - MeasureUiText(
                               TextFormat("SERVER %s", serverAddress), 18) /
                               2),
                (int)cy + 15,
                18,
                LIGHTGRAY);
        }
        else if (currentState == SESSION &&
                 connectionStatus == CONNECTION_RECONNECTING)
        {
            DrawBackdrop();

            DrawUiText(
                "RECONNECTING...",
                (int)(cx - MeasureUiText("RECONNECTING...", 38) / 2),
                (int)cy - 70,
                38,
                YELLOW);

            DrawUiText(
                "Rejoining the same server session automatically",
                (int)(cx - MeasureUiText("Rejoining the same server session automatically", 17) / 2),
                (int)cy - 18,
                17,
                LIGHTGRAY);

            DrawUiText(
                TextFormat("SERVER  %s", serverAddress),
                (int)(cx - MeasureUiText(TextFormat("SERVER  %s", serverAddress), 15) / 2),
                (int)cy + 24,
                15,
                Fade(WHITE, 0.65f));

            DrawUiText(
                "Press ESC to leave",
                (int)(cx - MeasureUiText("Press ESC to leave", 14) / 2),
                (int)cy + 62,
                14,
                Fade(WHITE, 0.5f));
        }
        else if (currentState == SESSION &&
                 connectionStatus == CONNECTION_FAILED)
        {
            DrawBackdrop();

            DrawUiText(
                "CONNECTION FAILED",
                (int)(cx - MeasureUiText("CONNECTION FAILED", 38) / 2),
                (int)cy - 100,
                38,
                RED);

            DrawUiText(
                "Check the address, UDP port, server process and firewall.",
                (int)(cx - MeasureUiText(
                               "Check the address, UDP port, server process and firewall.", 16) /
                               2),
                (int)cy - 40,
                16,
                LIGHTGRAY);

            DrawButton(btnRetry, "RETRY", SKYBLUE, mousePoint);
            DrawButton(btnChangeServer, "CHANGE SERVER", PURPLE, mousePoint);
        }
        else if (currentState == SESSION &&
                 connectionStatus == CONNECTION_CONNECTED &&
                 matchPhase == NET_PHASE_LOBBY)
        {
            DrawLobby(
                players,
                myId,
                localReady,
                mousePoint,
                cx,
                cy,
                btnReady,
                btnLeave);
        }
        else if (currentState == SESSION &&
                 connectionStatus == CONNECTION_CONNECTED)
        {
            DrawWorld(
                camera,
                players,
                bullets,
                myId,
                targetX,
                targetY,
                myHp,
                (float)angleDegrees,
                selectedColor,
                worldMouse,
                matchPhase == NET_PHASE_PLAYING,
                onlineJuice);

            if (matchPhase == NET_PHASE_COUNTDOWN)
            {
                DrawRectangle(
                    0, 0, GetScreenWidth(), GetScreenHeight(),
                    Fade(BLACK, 0.48f));

                DrawUiText(
                    "ROUND STARTING",
                    (int)(cx - MeasureUiText("ROUND STARTING", 34) / 2),
                    (int)cy - 65,
                    34,
                    SKYBLUE);

                DrawUiText(
                    TextFormat("%d", secondsLeft),
                    (int)(cx - MeasureUiText(
                                   TextFormat("%d", secondsLeft), 80) /
                                   2),
                    (int)cy - 5,
                    80,
                    WHITE);

                DrawUiText(
                    "Get ready",
                    (int)(cx - MeasureUiText("Get ready", 20) / 2),
                    (int)cy + 90,
                    20,
                    LIGHTGRAY);
            }
            else if (matchPhase == NET_PHASE_PLAYING)
            {
                uint16_t kills = 0;
                uint16_t deaths = 0;

                if (myId >= 0 && myId < MAX_PLAYERS &&
                    players[myId].active)
                {
                    kills = players[myId].kills;
                    deaths = players[myId].deaths;
                }

                DrawRectangle(
                    16, 16, 270, 132,
                    Fade(Color{5, 10, 22, 255}, 0.9f));

                DrawRectangleLinesEx(
                    {16, 16, 270, 132},
                    2,
                    Fade(SKYBLUE, 0.75f));

                DrawUiText("NEON PROTOCOL", 30, 27, 19, SKYBLUE);

                DrawUiText(
                    TextFormat("TIME  %02d:%02d",
                               secondsLeft / 60,
                               secondsLeft % 60),
                    30,
                    55,
                    17,
                    WHITE);

                DrawUiText(
                    TextFormat("KILLS  %u     DEATHS  %u",
                               (unsigned)kills,
                               (unsigned)deaths),
                    30,
                    82,
                    15,
                    LIGHTGRAY);

                DrawUiText(
                    TextFormat("HULL  %u%%", (unsigned)myHp),
                    30,
                    106,
                    13,
                    LIGHTGRAY);

                DrawHealthBar(30, 128, 230, myHp, LIME);

                DrawScoreboard(players, myId, GetScreenWidth());

                DrawMinimap(
                    players,
                    myId,
                    GetScreenWidth(),
                    GetScreenHeight());
                if (showNetgraph)
                    DrawNetgraph(netgraphPingHistory, netgraphWriteIndex);


                DrawUiText(
                    "WASD MOVE  |  MOUSE AIM  |  HOLD LMB FIRE  |  ESC LEAVE",
                    18,
                    GetScreenHeight() - 28,
                    14,
                    Fade(WHITE, 0.72f));

                DrawFPS(18, 158);
            }
            else if (matchPhase == NET_PHASE_RESULTS)
            {
                DrawRectangle(
                    0, 0, GetScreenWidth(), GetScreenHeight(),
                    Fade(BLACK, 0.68f));

                Rectangle panel = {
                    cx - 300,
                    cy - 240,
                    600,
                    480};

                DrawRectangleRec(
                    panel,
                    Fade(Color{5, 10, 22, 255}, 0.97f));

                DrawRectangleLinesEx(
                    panel,
                    2,
                    Fade(PURPLE, 0.9f));

                if (winnerId == NET_NO_WINNER)
                {
                    DrawUiText(
                        "DRAW",
                        (int)(cx - MeasureUiText("DRAW", 42) / 2),
                        (int)cy - 208,
                        42,
                        YELLOW);
                }
                else
                {
                    const char *winnerText =
                        TextFormat("PILOT %02d WINS", winnerId);

                    DrawUiText(
                        winnerText,
                        (int)(cx - MeasureUiText(winnerText, 36) / 2),
                        (int)cy - 208,
                        36,
                        SKYBLUE);
                }

                DrawUiText(
                    "FINAL SCORE",
                    (int)(cx - MeasureUiText("FINAL SCORE", 18) / 2),
                    (int)cy - 158,
                    18,
                    LIGHTGRAY);

                int row = 0;
                int readyPlayerCount = 0;
                int resultPlayerCount = 0;

                for (int i = 0; i < MAX_PLAYERS; i++)
                {
                    if (!players[i].active)
                        continue;

                    resultPlayerCount++;
                    if (players[i].ready)
                        readyPlayerCount++;

                    Color color = GetPlayerColor(players[i].color);
                    int rowY = (int)cy - 128 + row * 21;

                    DrawCircleV({cx - 230, (float)rowY + 8}, 6, color);

                    DrawUiText(
                        TextFormat("PILOT %02d", i),
                        (int)cx - 212,
                        rowY,
                        16,
                        WHITE);

                    DrawUiText(
                        TextFormat("%u KILLS   %u DEATHS",
                                   (unsigned)players[i].kills,
                                   (unsigned)players[i].deaths),
                        (int)cx + 20,
                        rowY,
                        16,
                        color);

                    row++;
                }

                DrawButton(
                    btnResultsRematch,
                    localReady ? "CANCEL READY" : "PLAY AGAIN",
                    localReady ? LIME : SKYBLUE,
                    mousePoint);
                DrawButton(
                    btnResultsMenu,
                    "BACK TO MENU",
                    PURPLE,
                    mousePoint);

                const char *rematchHint =
                    resultPlayerCount < NET_MIN_PLAYERS
                        ? TextFormat("NEED AT LEAST %d PILOTS FOR A REMATCH",
                                     NET_MIN_PLAYERS)
                        : TextFormat("%d / %d PILOTS READY",
                                     readyPlayerCount, resultPlayerCount);
                DrawUiText(
                    rematchHint,
                    (int)(cx - MeasureUiText(rematchHint, 13) / 2),
                    (int)cy + 162,
                    13,
                    Fade(WHITE, 0.8f));

                const char *returnText =
                    TextFormat("LOBBY IN %ds  |  EVERYONE MUST READY", secondsLeft);
                DrawUiText(
                    returnText,
                    (int)(cx - MeasureUiText(returnText, 13) / 2),
                    (int)cy + 187,
                    13,
                    LIGHTGRAY);
            }
        }

        EndDrawing();
    }

    network_probe_close();
    network_close();
    UnloadUiFont();
    CloseWindow();
    return 0;
}