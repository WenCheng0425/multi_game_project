/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : server_multi.c
  * @brief          : Multi-player MUD Game Server (Refactored)
  * @author         : Wencheng
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "game.h"       // <--- 新增這行！(它已經包含 stdio, stdlib, string, 以及 PORT 設定)
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <errno.h>

/* Private define ------------------------------------------------------------*/
// #define DEFAULT_IP "127.0.0.1"  // Server 其實不需要設 IP (它預設是聽所有來源 INADDR_ANY)，這行沒用到可刪
#define MAX_CLIENTS 10

/* Private typedef -----------------------------------------------------------*/
// 都已經搬到 game.h 了
/* Private variables ---------------------------------------------------------*/
Room map[MAP_SIZE][MAP_SIZE]; 
Player *player_list_head = NULL;

/* Private function prototypes -----------------------------------------------*/
void add_item(Item **head, const char *name);
void init_map(void);
void create_player(int fd);
Player *find_player_by_fd(int fd);
int init_server_socket(int Port);
void process_command(int sd, Player *current_player, char *buffer);

/* USER CODE BEGIN 0 */
// 輔助函式：新增物品到 Linked List 
void add_item(Item **head, const char *name) {
    Item *new_item = (Item *)malloc(sizeof(Item));
    strcpy(new_item->name, name);
    new_item->next = *head;
    *head = new_item;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main() {
    /* 1. 初始化系統 (System Initialization) */
    init_map();

    /* 2. 網路變數定義 (Network Variables) */
    int master_socket, addrlen, new_socket, client_socket[MAX_CLIENTS];
    int max_sd, sd, activity, valread;
    struct sockaddr_in address;
    char buffer[BUFFER_SIZE];
    fd_set readfds;

    // 初始化 client socket 陣列
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_socket[i] = 0;
    }

    /* 3. 啟動伺服器 (Start Server) */
    // 我們把 socket, bind, listen 全部封裝進去了
    master_socket = init_server_socket(DEFAULT_PORT);
    
    // 為了 accept 需要重新賦值 address 結構
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(DEFAULT_PORT);
    addrlen = sizeof(address);

    printf("========================================\n");
    printf(" 遊戲伺服器已啟動 (Game Server Started) \n");
    printf(" 監聽 Port: %d\n", DEFAULT_PORT);
    printf(" 可接受連線來源: 本機 (127.0.0.1) 或 區網 IP\n");
    printf("========================================\n");

    /* 4. 主迴圈 (Infinite Loop) */
    while (1) {
        // 清空並重新設定 File Descriptor Set
        FD_ZERO(&readfds);
        FD_SET(master_socket, &readfds);
        max_sd = master_socket;

        // 加入所有連線中的 client
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        // 等待活動 (Wait for activity)
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error\n");
        }

        /* 5. 處理新連線 (Handle New Connection) */
        if (FD_ISSET(master_socket, &readfds)) {
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
           printf("[新連線] Socket fd: %d, IP: %s, Port: %d\n", 
                   new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
            
            // 創建遊戲角色
            create_player(new_socket);

            // 加入 socket 陣列
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    break;
                }
            }
        }

        /* 6. 處理客戶端訊息 (Handle Client IO) */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];

            if (FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, BUFFER_SIZE); // 重要：清空 buffer
                valread = read(sd, buffer, BUFFER_SIZE);

                if (valread == 0) {
                    // 斷線處理
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("[斷線] Host disconnected, fd %d, IP %s\n", sd, inet_ntoa(address.sin_addr));
                    close(sd);
                    client_socket[i] = 0;
                    // TODO: 這裡未來可以加入移除 Player 結構的邏輯
                } else {
                    // 處理接收到的字串
                    buffer[strcspn(buffer, "\n")] = 0;
                    buffer[strcspn(buffer, "\r")] = 0;

                    if (strlen(buffer) > 0) {
                        printf("Client %d says: %s\n", sd, buffer);
                        
                        Player *current_player = find_player_by_fd(sd);
                        if (current_player) {
                            // ★ 核心邏輯轉移到此函式 ★
                            process_command(sd, current_player, buffer);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/* User Code Implementation --------------------------------------------------*/

/**
  * @brief 初始化 Socket、Bind 和 Listen
  */
int init_server_socket(int port) {
    int master_socket;
    struct sockaddr_in address;
    int opt = 1;

    // Create socket
    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return master_socket;
}

/**
  * @brief 處理遊戲核心邏輯 (移動、查看、撿取)
  */
void process_command(int sd, Player *current_player, char *buffer) {
    char response[BUFFER_SIZE];
    char temp[100]; // 暫存字串用
    memset(response, 0, BUFFER_SIZE);

    // --- Command: LOOK ---
    if (strncmp(buffer, "look", 4) == 0) {
        // 1. 顯示 Location
        sprintf(response, "\nLocation: %d %d\n", current_player->x, current_player->y);

        // 2. 顯示 Player(s)
        strcat(response, "Player(s):");
        Player *p = player_list_head; // 使用全域變數 player_list_head
        while (p != NULL) {
            // 檢查是否在同一個房間
            if (p->x == current_player->x && p->y == current_player->y) {
                if (p->socket_fd == sd) {
                    sprintf(temp, " %s(Me)", p->name); // 自己
                } else {
                    sprintf(temp, " %s", p->name);     // 別人
                }
                strcat(response, temp);
            }
            p = p->next;
        }
        strcat(response, "\n");

        // 3. 顯示 Item(s)
        strcat(response, "Item(s):");
        Room *curr_room = &map[current_player->x][current_player->y];
        Item *item = curr_room->ground_items;
        
        if (item == NULL) {
            strcat(response, " (empty)");
        } else {
            while (item != NULL) {
                sprintf(temp, " %s", item->name);
                strcat(response, temp);
                item = item->next;
            }
        }
        strcat(response, "\n");
        
        send(sd, response, strlen(response), 0);
    }
    // --- Command: MOVE (North, South, East, West) ---
    else if (strncmp(buffer, "north", 5) == 0) {
        if (current_player->y > 0) {
            current_player->y--;
            send(sd, "You moved North.\n", 17, 0);
        } else {
            send(sd, "You hit a wall!\n", 16, 0);
        }
    }
    else if (strncmp(buffer, "south", 5) == 0) {
        if (current_player->y < MAP_SIZE - 1) {
            current_player->y++;
            send(sd, "You moved South.\n", 17, 0);
        } else {
            send(sd, "You hit a wall!\n", 16, 0);
        }
    }
    else if (strncmp(buffer, "east", 4) == 0) {
        if (current_player->x < MAP_SIZE - 1) {
            current_player->x++;
            send(sd, "You moved East.\n", 16, 0);
        } else {
            send(sd, "You hit a wall!\n", 16, 0);
        }
    }
    else if (strncmp(buffer, "west", 4) == 0) {
        if (current_player->x > 0) {
            current_player->x--;
            send(sd, "You moved West.\n", 16, 0);
        } else {
            send(sd, "You hit a wall!\n", 16, 0);
        }
    }
    // --- Command: INVENTORY ---
    else if (strncmp(buffer, "inventory", 9) == 0 || strncmp(buffer, "i", 1) == 0) {
        sprintf(response, "Your Backpack:\n");
        Item *item = current_player->backpack;
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
    // --- Command: TAKE ---
    else if (strncmp(buffer, "take", 4) == 0) {
        char target_name[50];
        if (sscanf(buffer + 5, "%s", target_name) == 1) {
            Room *curr_room = &map[current_player->x][current_player->y];
            Item *curr = curr_room->ground_items;
            Item *prev = NULL;
            int found = 0;
            
            while (curr != NULL) {
                if (strcasecmp(curr->name, target_name) == 0) { 
                    found = 1; 
                    break; 
                }
                prev = curr;
                curr = curr->next;
            }

            if (found) {
                // 從地上移除
                if (prev == NULL) curr_room->ground_items = curr->next;
                else prev->next = curr->next;

                // 加到背包
                curr->next = current_player->backpack;
                current_player->backpack = curr;
                
                sprintf(response, "You took the %s.\n", curr->name);
                send(sd, response, strlen(response), 0);
            } else {
                send(sd, "You don't see that here.\n", 25, 0);
            }
        } else {
            send(sd, "Take what?\n", 11, 0);
        }
    }
    // --- Command: DEPOSIT ---
    else if (strncmp(buffer, "deposit", 7) == 0) {
        char target_name[50];
        if (sscanf(buffer + 8, "%s", target_name) == 1) {
            Item *curr = current_player->backpack;
            Item *prev = NULL;
            int found = 0;

            // 在背包中尋找物品
            while (curr != NULL) {
                if (strcasecmp(curr->name, target_name) == 0) {
                    found = 1;
                    break;
                }
                prev = curr;
                curr = curr->next;
            }

            if (found) {
                // 從背包移除
                if (prev == NULL) current_player->backpack = curr->next;
                else prev->next = curr->next;

                // 加到地圖目前位置
                Room *curr_room = &map[current_player->x][current_player->y];
                curr->next = curr_room->ground_items;
                curr_room->ground_items = curr;

                sprintf(response, "You deposited the %s.\n", curr->name);
                send(sd, response, strlen(response), 0);
            } else {
                send(sd, "You don't have that in your backpack.\n", 38, 0);
            }
        } else {
            send(sd, "Deposit what?\n", 14, 0);
        }
    }
    // --- Unknown Command ---
    else {
        char *msg = "Unknown command. Try 'look', 'north', 'south', 'east', 'west', 'take <item>'.\n";
        send(sd, msg, strlen(msg), 0);
    }
}

/**
  * @brief 初始化地圖與載入 map.txt
  */
void init_map() {
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            map[i][j].ground_items = NULL;
        }
    }

    FILE *fp = fopen("map.txt", "r");
    if (fp == NULL) {
        printf("Warning: map.txt not found. Loading default map.\n");
        add_item(&map[0][0].ground_items, "Apple");
        add_item(&map[1][0].ground_items, "Banana");
        add_item(&map[0][1].ground_items, "Sword");
        return;
    }

    int x, y;
    char item_name[MAX_NAME];
    while (fscanf(fp, "%d %d %s", &x, &y, item_name) != EOF) {
        if (x >= 0 && x < MAP_SIZE && y >= 0 && y < MAP_SIZE) {
            add_item(&map[x][y].ground_items, item_name);
        }
    }
    fclose(fp);
    printf("Map initialized successfully!\n");
}

void create_player(int fd) {
    Player *p = (Player *)malloc(sizeof(Player));
    p->socket_fd = fd;
    sprintf(p->name, "Player%d", fd); // 預設名稱
    p->x = 0;
    p->y = 0;
    p->backpack = NULL;
    
    p->next = player_list_head;
    player_list_head = p;
    
    printf("Created player for socket %d\n", fd);
}

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