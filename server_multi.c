#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

#define MAX_NAME 32
#define MAP_SIZE 3

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

Room map[MAP_SIZE][MAP_SIZE]; 
Player *player_list_head = NULL;

// 用來新增物品到清單的輔助函式 
void add_item(Item **head, const char *name) {
    Item *new_item = (Item *)malloc(sizeof(Item));
    strcpy(new_item->name, name);
    new_item->next = *head;
    *head = new_item;
}

// 初始化地圖函式
void init_map() {
    // 先把地圖清空，確保安全
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            map[i][j].ground_items = NULL;
        }
    }

    // 讀取 map.txt 檔案
    FILE *fp = fopen("map.txt", "r");
    if (fp == NULL) {
        printf("Error: Cannot open map.txt\n");
        // 如果讀不到檔，我們先手動塞一點資料測試，避免程式掛掉
        add_item(&map[0][0].ground_items, "Apple");
        add_item(&map[1][0].ground_items, "Banana");
        return;
    }

    int x, y;
    char item_name[MAX_NAME];
    // 讀取格式：X座標 Y座標 物品名稱
    while (fscanf(fp, "%d %d %s", &x, &y, item_name) != EOF) {
        // 防止 map.txt 寫錯座標導致當機
        if (x >= 0 && x < MAP_SIZE && y >= 0 && y < MAP_SIZE) {
            add_item(&map[x][y].ground_items, item_name);
        }
    }
    fclose(fp);
    printf("Map initialized successfully!\n");
}

// 新增玩家到遊戲世界
void create_player(int fd) {
    Player *p = (Player *)malloc(sizeof(Player));
    p->socket_fd = fd;
    strcpy(p->name, "Unknown"); // 暫時叫 Unknown，之後寫 login 再改
    p->x = 0; // 預設出生在 (0,0)
    p->y = 0;
    p->backpack = NULL;
    
    // 把新玩家加到全域清單的最前面 (Linked List 插入)
    p->next = player_list_head;
    player_list_head = p;
    
    printf("Created player structure for socket %d at (0,0)\n", fd);
}

