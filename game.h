#ifndef GAME_H
#define GAME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Common Constants / 共用常數 ---
#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_NAME 32
#define MAP_SIZE 3

// --- Multicast Settings (Server Discovery) / Multicast 設定 ---
#define MCAST_GRP "239.0.0.1"   // Multicast Group IP / 多播組 IP (224.0.0.0 ~ 239.255.255.255)
#define MCAST_PORT 8888         // Multicast Port / 多播專用 Port (separated from game port 8080)
#define DISCOVERY_MSG "MUD_WHO"   // Client Discovery Request / Client 發出的尋人啟事
#define DISCOVERY_RESP "MUD_HERE" // Server Response / Server 回應的訊號

/// --- Change Encryption Key / 更換加密金鑰  ---
// Use 'K' (ASCII 75) to avoid conflict with Space (32) / 使用 'K' 避免跟空白鍵衝突
#define XOR_KEY 'K'

// --- Shared Structures / 共用結構 ---
// Linked List Data Structures to Dynamically Manage Variable Number of Players and Items
// 鏈結串列資料結構：用於動態管理不確定數量的玩家與物品。
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

// Shared Encryption Function / 共用的加密函式
// Ensure two parameters: string pointer, length / 確保兩個參數：字串指標、長度
static void xor_process(char *msg, int len) {
    if (msg == NULL) return;
    for (int i = 0; i < len; i++) {
        msg[i] ^= XOR_KEY;
    }
}
#endif