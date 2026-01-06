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
#include "game.h"       
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <errno.h>

/* Private define ------------------------------------------------------------*/
#define MAX_CLIENTS 10

/* =========================================================================== */
/* 1. 全域變數宣告 / Global Variables                       */
/* =========================================================================== */
Room map[MAP_SIZE][MAP_SIZE]; 
Player *player_list_head = NULL;

/* Private function prototypes -----------------------------------------------*/
// 輔助與初始化 / Helpers & Initialization
void add_item(Item **head, const char *name);
void init_map(void);
void create_player(int fd);
void remove_player(int fd);
Player *find_player_by_fd(int fd);
int init_server_socket(int Port);
int init_multicast_socket();
void send_encrypted(int sd, char *msg, int len, int flags);

// 遊戲邏輯 / Game Logic
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
void handle_name(int sd, Player *current_player, char *buffer);
void handle_exit(int sd, Player *p);

/* USER CODE BEGIN 0 */
/**
 * @brief 新增物品到鏈結串列 / Add item to linked list
 * @param head Pointer to the head of the list (串列頭指標)
 * @param name Name of the item (物品名稱)
 */
void add_item(Item **head, const char *name) {
    Item *new_item = (Item *)malloc(sizeof(Item));
    strcpy(new_item->name, name);
    new_item->next = *head;
    *head = new_item;
}

/**
 * @brief 加密並發送訊息 / Encrypt and send message
 * @param sd Socket descriptor (Socket 描述符)
 * @param msg Message to be sent (欲發送的訊息)
 * @param len Length of message (訊息長度)
 * @param flags Send flags (發送標誌)
 */
void send_encrypted(int sd, char *msg, int len, int flags) {
    char buffer[BUFFER_SIZE];
    
    // Safety check: Return immediately if message is NULL
    // 安全檢查：如果是空的就不處理
    if (msg == NULL) return;

    // Calculate and store original length before encryption
    // 在加密前先算出真正的長度並存起來 (避免加密後出現 Null 導致 strlen 變短)
    int original_len = strlen(msg);

    // Copy string to buffer
    // 複製字串到緩衝區
    strncpy(buffer, msg, BUFFER_SIZE - 1);
    buffer[BUFFER_SIZE - 1] = '\0'; // Ensure null-termination (確保結尾安全)
    
    // Encrypt the buffer (using original length)
    // 加密 (使用原本的長度)
    xor_process(buffer, original_len);

    // Send data (using original length)
    // 發送資料 (使用原本的長度)
    send(sd, buffer, original_len, 0);
}
/* USER CODE END 0 */

