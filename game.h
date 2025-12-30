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

// ==========================================
// ★★★ 加密/解密函式 (放在這裡大家都能用) ★★★
// ==========================================
// 注意：一定要加 static，避免重複定義錯誤
static void xor_process(char *data, int len) {
    int key = 66; // 雙方約定好的密鑰 (改這裡，兩邊都會一起變，超方便)
    
    for (int i = 0; i < len; i++) {
        // 排除換行符號和結尾符號，只加密內容
        if (data[i] != '\0' && data[i] != '\n' && data[i] != '\r') {
            data[i] = data[i] ^ key;
        }
    }
}
#endif