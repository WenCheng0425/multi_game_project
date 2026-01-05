#include "game.h"       
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

// We use a variable instead of hardcoding it.
// #define SERVER_IP "127.0.0.

/**
 * @brief 自動搜尋 Server IP / Auto Discovery Server IP
 */
int discover_server_ip(char *found_ip) {
    int sock;
    struct sockaddr_in addr;
    char buffer[128];
    struct timeval tv;

    printf("Searching for server in LAN (Auto Discovery)...\n");

    // 1. 建立 UDP Socket / Create UDP Socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation failed");
        return 0;
    }

    // 2. 設定目標地址 (Multicast Group) / Set Target Address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(MCAST_GRP);
    addr.sin_port = htons(MCAST_PORT);

    // 3. 發送搜尋請求 "MUD_WHO" / Send Discovery Request
    sendto(sock, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0,
           (struct sockaddr*)&addr, sizeof(addr));

    // 4. 設定超時 (等 2 秒，沒人回就算了) / Set Timeout (Wait 2 seconds)
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // 5. 等待回應 / Wait for Response
    struct sockaddr_in server_addr;
    socklen_t len = sizeof(server_addr);
    int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, &len);

    if (n > 0) {
        buffer[n] = '\0';
        if (strcmp(buffer, DISCOVERY_RESP) == 0) {
            // 成功收到 Server 回應，將 IP 轉成字串
            // Success: Server responded, convert IP to string
            strcpy(found_ip, inet_ntoa(server_addr.sin_addr));
            printf(">> Server found! IP: %s\n", found_ip);
            close(sock);
            return 1; // Success
        }
    }

    printf(">> Server not detected (Timeout).\n");
    close(sock);
    return 0; // Failed
}

int main(int argc, char *argv[]) {
    int sock = 0;
    // 準備兩個結構，看情況用哪一個 / Prepare structures for IPv4 and IPv6
    struct sockaddr_in serv_addr_v4;
    struct sockaddr_in6 serv_addr_v6;

    char buffer[BUFFER_SIZE] = {0};
    
    // 準備一個字串陣列來存 IP (不管是輸入的，還是自動找到的)
    // String to store IP (either input or auto-discovered)
    char target_ip_storage[64]; 
    char *target_ip = target_ip_storage;

    // --- 1. 決定目標 IP / Determine Target IP ---
    if (argc < 2) {
        // 使用者沒輸入 IP -> 啟動自動搜尋！ / No IP entered -> Start Auto Discovery!
        if (discover_server_ip(target_ip_storage)) {
            // 搜尋成功，target_ip 已經被填入 Server 的 IP 了
            // Search success, target_ip is filled with Server IP
        } else {
            // 搜尋失敗，退回預設值 / Search failed, fallback to default
            printf("Auto discovery failed. Defaulting to 127.0.0.1\n");
            strcpy(target_ip, "127.0.0.1");
        }
    } else {
        // 使用者有輸入，直接用 / Use provided IP
        strcpy(target_ip, argv[1]);
    }
    
    // --- 2. 判斷是 IPv4 還是 IPv6 (檢查有沒有冒號 ':') / Check IPv4 or IPv6
    if (strchr(target_ip, ':') != NULL) {
        // ==========================
        //      IPv6 連線模式 / IPv6 Connection
        // ==========================
        printf("Detected IPv6 address [%s]...\n", target_ip);

        // 建立 IPv6 Socket / Create IPv6 Socket
        if ((sock = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
            printf("\n Socket creation error (IPv6) \n");
            return -1;
        }

        memset(&serv_addr_v6, 0, sizeof(serv_addr_v6));
        serv_addr_v6.sin6_family = AF_INET6;
        serv_addr_v6.sin6_port = htons(DEFAULT_PORT);

        // 轉換 IPv6 地址 / Convert IPv6 Address
        if (inet_pton(AF_INET6, target_ip, &serv_addr_v6.sin6_addr) <= 0) {
            printf("\nInvalid IPv6 address: %s \n", target_ip);
            return -1;
        }

        printf("Attempting to connect to IPv6 Server...\n");
        if (connect(sock, (struct sockaddr *)&serv_addr_v6, sizeof(serv_addr_v6)) < 0) {
            printf("\nIPv6 Connection Failed\n");
            return -1;
        }

    } else {
        // ==========================
        //      IPv4 連線模式 / IPv4 Connection
        // ==========================
        printf("Detected IPv4 address %s...\n", target_ip);

        // 建立 IPv4 Socket / Create IPv4 Socket
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("\n Socket creation error (IPv4) \n");
            return -1;
        }

        memset(&serv_addr_v4, 0, sizeof(serv_addr_v4));
        serv_addr_v4.sin_family = AF_INET;
        serv_addr_v4.sin_port = htons(DEFAULT_PORT);

        // 轉換 IPv4 地址 / Convert IPv4 Address
        if (inet_pton(AF_INET, target_ip, &serv_addr_v4.sin_addr) <= 0) { 
             // 這裡通常是 .sin_addr，但有些平台實作差異，為了保險起見我們再寫一次標準的
             if (inet_pton(AF_INET, target_ip, &serv_addr_v4.sin_addr) <= 0) {
                 printf("\nInvalid IPv4 address: %s \n", target_ip);
                 return -1;
             }
        }

        printf("Attempting to connect to IPv4 Server...\n");
        if (connect(sock, (struct sockaddr *)&serv_addr_v4, sizeof(serv_addr_v4)) < 0) {
            printf("\nIPv4 Connection Failed\n");
            return -1;
        }
    }

    printf("Connection successful! (Type and press Enter to send)\n");
    
    // 主動接收歡迎訊息 / Receive Welcome Message
    char welcome_buffer[2048] = {0};
    int valread = recv(sock, welcome_buffer, sizeof(welcome_buffer) - 1, 0);
    
    if (valread > 0) {
        // 解密歡迎訊息 / Decrypt Welcome Message
        xor_process(welcome_buffer, valread);

        welcome_buffer[valread] = '\0';
        printf("%s\n", welcome_buffer);
        fflush(stdout);
    }
    
    fd_set readfds;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(0, &readfds);       // 0 代表標準輸入 (鍵盤) / 0 is Keyboard
        FD_SET(sock, &readfds);    // sock 代表 Server 的連線 / sock is Server

        // 使用 select 監聽這兩個來源 / Use select to monitor both
        int activity = select(sock + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0)) {
            printf("Select error\n");
        }

        // 情況 A：Server 傳來訊息 / Case A: Message from Server
        if (FD_ISSET(sock, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int valread = read(sock, buffer, BUFFER_SIZE);
            if (valread == 0) {
                printf("Server disconnected.\n");
                break;
            }
            // 解密 Server 傳來的內容 / Decrypt Content
            xor_process(buffer, valread);

            printf("%s", buffer); // 直接印出 Server 講的話 / Print message
            fflush(stdout);       // 強制刷新畫面 / Force flush
        }

        // 情況 B：使用者在鍵盤打字 / Case B: User typing on Keyboard
        if (FD_ISSET(0, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            // 讀取鍵盤輸入 / Read keyboard input
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                // 發送前先加密 / Encrypt before Sending
                int len = strlen(buffer);

                xor_process(buffer, len);
                // 傳送給 Server / Send to Server
                send(sock, buffer, strlen(buffer), 0);
            }
        }
    }

    close(sock);
    return 0;
}