/* ================================================================================== */
/* 2. 主程式 / Main Function                                */
/* ================================================================================== */
int main() {
    /* 1. 初始化系統 (System Initialization) */
    init_map();

    /* 2. 網路變數定義 (Network Variables) */
    int master_socket, new_socket, client_socket[MAX_CLIENTS];
    int max_sd, sd, activity, valread;
    char buffer[BUFFER_SIZE];
    fd_set readfds;

    int udp_socket;

    // Use sockaddr_storage to hold both IPv4 and IPv6 structures
    // 改用 sockaddr_storage 這個大容器，才能同時裝下 IPv4 或 IPv6
    struct sockaddr_storage address; 
    socklen_t addrlen;
 
    // Initialize client socket array / 初始化 client socket 陣列
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_socket[i] = 0;
    }

    /* 3. 啟動伺服器 (Start Server) */
    master_socket = init_server_socket(DEFAULT_PORT);

    // Start UDP Server for Discovery / 啟動 UDP Server (搜尋用)
    udp_socket = init_multicast_socket();
    if (udp_socket < 0) {
        printf("Multicast init failed, auto-discovery disabled.\n");
    }

    addrlen = sizeof(address);

    printf("========================================\n");
    printf(" Game Server Started \n");
    printf(" Listening on Port: %d\n", DEFAULT_PORT);
    printf(" Accepting connections from: Localhost or LAN IP\n");
    printf("========================================\n");

    /* 4. 主迴圈 (Infinite Loop) */
    while (1) {
        // Clear and reset File Descriptor Set / 清空並重新設定 File Descriptor Set
        FD_ZERO(&readfds);

        // Add TCP Master Socket / 加入 TCP Master Socket
        FD_SET(master_socket, &readfds);
        max_sd = master_socket;

        // Add UDP Socket to monitoring list / 加入 UDP Socket 到監聽列表
        if (udp_socket > 0) {
            FD_SET(udp_socket, &readfds);
            if (udp_socket > max_sd) max_sd = udp_socket;
        }

        // Add all connected clients / 加入所有連線中的 client
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        // Wait for activity / 等待活動
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error\n");
        }
        // --------------------------------------------------
        // ★Handle UDP Multicast Request / 處理 UDP Multicast 請求 (是否有人在找 Server？)
        // --------------------------------------------------
        if (udp_socket > 0 && FD_ISSET(udp_socket, &readfds)) {
            char udp_buf[128];
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            // Receive UDP packet / 接收 UDP 封包
            int n = recvfrom(udp_socket, udp_buf, sizeof(udp_buf), 0, 
                           (struct sockaddr*)&client_addr, &client_len);
            if (n > 0) {
                udp_buf[n] = '\0';
                // If passphrase is correct / 如果通關密語正確
                if (strcmp(udp_buf, DISCOVERY_MSG) == 0) {
                    printf("[Discovery] Client searching from %s\n", inet_ntoa(client_addr.sin_addr));
                    
                    // Reply: I am here! / 回覆：我在這！
                    sendto(udp_socket, DISCOVERY_RESP, strlen(DISCOVERY_RESP), 0,
                           (struct sockaddr*)&client_addr, client_len);
                }
            }
        }

        /* 5. 處理新連線 (Handle New Connection) */
        if (FD_ISSET(master_socket, &readfds)) {
            // Use generic structure & pointer casting for accept / accept 使用通用結構 & 指標轉型
            addrlen = sizeof(address);
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            // ★ Check if v4 or v6 and display correct IP / 判斷是 v4 還是 v6 並顯示正確 IP
            char ip_str[INET6_ADDRSTRLEN]; 
            int port = 0;

            if (address.ss_family == AF_INET) {
                // 如果是 IPv4
                struct sockaddr_in *s = (struct sockaddr_in *)&address;
                port = ntohs(s->sin_port);
                inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
            } else { 
                // 如果是 IPv6
                struct sockaddr_in6 *s = (struct sockaddr_in6 *)&address;
                port = ntohs(s->sin6_port);
                inet_ntop(AF_INET6, &s->sin6_addr, ip_str, sizeof(ip_str));
            }

            printf("[New Connection] Socket fd: %d, IP: %s, Port: %d\n", new_socket, ip_str, port);

            // Create player character / 創建遊戲角色
            create_player(new_socket);

            // Add to socket array / 加入 socket 陣列
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
            char ip_str[INET6_ADDRSTRLEN];

            if (FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, BUFFER_SIZE); // Important: Clear buffer / 重要：清空 buffer
                valread = read(sd, buffer, BUFFER_SIZE);

                if (valread <= 0) {
                    // // Handle disconnection / 斷線處理
                    getpeername(sd, (struct sockaddr*)&address, &addrlen);
                    
                    char ip_str[INET6_ADDRSTRLEN];
                    if (address.ss_family == AF_INET) {
                        struct sockaddr_in *s = (struct sockaddr_in *)&address;
                        inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
                    } else {
                        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&address;
                        inet_ntop(AF_INET6, &s->sin6_addr, ip_str, sizeof(ip_str));
                    }
                    printf("[Disconnected] Host disconnected, fd %d, IP %s\n", sd, ip_str);
                    remove_player(sd); // Remove player / 移除玩家
                    close(sd);
                    client_socket[i] = 0;
                } else {
                    // Decrypt received command from ClientServer / 收到指令，先解密才能看懂
                    xor_process(buffer, valread);
                    // Process received string / 處理接收到的字串
                    buffer[strcspn(buffer, "\n")] = 0;
                    buffer[strcspn(buffer, "\r")] = 0;

                    if (strlen(buffer) > 0) {
                        printf("Client %d says: %s\n", sd, buffer);
                        
                        // Find the player sending the message / 找到發送訊息的玩家
                        Player *current_player = find_player_by_fd(sd);
                        
                        if (current_player) {
                            // Process command / 處理指令
                            int status = process_command(sd, current_player, buffer);

                            // printf("[Debug] process_command returned: %d\n", status); 

                            // Check if exit is requested / 檢查是否要離開
                            if (status == 1) {
                                getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                                printf("[Disconnected] Host disconnected, fd %d, IP %s\n", sd, ip_str);
                                
                                remove_player(sd);    // Save and remove / 存檔並移除
                                close(sd);            // Close Socket / 關閉 Socket
                                client_socket[i] = 0; // Clear array / 清除陣列
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/* ================================================================================== */
/* 3. 網路初始化 / Network Initialization                   */
/* ================================================================================== */

/**
  * @brief Initialize TCP Socket (IPv4/IPv6 Dual Stack)
  * 初始化 TCP Socket (支援 IPv4/IPv6 雙堆疊)
  */
int init_server_socket(int port) {
    int master_socket;
    struct sockaddr_in6 address; // ★ Use IPv6 structure / 改用 IPv6 結構
    int opt = 1;
    int no = 0;

    // 1. Create IPv6 Socket / 建立 IPv6 Socket
    if ((master_socket = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 2. Set Socket Options: Allow Port Reuse / 設定 Socket 選項：允許 Port 重用
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt reuseaddr");
        exit(EXIT_FAILURE);
    }

    // 3. Enable Dual Stack (Receive both IPv4 and IPv6) / 開啟 Dual Stack
    if (setsockopt(master_socket, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no)) < 0) {
        perror("setsockopt v6only");
        exit(EXIT_FAILURE);
    }

    // 4. Bind Address / 綁定位址
    memset(&address, 0, sizeof(address));
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any; // ★ Any address (IPv4/IPv6) / 任意位址
    address.sin6_port = htons(port);
    
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 5. Listen / 監聽
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return master_socket;
}

/**
  * @brief Initialize UDP Multicast Socket (For Auto Discovery)
  * 初始化 UDP Multicast Socket (用於自動搜尋)
  */
int init_multicast_socket() {
    int sock;
    struct sockaddr_in addr;
    struct ip_mreq mreq;
    int reuse = 1;

    // 1. Create UDP Socket / 建立 UDP Socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Multicast socket creation failed");
        return -1;
    }

    // 2. Allow Port Reuse / 允許 Port 重用
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        perror("Setting SO_REUSEADDR error");
        close(sock);
        return -1;
    }

    // 3. Bind to Multicast Port / 綁定到 Multicast Port (8888)
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    addr.sin_port = htons(MCAST_PORT);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Binding multicast socket failed");
        close(sock);
        return -1;
    }

    // 4. Join Multicast Group / 加入多播群組 (239.0.0.1)
    mreq.imr_multiaddr.s_addr = inet_addr(MCAST_GRP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        perror("Adding multicast group failed");
        close(sock);
        return -1;
    }

    printf(">> Multicast Discovery Listener active on %s:%d\n", MCAST_GRP, MCAST_PORT);
    return sock;
}

/* ================================================================================== */
/* 4. 遊戲核心邏輯 / Game Core Logic                        */
/* ================================================================================== */

/**
  * @brief Parse and process commands / 解析並處理玩家指令
  */
int process_command(int sd, Player *current_player, char *buffer) {
    // 1. LOOK
    if (strncmp(buffer, "look", 4) == 0) {
        handle_look(sd, current_player);
    }
    // 2. MOVE (North, South, East, West)
    else if (strncmp(buffer, "north", 5) == 0) handle_move(sd, current_player, "North");
    else if (strncmp(buffer, "south", 5) == 0) handle_move(sd, current_player, "South");
    else if (strncmp(buffer, "east", 4) == 0)  handle_move(sd, current_player, "East");
    else if (strncmp(buffer, "west", 4) == 0)  handle_move(sd, current_player, "West");
    // 3. INVENTORY
    else if (strncmp(buffer, "inventory", 9) == 0 || strncmp(buffer, "i", 1) == 0) {
        handle_inventory(sd, current_player);
    }
    // 4. TAKE
    else if (strncmp(buffer, "take", 4) == 0) {
        char target_name[MAX_NAME];
        if (sscanf(buffer + 5, "%s", target_name) == 1) {
            handle_take(sd, current_player, target_name);
        } else {
            send_encrypted(sd, "Take what?\n", 11, 0);
        }
    }
    // 5. DEPOSIT
    else if (strncmp(buffer, "deposit", 7) == 0) {
        char target_name[MAX_NAME];
        if (sscanf(buffer + 8, "%s", target_name) == 1) {
            handle_deposit(sd, current_player, target_name);
        } else {
            send_encrypted(sd, "Deposit what?\n", 14, 0);
        }
    }
    // 6. TELL (Private Message)
    else if (strncmp(buffer, "tell", 4) == 0) {
        handle_tell(sd, current_player, buffer);
    }
    // 7. GIVE
    else if (strncmp(buffer, "give", 4) == 0) {
        handle_give(sd, current_player, buffer);
    }
    // 8. SAVE
    else if (strncmp(buffer, "save", 4) == 0) {
        handle_save(sd, current_player);
    }
    // 9. LOGIN
    else if (strncmp(buffer, "login", 5) == 0) {
        char username[MAX_NAME];
        if (sscanf(buffer + 6, "%s", username) == 1) {
            handle_login(sd, current_player, username);
        } else {
            send_encrypted(sd, "Usage: login <username>\n", 24, 0);
        }
    }
    // 10. NAME (Change Name/Register)
    else if (strncmp(buffer, "name", 4) == 0) {
        handle_name(sd, current_player, buffer);
    }
    // 11. EXIT
    else if (strncmp(buffer, "exit", 4) == 0) {
        if (strncmp(current_player->name, "Guest", 5) != 0) {
            handle_save(sd, current_player); 
        }
        send_encrypted(sd, "Goodbye!\n", 9, 0);
        return 1; // Return 1 to disconnect / 回傳 1 表示要斷線
    }
    // 12. Unknown Command
    else {
        char *msg = "Unknown command. Try 'look', 'north', 'take <item>', 'i'.\n";
        send_encrypted(sd, msg, strlen(msg), 0);
    }
    return 0; // Continue / 正常結束
}

/**
 * @brief Handle Look Command / 處理查看指令
 */
void handle_look(int sd, Player *current_player) {
    char response[BUFFER_SIZE];
    char temp[100];
    memset(response, 0, BUFFER_SIZE);

    // 1. Show Location
    sprintf(response, "\nLocation: %d %d\n", current_player->x, current_player->y);

    // 2. Show Player(s)
    strcat(response, "Player(s):");
    Player *p = player_list_head; 
    while (p != NULL) {
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

    // 3. Show Item(s)
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
    
    send_encrypted(sd, response, strlen(response), 0);
}

/**
 * @brief Handle Move Command / 處理移動指令
 */
void handle_move(int sd, Player *p, const char *direction) {
    int can_move = 0;
    int new_x = p->x;
    int new_y = p->y;
    char broadcast_msg[100];
    char msg[64];

    // 1. 先計算新座標與檢查邊界 / Pre-calculate new coordinates and check boundaries
    if (strcmp(direction, "North") == 0) {
        if (p->y > 0) { new_y--; can_move = 1; }
    } 
    else if (strcmp(direction, "South") == 0) {
        if (p->y < MAP_SIZE - 1) { new_y++; can_move = 1; }
    }
    else if (strcmp(direction, "East") == 0) {
        if (p->x < MAP_SIZE - 1) { new_x++; can_move = 1; }
    }
    else if (strcmp(direction, "West") == 0) {
        if (p->x > 0) { new_x--; can_move = 1; }
    }

    // 2. 根據檢查結果執行動作 / Execute move if valid
    if (can_move) {
        // Notify users in the OLD room (Before updating coordinates)
        sprintf(broadcast_msg, "\n[Notification] %s left the room.\n", p->name);
        broadcast_room(p, broadcast_msg); 
        // p->x and p->y are still the old values here

        // Update player's actual position
        p->x = new_x;
        p->y = new_y;

        // [Feedback] Send confirmation to the player
        sprintf(msg, "You moved %s.\n", direction);
        send_encrypted(sd, msg, strlen(msg), 0);

        // [Broadcast B] Notify users in the NEW room (After updating coordinates)
        // p->x and p->y are now the new values.
        sprintf(broadcast_msg, "\n[Notification] %s entered the room.\n", p->name);
        broadcast_room(p, broadcast_msg);

        // Automatically show the new room description
        handle_look(sd, p); 

    } else {
        // Handle invalid move (Hit a wall)
        send_encrypted(sd, "You hit a wall!\n", 16, 0);
    }
}

/**
 * @brief Handle Inventory / 處理背包查看
 */
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
    send_encrypted(sd, response, strlen(response), 0);
}

/**
 * @brief Handle Take Item / 處理撿取物品
 */
void handle_take(int sd, Player *current_player, char *target_name) {
    char response[BUFFER_SIZE];
    Room *curr_room = &map[current_player->x][current_player->y];
    Item *curr = curr_room->ground_items;
    Item *prev = NULL;
    int found = 0;
    
    // Find item on ground / 在地上找東西
    while (curr != NULL) {
        if (strcasecmp(curr->name, target_name) == 0) { 
            found = 1; 
            break; 
        }
        prev = curr;
        curr = curr->next;
    }

    if (found) {
        // Remove from ground / 從地上移除
        if (prev == NULL) curr_room->ground_items = curr->next;
        else prev->next = curr->next;

        // Add to backpack / 加到背包
        curr->next = current_player->backpack;
        current_player->backpack = curr;
        
        sprintf(response, "You took the %s.\n", curr->name);
        send_encrypted(sd, response, strlen(response), 0);
    } else {
        send_encrypted(sd, "You don't see that here.\n", 25, 0);
    }
}

/**
 * @brief Handle Deposit Item / 處理丟棄物品
 */
void handle_deposit(int sd, Player *current_player, char *target_name) {
    char response[BUFFER_SIZE];
    Item *curr = current_player->backpack;
    Item *prev = NULL;
    int found = 0;

    // Find item in backpack / 在背包找東西
    while (curr != NULL) {
        if (strcasecmp(curr->name, target_name) == 0) {
            found = 1;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (found) {
        // Remove from backpack / 從背包移除
        if (prev == NULL) current_player->backpack = curr->next;
        else prev->next = curr->next;

        // Add to ground / 加到地上
        Room *curr_room = &map[current_player->x][current_player->y];
        curr->next = curr_room->ground_items;
        curr_room->ground_items = curr;

        sprintf(response, "You deposited the %s.\n", curr->name);
        send_encrypted(sd, response, strlen(response), 0);
    } else {
        send_encrypted(sd, "You don't have that in your backpack.\n", 38, 0);
    }
}  

/**
 * @brief Broadcast message to other players in the same room
 * 廣播訊息給同房間的其他人
 */
void broadcast_room(Player *sender, char *message) {
    Player *p = player_list_head;
    while (p != NULL) {
        // 1. Same room / 必須在同一個房間
        // 2. Not the sender / 不能是發送者自己
        if (p->x == sender->x && p->y == sender->y && p->socket_fd != sender->socket_fd) {
            send_encrypted(p->socket_fd, message, strlen(message), 0);
        }
        p = p->next;
    }
}

/**
 * @brief Handle Tell (Private Message) / 處理私訊
 */
void handle_tell(int sd, Player *current_player, char *buffer) {
    char target_name[MAX_NAME];
    
    // format: tell <name> <message...>
    if (sscanf(buffer + 5, "%s", target_name) != 1) {
        send_encrypted(sd, "Usage: tell <player_name> <message>\n", 36, 0);
        return;
    }

    char *msg_start = buffer + 5 + strlen(target_name);
    while(*msg_start == ' ') msg_start++; 

    if (strlen(msg_start) == 0) {
        send_encrypted(sd, "Tell what?\n", 11, 0);
        return;
    }

    Player *p = player_list_head;
    int found = 0;
    while (p != NULL) {
        if (strcmp(p->name, target_name) == 0) {
            char format_msg[BUFFER_SIZE];
            sprintf(format_msg, "\n[Private] %s tells you: %s\n", current_player->name, msg_start);
            send_encrypted(p->socket_fd, format_msg, strlen(format_msg), 0);
            
            send_encrypted(sd, "Message sent.\n", 14, 0);
            found = 1;
            break;
        }
        p = p->next;
    }

    if (!found) {
        send_encrypted(sd, "Player not found.\n", 18, 0);
    }
}

/**
 * @brief Handle Give Item / 處理給予物品
 */
void handle_give(int sd, Player *current_player, char *buffer) {
    char target_name[MAX_NAME];
    char item_name[MAX_NAME];
    
    // format: give <player> <item>
    if (sscanf(buffer + 5, "%s %s", target_name, item_name) != 2) {
        send_encrypted(sd, "Usage: give <player_name> <item_name>\n", 34, 0);
        return;
    }

    // 1. Find Player (Must be in same room) / 找玩家
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
        send_encrypted(sd, "Player not found or not in this room.\n", 34, 0);
        return;
    }

    // 2. Find Item (Must be in backpack) / 找物品
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
        // 3. Transfer Item / 轉移物品
        if (prev == NULL) current_player->backpack = curr->next;
        else prev->next = curr->next;

        curr->next = target->backpack;
        target->backpack = curr;

        // 4. Notify both / 通知雙方
        char msg[BUFFER_SIZE];
        sprintf(msg, "You gave %s to %s.\n", item_name, target_name);
        send_encrypted(sd, msg, strlen(msg), 0);

        sprintf(msg, "%s gave you a %s.\n", current_player->name, item_name);
        send_encrypted(target->socket_fd, msg, strlen(msg), 0);

    } else {
        send_encrypted(sd, "You don't have that item.\n", 24, 0);
    }
}

/* ================================================================================== */
/* 5. 玩家管理與輔助工具 / Player Management & Utils        */
/* ================================================================================== */

/**
 * @brief Save Game / 玩家存檔
 */
void handle_save(int sd, Player *p) {
    char filename[64];
    // 檔名用玩家名字命名，例如 "Vincent.dat"
    sprintf(filename, "%s.dat", p->name);
    
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        send_encrypted(sd, "System Error: Cannot create save file.\n", 36, 0);
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
    send_encrypted(sd, msg, strlen(msg), 0);
}

/**
 * @brief Login (Load Game) / 玩家登入
 */
void handle_login(int sd, Player *p, char *username) {
    // Check duplicate login / 檢查重複登入
    Player *iterator = player_list_head;
    while (iterator != NULL) {
        if (strcasecmp(iterator->name, username) == 0 && iterator->socket_fd != sd) {
            char error_msg[100];
            sprintf(error_msg, "Login Failed: The name '%s' is already online!\n", username);
            send_encrypted(sd, error_msg, strlen(error_msg), 0);
            return; 
        }
        iterator = iterator->next;
    }

    char filename[64];
    sprintf(filename, "%s.dat", username);

    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        // New player, change name directly / 新玩家，直接改名
        strcpy(p->name, username);
        char msg[100];
        sprintf(msg, "Welcome, %s! (New Character Created)\n", username);
        send_encrypted(sd, msg, strlen(msg), 0);
        return;
    }

    printf("Loading data for %s...\n", username);

    // 1. Update Name
    strcpy(p->name, username);

    // 2. Load Coordinates
    fscanf(fp, "%d %d", &p->x, &p->y);

    // 3. Clear Backpack
    p->backpack = NULL; 

    // 4. Load Items
    char item_name[MAX_NAME];
    while (fscanf(fp, "%s", item_name) != EOF) {
        Item *new_item = (Item *)malloc(sizeof(Item));
        strcpy(new_item->name, item_name);
        new_item->next = p->backpack;
        p->backpack = new_item;
    }

    fclose(fp);

    char msg[100];
    sprintf(msg, "Welcome back, %s! Data loaded.\n", username);
    send_encrypted(sd, msg, strlen(msg), 0);
    
    handle_look(sd, p);
}

/**
 * @brief Remove Player (Disconnection) / 移除玩家 (斷線處理)
 */
void remove_player(int fd) {
    Player *curr = player_list_head;
    Player *prev = NULL;

    while (curr != NULL) {
        if (curr->socket_fd == fd) {
            // Broadcast Disconnection / 廣播離開訊息
            char msg[100];
            sprintf(msg, "\n[Notification] %s disconnected.\n", curr->name);
            broadcast_room(curr, msg);

            // Free Backpack / 釋放背包
            Item *item = curr->backpack;
            while (item != NULL) {
                Item *temp = item;
                item = item->next;
                free(temp);
            }

            // Remove Node / 移除節點
            if (prev == NULL) {
                player_list_head = curr->next;
            } else {
                prev->next = curr->next;
            }

            free(curr);
            printf("Player with fd %d removed.\n", fd);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * @brief Change Name (Register) / 玩家改名 (註冊)
 */
void handle_name(int sd, Player *current_player, char *buffer) {
    char new_name[MAX_NAME];
    
    // name <new_name>
    if (sscanf(buffer + 5, "%s", new_name) != 1) {
        send_encrypted(sd, "Usage: name <new_name>\n", 20, 0);
        return;
    }

    // Check Online Names / 檢查線上名字
    Player *p = player_list_head;
    while (p != NULL) {
        if (strcasecmp(p->name, new_name) == 0) {
            send_encrypted(sd, "Name already taken(Online).\n", 19, 0);
            return;
        }
        p = p->next;
    }

    // Check File Registration / 檢查存檔是否重複
    char filename[64];
    sprintf(filename, "%s.dat", new_name); 
    
    FILE *fp = fopen(filename, "r"); 
    if (fp != NULL) {
        fclose(fp);
        char *err_msg = "Name already registered (File exists). Please choose another.\n";
        send_encrypted(sd, err_msg, strlen(err_msg), 0);
        return; 
    }

    char old_name[MAX_NAME];
    strcpy(old_name, current_player->name);
    strcpy(current_player->name, new_name);

    char msg[BUFFER_SIZE];
    sprintf(msg, "You changed your name to %s.\n", new_name);
    send_encrypted(sd, msg, strlen(msg), 0);

    // Broadcast Name Change / 廣播改名
    sprintf(msg, "\n[Notification] %s is now known as %s.\n", old_name, new_name);
    broadcast_room(current_player, msg);
}

/**
 * @brief Handle Exit Command / 處理離開指令
 */
void handle_exit(int sd, Player *p) {
    if (strncmp(p->name, "Guest", 5) != 0) {
        handle_save(sd, p); 
    } 

    char msg[64];
    sprintf(msg, "Goodbye, %s!\n", p->name);
    send_encrypted(sd, msg, strlen(msg), 0);
}

/**
  * @brief Initialize Map / 初始化地圖
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

/**
 * @brief Create a new player and send welcome message / 建立新玩家並發送歡迎訊息
 */
void create_player(int fd) {
    Player *p = (Player *)malloc(sizeof(Player));
    p->socket_fd = fd;
    
    // default name : Guest / 設定預設名稱
    sprintf(p->name, "Guest%d", fd);
    
    p->x = 0;
    p->y = 0;
    p->backpack = NULL;
    
    p->next = player_list_head;
    player_list_head = p;
    
    printf("Created player for socket %d\n", fd);

    // Welcome Message / 歡迎訊息
    char welcome_msg[512]; 
    sprintf(welcome_msg, 
        "\n"
        "========================================\n"
        "      Welcome to the MUD Game!          \n"
        "========================================\n"
        "You are currently logged in as: %s\n"
        "\n"
        "[Register / Change Name]:\n"
        "  Type 'name <your_name>' to create a new identity.\n"
        "  (e.g., name Vincent)\n"
        "\n"
        "[Login]:\n"
        "  Type 'login <username>' to load your saved data.\n"
        "  (e.g., login Vincent)\n"
        "\n"
        "Type 'look' to see around.\n"
        "========================================\n"
        , p->name);

    send_encrypted(fd, welcome_msg, strlen(welcome_msg), 0);
}

/**
 * @brief Find player by socket fd / 依據 fd 尋找玩家
 */
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