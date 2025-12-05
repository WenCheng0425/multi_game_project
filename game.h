#ifndef GAME_H
#define GAME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- 共用常數 ---
#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_NAME 32
#define MAP_SIZE 3

// --- 共用結構體 (Structs) ---
// 注意順序：因為 Player 裡面有 Item，所以 Item 要放前面

typedef struct Item {
    char name[MAX_NAME];   
    struct Item *next;     
} Item;

typedef struct Player {
    int socket_fd;         
    char name[MAX_NAME];   
    int x, y;              
    Item *backpack;        
    struct Player *next;   
} Player;

typedef struct Room {
    Item *ground_items;    
} Room;

// 注意：如果你之後有 struct Player 或 struct GameState
// 也要全部剪下貼過來這裡，這樣 Server 跟 Client 才會長得一樣

#endif