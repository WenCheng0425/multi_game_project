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
int process_command(int sd, Player *current_player, char *buffer);

void handle_look(int sd, Player *current_player);
void handle_move(int sd, Player *current_player, const char *direction);
void handle_inventory(int sd, Player *current_player);
void handle_take(int sd, Player *current_player, char *item_name);
void handle_deposit(int sd, Player *current_player, char *item_name);

void broadcast_room(Player *sender, char *message);
void handle_tell(int sd, Player *current_player, char *buffer);
void handle_give(int sd, Player *current_player, char *buffer);

void handle_save(int sd, Player *p);
void handle_login(int sd, Player *p, char *username);
void remove_player(int fd);
void handle_name(int sd, Player *current_player, char *buffer);
void handle_exit(int sd, Player *p);
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

                if (valread <= 0) {
                    // 斷線處理
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("[斷線] Host disconnected, fd %d, IP %s\n", sd, inet_ntoa(address.sin_addr));
                    remove_player(sd); // 移除玩家
                    close(sd);
                    client_socket[i] = 0;
                } else {
                    // 處理接收到的字串
                    buffer[strcspn(buffer, "\n")] = 0;
                    buffer[strcspn(buffer, "\r")] = 0;

                    if (strlen(buffer) > 0) {
                        printf("Client %d says: %s\n", sd, buffer);
                        
                        // 1. 找到發送訊息的玩家
                        Player *current_player = find_player_by_fd(sd);
                        
                        if (current_player) {
                            // 2. ★ 關鍵修改：接收 process_command 的回傳值 (int) ★
                            int status = process_command(sd, current_player, buffer);

                            printf("[Debug] process_command returned: %d\n", status); 

                            // 3. ★ 檢查回傳值：如果是 1，代表玩家輸入了 exit ★
                            if (status == 1) {
                                getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                                printf("[斷線] Client requested exit, fd %d\n", sd);
                                
                                remove_player(sd); // 存檔並移除
                                close(sd);         // 關閉 Socket
                                client_socket[i] = 0; // 清除陣列
                            }
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
int process_command(int sd, Player *current_player, char *buffer) {
    // 1. 處理 LOOK
    if (strncmp(buffer, "look", 4) == 0) {
        handle_look(sd, current_player);
    }
    // 2. 處理 移動 (統一交給 handle_move)
    else if (strncmp(buffer, "north", 5) == 0) handle_move(sd, current_player, "North");
    else if (strncmp(buffer, "south", 5) == 0) handle_move(sd, current_player, "South");
    else if (strncmp(buffer, "east", 4) == 0)  handle_move(sd, current_player, "East");
    else if (strncmp(buffer, "west", 4) == 0)  handle_move(sd, current_player, "West");
    // 3. 處理 背包 (Inventory)
    else if (strncmp(buffer, "inventory", 9) == 0 || strncmp(buffer, "i", 1) == 0) {
        handle_inventory(sd, current_player);
    }
    // 4. 處理 撿東西 (TAKE)
    else if (strncmp(buffer, "take", 4) == 0) {
        char target_name[MAX_NAME];
        // 檢查有沒有輸入物品名稱
        if (sscanf(buffer + 5, "%s", target_name) == 1) {
            handle_take(sd, current_player, target_name);
        } else {
            send(sd, "Take what?\n", 11, 0);
        }
    }
    // 5. 處理 丟東西 (DEPOSIT)
    else if (strncmp(buffer, "deposit", 7) == 0) {
        char target_name[MAX_NAME];
        if (sscanf(buffer + 8, "%s", target_name) == 1) {
            handle_deposit(sd, current_player, target_name);
        } else {
            send(sd, "Deposit what?\n", 14, 0);
        }
    }
    // 6. 處理 私訊 (TELL)
    else if (strncmp(buffer, "tell", 4) == 0) {
        handle_tell(sd, current_player, buffer);
    }
    // 7. 處理 交付物品 (GIVE)
    else if (strncmp(buffer, "give", 4) == 0) {
    handle_give(sd, current_player, buffer);
    }
    // 8. 處理 存檔 (SAVE)
    else if (strncmp(buffer, "save", 4) == 0) {
        handle_save(sd, current_player);
    }
    // 9. 處理 登入 (LOGIN)
    else if (strncmp(buffer, "login", 5) == 0) {
        char username[MAX_NAME];
        if (sscanf(buffer + 6, "%s", username) == 1) {
            handle_login(sd, current_player, username);
        } else {
            send(sd, "Usage: login <username>\n", 24, 0);
        }
    }
    // 10. 處理 改名 (NAME)
    else if (strncmp(buffer, "name", 4) == 0) {
        handle_name(sd, current_player, buffer);
    }
    // 11. 處理 離開 (EXIT)
    else if (strncmp(buffer, "exit", 4) == 0) {
        // 1. 自動存檔 (如果是正式玩家)
        if (strncmp(current_player->name, "Guest", 5) != 0) {
            handle_save(sd, current_player); // 重用你的存檔功能
        }
        
        // 2. 發送道別訊息
        send(sd, "Goodbye!\n", 9, 0);
        
        // 3. 雖然這裡是 Server，但我們可以主動讓 Client 感覺到結束
        // 實際上，使用者看到 Goodbye 後自己按 Ctrl+C 也是符合 "Exit" 流程的
        // 如果要做到 Server 主動踢人，需要改變函式回傳值，這改動太大，先這樣就好。
        return 1; // 告訴 main：這個人要斷線了！
    }
    // 12. 未知指令
    else {
        char *msg = "Unknown command. Try 'look', 'north', 'take <item>', 'i'.\n";
        send(sd, msg, strlen(msg), 0);
    }
    return 0; // 正常結束
}

/* ========================================================== */
/* 功能獨立函式          */
/* ========================================================== */

void handle_look(int sd, Player *current_player) {
    char response[BUFFER_SIZE];
    char temp[100];
    memset(response, 0, BUFFER_SIZE);

    // 1. 顯示 Location
    sprintf(response, "\nLocation: %d %d\n", current_player->x, current_player->y);

    // 2. 顯示 Player(s)
    strcat(response, "Player(s):");
    Player *p = player_list_head; 
    while (p != NULL) {
        // 檢查是否在同一個房間
        if (p->x == current_player->x && p->y == current_player->y) {
            if (p->socket_fd == sd) {
                sprintf(temp, " %s(Me)", p->name);
            } else {
                sprintf(temp, " %s", p->name);
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

void handle_move(int sd, Player *p, const char *direction) {
    int moved = 0;
    char broadcast_msg[100];

    // 移動前的廣播 (告訴舊房間的人)
    sprintf(broadcast_msg, "\n[通知] %s left going %s.\n", p->name, direction);
    broadcast_room(p, broadcast_msg);

    // 判斷方向與邊界檢查
    if (strcmp(direction, "North") == 0) {
        if (p->y > 0) { p->y--; moved = 1; }
    } 
    else if (strcmp(direction, "South") == 0) {
        if (p->y < MAP_SIZE - 1) { p->y++; moved = 1; }
    }
    else if (strcmp(direction, "East") == 0) {
        if (p->x < MAP_SIZE - 1) { p->x++; moved = 1; }
    }
    else if (strcmp(direction, "West") == 0) {
        if (p->x > 0) { p->x--; moved = 1; }
    }

    // 回覆訊息
    if (moved) {
       // 告訴自己
        char msg[64];
        sprintf(msg, "You moved %s.\n", direction);
        send(sd, msg, strlen(msg), 0);

        // 移動後的廣播 (告訴新房間的人)
        sprintf(broadcast_msg, "\n[通知] %s entered the room.\n", p->name);
        broadcast_room(p, broadcast_msg);

        // 自動幫玩家看一眼新房間 (優化體驗)
        handle_look(sd, p); 
    } else {
        send(sd, "You hit a wall!\n", 16, 0);
    }
}

void handle_inventory(int sd, Player *current_player) {
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    
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

void handle_take(int sd, Player *current_player, char *target_name) {
    char response[BUFFER_SIZE];
    Room *curr_room = &map[current_player->x][current_player->y];
    Item *curr = curr_room->ground_items;
    Item *prev = NULL;
    int found = 0;
    
    // 在地上找東西
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

        // 加到背包 (頭插法)
        curr->next = current_player->backpack;
        current_player->backpack = curr;
        
        sprintf(response, "You took the %s.\n", curr->name);
        send(sd, response, strlen(response), 0);
    } else {
        send(sd, "You don't see that here.\n", 25, 0);
    }
}

void handle_deposit(int sd, Player *current_player, char *target_name) {
    char response[BUFFER_SIZE];
    Item *curr = current_player->backpack;
    Item *prev = NULL;
    int found = 0;

    // 在背包找東西
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

        // 加到地上 (頭插法)
        Room *curr_room = &map[current_player->x][current_player->y];
        curr->next = curr_room->ground_items;
        curr_room->ground_items = curr;

        sprintf(response, "You deposited the %s.\n", curr->name);
        send(sd, response, strlen(response), 0);
    } else {
        send(sd, "You don't have that in your backpack.\n", 38, 0);
    }
}    

void broadcast_room(Player *sender, char *message) {
    Player *p = player_list_head;
    while (p != NULL) {
        // 1. 必須在同一個房間
        // 2. 不能是發送者自己 (sender)
        if (p->x == sender->x && p->y == sender->y && p->socket_fd != sender->socket_fd) {
            send(p->socket_fd, message, strlen(message), 0);
        }
        p = p->next;
    }
}

void handle_tell(int sd, Player *current_player, char *buffer) {
    char target_name[MAX_NAME];
    char message[BUFFER_SIZE];
    
    // 解析指令： tell <name> <message...>
    // 注意：這裡稍微複雜一點，因為 message 可能包含空白
    //我們先讀取名字
    if (sscanf(buffer + 5, "%s", target_name) != 1) {
        send(sd, "Usage: tell <player_name> <message>\n", 36, 0);
        return;
    }

    // 接著尋找訊息開始的位置
    // buffer + 5 是跳過 "tell "
    // 然後我們要跳過 target_name 的長度，再跳過中間的空白
    char *msg_start = buffer + 5 + strlen(target_name);
    while(*msg_start == ' ') msg_start++; // 跳過空白

    if (strlen(msg_start) == 0) {
        send(sd, "Tell what?\n", 11, 0);
        return;
    }

    // 尋找目標玩家
    Player *p = player_list_head;
    int found = 0;
    while (p != NULL) {
        if (strcmp(p->name, target_name) == 0) {
            // 找到了！發送訊息
            char format_msg[BUFFER_SIZE];
            sprintf(format_msg, "\n[私訊] %s tells you: %s\n", current_player->name, msg_start);
            send(p->socket_fd, format_msg, strlen(format_msg), 0);
            
            send(sd, "Message sent.\n", 14, 0);
            found = 1;
            break;
        }
        p = p->next;
    }

    if (!found) {
        send(sd, "Player not found.\n", 18, 0);
    }
}

void handle_give(int sd, Player *current_player, char *buffer) {
    char target_name[MAX_NAME];
    char item_name[MAX_NAME];
    
    // 解析指令： give <player> <item>
    if (sscanf(buffer + 5, "%s %s", target_name, item_name) != 2) {
        send(sd, "Usage: give <player_name> <item_name>\n", 34, 0);
        return;
    }

    // 1. 找玩家 (必須在同一個房間)
    Player *target = player_list_head;
    int found_player = 0;
    while (target != NULL) {
        if (strcmp(target->name, target_name) == 0) {
            if (target->x == current_player->x && target->y == current_player->y) {
                found_player = 1;
            }
            break;
        }
        target = target->next;
    }

    if (!found_player) {
        send(sd, "Player not found or not in this room.\n", 34, 0);
        return;
    }

    // 2. 找物品 (必須在我的背包裡)
    Item *curr = current_player->backpack;
    Item *prev = NULL;
    int found_item = 0;

    while (curr != NULL) {
        if (strcasecmp(curr->name, item_name) == 0) {
            found_item = 1;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (found_item) {
        // 3. 轉移物品
        // (A) 從我的背包移除
        if (prev == NULL) current_player->backpack = curr->next;
        else prev->next = curr->next;

        // (B) 加到對方的背包 (頭插法)
        curr->next = target->backpack;
        target->backpack = curr;

        // 4. 通知雙方
        char msg[BUFFER_SIZE];
        sprintf(msg, "You gave %s to %s.\n", item_name, target_name);
        send(sd, msg, strlen(msg), 0);

        sprintf(msg, "%s gave you a %s.\n", current_player->name, item_name);
        send(target->socket_fd, msg, strlen(msg), 0);

    } else {
        send(sd, "You don't have that item.\n", 24, 0);
    }
}

/* 存檔功能：把玩家資料寫入檔案 */
void handle_save(int sd, Player *p) {
    char filename[64];
    // 檔名用玩家名字命名，例如 "Vincent.dat"
    sprintf(filename, "%s.dat", p->name);
    
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        send(sd, "System Error: Cannot create save file.\n", 36, 0);
        return;
    }

    // 1. 存座標 (x y)
    fprintf(fp, "%d %d\n", p->x, p->y);

    // 2. 存背包物品
    Item *curr = p->backpack;
    while (curr != NULL) {
        fprintf(fp, "%s\n", curr->name);
        curr = curr->next;
    }

    fclose(fp);
    
    char msg[BUFFER_SIZE];
    sprintf(msg, "Game saved to %s!\n", filename);
    send(sd, msg, strlen(msg), 0);
}

/* 讀檔/登入功能：讀取檔案並恢復狀態 */
void handle_login(int sd, Player *p, char *username) {
    // 檢查是否已經有人用這個名字在線上了
    Player *iterator = player_list_head;
    while (iterator != NULL) {
        // 檢查規則：
        // 1. 名字跟我想登入的 username 一樣
        // 2. 那個人不是我自己 (避免自己登入自己時報錯)
        if (strcasecmp(iterator->name, username) == 0 && iterator->socket_fd != sd) {
            char error_msg[100];
            sprintf(error_msg, "Login Failed: The name '%s' is already online!\n", username);
            send(sd, error_msg, strlen(error_msg), 0);
            return; // 直接踢掉，不准登入
        }
        iterator = iterator->next;
    }

    char filename[64];
    sprintf(filename, "%s.dat", username);

    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        // 檔案不存在 -> 代表是新玩家，直接改名就好
        strcpy(p->name, username); // 改名
        
        char msg[100];
        sprintf(msg, "Welcome, %s! (New Character Created)\n", username);
        send(sd, msg, strlen(msg), 0);
        return;
    }

    // 檔案存在 -> 開始讀取
    printf("Loading data for %s...\n", username);

    // 1. 更新名字
    strcpy(p->name, username);

    // 2. 讀取座標
    fscanf(fp, "%d %d", &p->x, &p->y);

    // 3. 清空目前背包 (避免重複)
    // (簡單寫法：直接把頭指針丟掉，雖然會 Memory Leak 但新手暫時不管)
    p->backpack = NULL; 

    // 4. 讀取背包物品
    char item_name[MAX_NAME];
    while (fscanf(fp, "%s", item_name) != EOF) {
        // 重用我們寫好的 add_item 邏輯，但這裡是加到背包
        // 這裡手動做一個簡單的頭插法
        Item *new_item = (Item *)malloc(sizeof(Item));
        strcpy(new_item->name, item_name);
        new_item->next = p->backpack;
        p->backpack = new_item;
    }

    fclose(fp);

    char msg[100];
    sprintf(msg, "Welcome back, %s! Data loaded.\n", username);
    send(sd, msg, strlen(msg), 0);
    
    // 自動幫他看一下環境
    handle_look(sd, p);
}
/* 移除玩家：當斷線時，把他在 Linked List 中的資料刪除 */
void remove_player(int fd) {
    Player *curr = player_list_head;
    Player *prev = NULL;

    while (curr != NULL) {
        if (curr->socket_fd == fd) {
            // 找到了，準備移除
            
            // 1. 如果他在房間裡，通知大家他走了 (選做)
            char msg[100];
            sprintf(msg, "\n[通知] %s disconnected.\n", curr->name);
            broadcast_room(curr, msg);

            // 2. 釋放背包記憶體 (避免 Memory Leak)
            Item *item = curr->backpack;
            while (item != NULL) {
                Item *temp = item;
                item = item->next;
                free(temp);
            }

            // 3. 從鏈結串列中移除
            if (prev == NULL) {
                // 如果是頭節點
                player_list_head = curr->next;
            } else {
                // 如果是中間節點
                prev->next = curr->next;
            }

            // 4. 釋放玩家記憶體
            free(curr);
            printf("Player with fd %d removed.\n", fd);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void handle_name(int sd, Player *current_player, char *buffer) {
    char new_name[MAX_NAME];
    
    // 解析指令: name <new_name>
    if (sscanf(buffer + 5, "%s", new_name) != 1) {
        send(sd, "Usage: name <new_name>\n", 20, 0);
        return;
    }

    // 1. 檢查【線上】名字是否被佔用 (不分大小寫)
    Player *p = player_list_head;
    while (p != NULL) {
        if (strcasecmp(p->name, new_name) == 0) {
            send(sd, "Name already taken(Online).\n", 19, 0);
            return;
        }
        p = p->next;
    }

    // 2. 檢查：是否跟「存檔」撞名 (避免覆蓋老手存檔) 
    char filename[64];
    sprintf(filename, "%s.dat", new_name); // 拼湊出檔名
    
    FILE *fp = fopen(filename, "r"); // 試著讀讀看
    if (fp != NULL) {
        // 讀得到檔案，代表這個名字已經被註冊過 (雖然他現在不在線上)
        fclose(fp);
        char *err_msg = "Name already registered (File exists). Please choose another.\n";
        send(sd, err_msg, strlen(err_msg), 0);
        return; // 擋下來！保護老手資料
    }
    // 3. 改名
    char old_name[MAX_NAME];
    strcpy(old_name, current_player->name);
    strcpy(current_player->name, new_name);

    char msg[BUFFER_SIZE];
    sprintf(msg, "You changed your name to %s.\n", new_name);
    send(sd, msg, strlen(msg), 0);

    // 4. 廣播告訴大家
    sprintf(msg, "\n[通知] %s is now known as %s.\n", old_name, new_name);
    broadcast_room(current_player, msg);
}

void handle_exit(int sd, Player *p) {
    // 1. 如果是正式玩家，就幫他存檔
    if (strncmp(p->name, "Guest", 5) != 0) {
        // 直接呼叫既有的存檔功能 (它會幫你寫入檔案，並回傳 "Game saved")
        handle_save(sd, p); 
    } 

    // 2. 發送道別訊息
    char msg[64];
    sprintf(msg, "Goodbye, %s!\n", p->name);
    send(sd, msg, strlen(msg), 0);
    
    // (注意：這裡不需要 close(sd)，因為 Server 回去迴圈後，
    //  若 Client 端收到 Goodbye 自行斷線，Server 會在 read() 讀到 0 時自動清理)
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
    sprintf(p->name, "Guest%d", fd);  // 改成 Guest，避開衝突
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

