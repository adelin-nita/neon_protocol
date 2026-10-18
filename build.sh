#!/bin/bash

echo "--- Curatare fisiere vechi ---"
rm -f server_linux client_windows.exe

echo "--- Construire Server (Linux) ---"
gcc -std=c11 -Wall -Wextra -O2 server/server.c -o server_linux -lm
echo "Server complet."

echo "--- Construire Client (Windows) ---"
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -c client/net_client.c -o client/net_client.o
x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra client/main_client.cpp client/net_client.o -I./raylib-5.0_win64_mingw-w64/include -L./raylib-5.0_win64_mingw-w64/lib -o client_windows.exe -lraylib -lgdi32 -lwinmm -lws2_32 -static
rm -f client/net_client.o
echo "Client complet."