// 用 Socket ID 找到對應的玩家
Player *find_player_by_fd(int fd) {
    Player *current = player_list_head;
    while (current != NULL) {
        if (current->socket_fd == fd) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int main() {
    init_map(); // 初始化地圖

    int master_socket, addrlen, new_socket, client_socket[MAX_CLIENTS], max_sd, sd, activity, valread;
    struct sockaddr_in address;
    char buffer[BUFFER_SIZE];
    
    // fd_set 是用來儲存所有我們要監控的檔案描述符 (File Descriptors) 的集合
    fd_set readfds;

    // 初始化 client_socket 陣列，0 代表該位置是空的
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_socket[i] = 0;
    }

    // 1. 建立 Master Socket (這是主要的總機，用來接受新連線)
    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 設定 socket 選項，允許重用地址 (避免 Ctrl+C 後要等很久才能重開)
    int opt = 1;
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // 2. 綁定地址與 Port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 接受任何 IP
    address.sin_port = htons(PORT);

    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 3. 開始監聽
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Game Server started on port %d... Waiting for connections...\n", PORT);
    addrlen = sizeof(address);

    // 4. 進入無窮迴圈
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(master_socket, &readfds);
        max_sd = master_socket;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0)) {
            printf("select error\n");
        }

        // --- 處理新連線 ---
        if (FD_ISSET(master_socket, &readfds)) {
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            printf("New connection: socket fd is %d, ip is %s, port is %d\n", 
                   new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
            
            // 生出玩家！
            create_player(new_socket);

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    break;
                }
            }
        }

        // --- 處理舊玩家的訊息 ---
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];

            if (FD_ISSET(sd, &readfds)) {
                // 【修正 1】讀取前，一定要把 buffer 洗乾淨！避免印出上一個人的訊息
                memset(buffer, 0, BUFFER_SIZE);

                valread = read(sd, buffer, BUFFER_SIZE);

                // 如果讀到 0 (斷線)
                if (valread == 0) {
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected, ip %s, port %d\n", 
                           inet_ntoa(address.sin_addr), ntohs(address.sin_port));
                    close(sd);
                    client_socket[i] = 0;
                }
                // 【修正 2】如果 valread < 0 代表讀取錯誤，也要忽略
                else if (valread < 0) {
                    perror("read error");
                }
                // 讀到正常的資料
                else {
                    // 1. 去除換行符號
                    buffer[strcspn(buffer, "\n")] = 0;
                    buffer[strcspn(buffer, "\r")] = 0;

                    if (strlen(buffer) == 0) continue;

                    printf("Client %d says: %s\n", sd, buffer);

                    // 2. 取得當前說話的玩家
                    Player *current_player = find_player_by_fd(sd);
                    if (current_player == NULL) {
                        printf("Error: Player not found for socket %d\n", sd); // 預防錯誤處理
                        continue;
                    }

                    // --- 指令處理開始 ---

                    // 1. 處理 LOOK (保留你原本寫得很棒的物品顯示邏輯)
                    if (strncmp(buffer, "look", 4) == 0) { // 只要前四個字是 look 就行
                        // 準備要回傳的字串
                        char response[BUFFER_SIZE];
                        memset(response, 0, BUFFER_SIZE); // 清空

                        // 找到玩家所在的房間
                        Room *curr_room = &map[current_player->x][current_player->y];

                        sprintf(response, "You are at (%d, %d).\nYou see:\n", current_player->x, current_player->y);

                        // 檢查地上有沒有東西
                        Item *item = curr_room->ground_items;
                        if (item == NULL) {
                            strcat(response, "  Nothing.\n");
                        } else {
                            while (item != NULL) {
                                strcat(response, "  - ");
                                strcat(response, item->name);
                                strcat(response, "\n");
                                item = item->next;
                            }
                        }
                        // 寄回去給玩家
                        send(sd, response, strlen(response), 0);
                    }
                    // 2. 處理 NORTH (新增的移動邏輯)
                    else if (strncmp(buffer, "north", 5) == 0) {
                        if (current_player->y > 0) {
                            current_player->y--;
                            char *msg = "You moved North.\n";
                            send(sd, msg, strlen(msg), 0);
                        } else {
                            char *msg = "You hit a wall!\n";
                            send(sd, msg, strlen(msg), 0);
                        }
                    }
                    // 3. 處理 SOUTH (新增的移動邏輯)
                    else if (strncmp(buffer, "south", 5) == 0) {
                        if (current_player->y < MAP_SIZE - 1) {
                            current_player->y++;
                            char *msg = "You moved South.\n";
                            send(sd, msg, strlen(msg), 0);
                        } else {
                            char *msg = "You hit a wall!\n";
                            send(sd, msg, strlen(msg), 0);
                        }
                    }
                    // 4. 處理 EAST (新增的移動邏輯)
                    else if (strncmp(buffer, "east", 4) == 0) {
                        if (current_player->x < MAP_SIZE - 1) {
                            current_player->x++;
                            char *msg = "You moved East.\n";
                            send(sd, msg, strlen(msg), 0);
                        } else {
                            char *msg = "You hit a wall!\n";
                            send(sd, msg, strlen(msg), 0);
                        }
                    }
                    // 5. 處理 WEST (新增的移動邏輯)
                    else if (strncmp(buffer, "west", 4) == 0) {
                        if (current_player->x > 0) {
                            current_player->x--;
                            char *msg = "You moved West.\n";
                            send(sd, msg, strlen(msg), 0);
                        } else {
                            char *msg = "You hit a wall!\n";
                            send(sd, msg, strlen(msg), 0);
                        }
                    }
                    // 6. 處理 INVENTORY (查看背包)
                    else if (strncmp(buffer, "inventory", 9) == 0 || strncmp(buffer, "i", 1) == 0) {
                        char response[BUFFER_SIZE];
                        memset(response, 0, BUFFER_SIZE);
                        
                        sprintf(response, "Your Backpack:\n");
                        
                        Item *item = current_player->backpack; // 注意：這裡改用 backpack
                        if (item == NULL) {
                            strcat(response, "  (Empty)\n");
                        } else {
                            while (item != NULL) {
                                strcat(response, "  - ");
                                strcat(response, item->name);
                                strcat(response, "\n");
                                item = item->next;
                            }
                        }
                        send(sd, response, strlen(response), 0);
                    }
                    // 7. 處理 TAKE (撿起物品)
                    else if (strncmp(buffer, "take", 4) == 0) {
                        // 1. 解析指令，找出玩家想撿什麼 (例如: take apple)
                        char target_name[50];
                        // 跳過 "take " 這 5 個字元，讀取後面的字
                        if (sscanf(buffer + 5, "%s", target_name) == 1) {
                            
                            Room *curr_room = &map[current_player->x][current_player->y];
                            Item *curr = curr_room->ground_items;
                            Item *prev = NULL;
                            int found = 0;

                            // 2. 在房間地上搜尋這個物品
                            while (curr != NULL) {
                                // 這裡用 strcasecmp 可以忽略大小寫 (Apple vs apple 都可以)，如果編譯不過改回 strcmp
                                if (strcasecmp(curr->name, target_name) == 0) { 
                                    found = 1;
                                    break; // 找到了！curr 現在指向該物品
                                }
                                prev = curr;
                                curr = curr->next;
                            }

                            if (found) {
                                // 3. 從房間移除該物品 (Linked List 移除節點)
                                if (prev == NULL) {
                                    // 如果是第一個物品
                                    curr_room->ground_items = curr->next;
                                } else {
                                    // 如果是中間或後面的物品
                                    prev->next = curr->next;
                                }

                                // 4. 放入玩家背包 (Linked List 插入頭部)
                                curr->next = current_player->backpack; // 注意：這裡改用 backpack
                                current_player->backpack = curr;       // 注意：這裡改用 backpack

                                char msg[100];
                                sprintf(msg, "You took the %s.\n", curr->name);
                                send(sd, msg, strlen(msg), 0);
                            } else {
                                char *msg = "You don't see that here.\n";
                                send(sd, msg, strlen(msg), 0);
                            }
                        } else {
                            char *msg = "Take what?\n";
                            send(sd, msg, strlen(msg), 0);
                        }
                    }
                    // 8. 未知指令
                    else {
                        char *msg = "Unknown command. Try 'look', 'north', 'south', 'east', 'west'.\n";
                        send(sd, msg, strlen(msg), 0);
                    }
                }
            }
        }
    }
    return 0;
}