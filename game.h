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

// --- Multicast 設定 (Server Discovery) ---
#define MCAST_GRP "239.0.0.1"  // 多播組 IP (224.0.0.0 ~ 239.255.255.255)
#define MCAST_PORT 8888        // 多播專用 Port (跟遊戲的 8080 分開)
#define DISCOVERY_MSG "MUD_WHO" // Client 發出的尋人啟事
#define DISCOVERY_RESP "MUD_HERE" // Server 回應的訊號

// --- ★★★ 關鍵修改：更換加密金鑰 ★★★ ---
// 使用 'K' (ASCII 75) 避免跟空白鍵 (32) 發生衝突
#define XOR_KEY 'K'

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

// 共用的加密函式
// 確保兩個參數：字串指標、長度
static void xor_process(char *msg, int len) {
    if (msg == NULL) return;
    for (int i = 0; i < len; i++) {
        msg[i] ^= XOR_KEY;
    }
}
#endif