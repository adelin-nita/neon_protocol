# Neon Protocol

A small server-authoritative multiplayer arena built as a personal software engineering project.

**Created by Nita Adelin-Alexandru**  
**@ UPB - Automatica si Calculatoare**


## Current features

- A Linux dedicated server and Windows graphical client.
- A local single-player campaign with 5 sectors and 15 waves, three visually distinct enemy ships, Guardian boss encounters, lives, and a saved personal high score.
- Campaign play is offline and does not require starting or connecting to the server.
- A shared lobby for 2-10 pilots, with a ready system and a 3-second countdown.
- Server-authoritative movement, shooting, hit detection, health, kills, deaths, respawns, round timer, winner and results.
- 60 Hz server simulation and UDP state snapshots.
- Client connection feedback, retry, and a 15-second server-side rejoin window for a temporarily lost UDP connection.
- Results screen with a server-checked rematch vote and a 10-second return to the lobby.
- A five-color neon hull and engine trail palette shared by solo and online play.
- Three server-authoritative multiplayer weapons: Twin Laser, Plasma Cannon and Spread Shot.
- An interactive ship hangar with animated weapon stats and a live firing range preview.
- Main-menu ship aiming, click-to-fire projectiles, muzzle sparks and an animated radar sweep.
- Solo and online combat feedback with screen shake, hit flashes, impact particles and a shockwave-reactive arena grid.
- ABOUT screen server ping and live network details; F3 opens the in-match ping graph and throughput panel.
- Address entry supports IPv4 or a hostname, with an optional port: 198.51.100.20:8080 or game.example.net:41272.

## Technologies

- **Client:** C++17, raylib 5.0, Winsock2.
- **Server:** C11, Linux/POSIX sockets.
- **Networking:** UDP over IPv4; fixed-size binary packets in network byte order.
- **Build tools:** GCC, MinGW-w64, Make and Bash.

## Build

Run from the project directory in Ubuntu/WSL:

~~~bash
./build.sh
~~~

This builds server_linux and client_windows.exe. The client build uses the raylib 5.0 files in raylib-5.0_win64_mingw-w64/.

Alternatively:

~~~bash
make server
make client
~~~

Run the automated Linux server integration test with:

~~~bash
make test
~~~

## Run a local session

Start the server in WSL:

~~~bash
./server_linux
~~~

The server listens on UDP port 8080. A different port can be selected for local development:

~~~bash
./server_linux --port 9000
~~~

Launch two copies of client_windows.exe, choose **PLAY ONLINE**, and enter the server address. Include a port only when it differs from 8080.

For friends outside your home network, the server needs a public route. A zero-cost option is router UDP port forwarding if your ISP connection has a public IPv4 address. With WSL2, mirrored networking may be needed on supported Windows 11 versions. If the connection is behind CGNAT, a free UDP tunnel can be used; only the host runs its agent, while friends use the game client.

## Controls

- W, A, S, D: move
- Left Shift: boost in the single-player campaign
- P: pause or resume the single-player campaign
- Single-player score record is saved in the NeonProtocol settings folder on the computer.
- Mouse: aim
- Hold left mouse button: fire
- Enter or Space: ready in lobby or request/cancel a rematch on the results screen
- Escape: leave the session
- F3: show or hide the multiplayer network graph
- F11: toggle fullscreen
- HANGAR: Left/Right select hull color; Up/Down select weapon

## Network protocol overview

Client input is exactly three 16-bit values:

1. Input flags.
2. Aim angle in signed degrees.
3. Packed loadout value: color index multiplied by 3, plus weapon index.

The server rejects malformed packet sizes and invalid input fields, limits accepted input to 120 packets per second per player, neutralizes stale controls, and keeps a short rejoin grace period. A separate four-byte UDP ping request is answered directly without allocating a lobby player slot.

A server snapshot contains four header values (player slot, match phase, remaining seconds and winner slot), followed by 10 player records and 20 bullet records. Each player record has 11 unsigned 16-bit words: active, x, y, angle, color, health, kills, deaths, ready, connected and weapon. Each bullet record has four words: active, x, y and weapon. The full snapshot is 388 bytes. All values use network byte order. The client and server must be rebuilt together because this protocol version changes the snapshot layout.

The server owns movement, projectile simulation, collision, health, score, match phase and winner selection. The client sends input and renders received state.

## Current scope and next engineering milestones

This build has one shared lobby rather than multiple rooms or invite codes. It does not yet have accounts, persistent profiles, chat, replay storage, or matchmaking queues. Rejoining works for up to 15 seconds when the same client process and UDP socket return. A full client restart does not yet preserve player identity.

The next multiplayer milestones are room/invite codes, a reconnect token, stronger session admission controls, remote-player interpolation, and more comprehensive automated tests. The server currently has no account or room-password system, so treat an internet-facing playtest as an open server and share its address only with the test group.