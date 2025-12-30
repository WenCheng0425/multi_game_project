#include "game.h"       // <--- 引入我們剛寫好的標頭檔
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

// IP 只有 Client 需要知道，所以留在這裡
// 【重要】：當你搬到第二台電腦時，這裡要改成第一台電腦(Server)的真實 IP
// 我們不再這裡寫死 IP，改用變數接收
// #define SERVER_IP "127.0.0.1"

/* 自動搜尋 Server IP */
int discover_server_ip(char *found_ip) {
    int sock;
    struct sockaddr_in addr;
    char buffer[128];
    struct timeval tv;

    printf("正在搜尋區域網路內的伺服器 (Auto Discovery)...\n");

    // 1. 建立 UDP Socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation failed");
        return 0;
    }

    // 2. 設定目標地址 (Multicast Group)
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(MCAST_GRP);
    addr.sin_port = htons(MCAST_PORT);

    // 3. 發送搜尋請求 "MUD_WHO"
    sendto(sock, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0,
           (struct sockaddr*)&addr, sizeof(addr));

    // 4. 設定超時 (等 2 秒，沒人回就算了)
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // 5. 等待回應
    struct sockaddr_in server_addr;
    socklen_t len = sizeof(server_addr);
    int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, &len);

    if (n > 0) {
        buffer[n] = '\0';
        if (strcmp(buffer, DISCOVERY_RESP) == 0) {
            // 成功收到 Server 回應，將 IP 轉成字串
            strcpy(found_ip, inet_ntoa(server_addr.sin_addr));
            printf(">> 找到伺服器！ IP: %s\n", found_ip);
            close(sock);
            return 1; // 成功
        }
    }

    printf(">> 未偵測到伺服器 (Timeout).\n");
    close(sock);
    return 0; // 失敗
}

int main(int argc, char *argv[]) {
    int sock = 0;
    // 準備兩個結構，看情況用哪一個
    struct sockaddr_in serv_addr_v4;
    struct sockaddr_in6 serv_addr_v6;

    char buffer[BUFFER_SIZE] = {0};
    
    // 準備一個字串陣列來存 IP (不管是輸入的，還是自動找到的)
    char target_ip_storage[64]; 
    char *target_ip = target_ip_storage;

    // --- 1. 決定目標 IP ---
    if (argc < 2) {
        // 使用者沒輸入 IP -> 啟動自動搜尋！
        if (discover_server_ip(target_ip_storage)) {
            // 搜尋成功，target_ip 已經被填入 Server 的 IP 了
        } else {
            // 搜尋失敗，退回預設值
            printf("自動搜尋失敗，預設連線到 127.0.0.1\n");
            strcpy(target_ip, "127.0.0.1");
        }
    } else {
        // 使用者有輸入，直接用
        strcpy(target_ip, argv[1]);
    }
    // --- 2. 判斷是 IPv4 還是 IPv6 (檢查有沒有冒號 ':') ---
    if (strchr(target_ip, ':') != NULL) {
        // ==========================
        //      IPv6 連線模式
        // ==========================
        printf("偵測到 IPv6 位址 [%s]...\n", target_ip);

        // 建立 IPv6 Socket
        if ((sock = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
            printf("\n Socket creation error (IPv6) \n");
            return -1;
        }

        memset(&serv_addr_v6, 0, sizeof(serv_addr_v6));
        serv_addr_v6.sin6_family = AF_INET6;
        serv_addr_v6.sin6_port = htons(DEFAULT_PORT);

        // 轉換 IPv6 地址
        if (inet_pton(AF_INET6, target_ip, &serv_addr_v6.sin6_addr) <= 0) {
            printf("\n無效的 IPv6 位址: %s \n", target_ip);
            return -1;
        }

        printf("嘗試連線到 IPv6 Server...\n");
        if (connect(sock, (struct sockaddr *)&serv_addr_v6, sizeof(serv_addr_v6)) < 0) {
            printf("\n連線失敗 (IPv6 Connection Failed)\n");
            return -1;
        }

    } else {
        // ==========================
        //      IPv4 連線模式
        // ==========================
        printf("偵測到 IPv4 位址 %s...\n", target_ip);

        // 建立 IPv4 Socket
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("\n Socket creation error (IPv4) \n");
            return -1;
        }

        memset(&serv_addr_v4, 0, sizeof(serv_addr_v4));
        serv_addr_v4.sin_family = AF_INET;
        serv_addr_v4.sin_port = htons(DEFAULT_PORT);

        // 轉換 IPv4 地址
        if (inet_pton(AF_INET, target_ip, &serv_addr_v4.sin_addr) <= 0) {
            printf("\n無效的 IPv4 位址: %s \n", target_ip);
            return -1;
        }

        printf("嘗試連線到 IPv4 Server...\n");
        if (connect(sock, (struct sockaddr *)&serv_addr_v4, sizeof(serv_addr_v4)) < 0) {
            printf("\n連線失敗 (IPv4 Connection Failed)\n");
            return -1;
        }
    }

    printf("連線成功！(直接打字並按 Enter 即可發送)\n");
    // 主動接收歡迎訊息 
    char welcome_buffer[2048] = {0};
    int valread = recv(sock, welcome_buffer, sizeof(welcome_buffer) - 1, 0);
    
    if (valread > 0) {
        // ★★★ 新增：解密歡迎訊息 ★★★
        xor_process(welcome_buffer, valread);

        welcome_buffer[valread] = '\0';
        printf("%s\n", welcome_buffer);
        fflush(stdout);
    }
    fd_set readfds;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(0, &readfds);       // 0 代表標準輸入 (鍵盤)
        FD_SET(sock, &readfds);    // sock 代表 Server 的連線

        // 使用 select 監聽這兩個來源
        // sock + 1 是因為 select 需要知道最大的 fd 號碼是多少
        int activity = select(sock + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0)) {
            printf("Select error\n");
        }

        // 情況 A：Server 傳來訊息
        if (FD_ISSET(sock, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int valread = read(sock, buffer, BUFFER_SIZE);
            if (valread == 0) {
                printf("Server disconnected.\n");
                break;
            }
            // ★★★ 新增：解密 Server 傳來的內容 ★★★
            xor_process(buffer, valread);

            printf("%s", buffer); // 直接印出 Server 講的話
            fflush(stdout);       // 強制刷新畫面
        }

        // 情況 B：使用者在鍵盤打字
        if (FD_ISSET(0, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            // 讀取鍵盤輸入
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                // ★★★ 新增：發送前先加密！ ★★★
                int len = strlen(buffer);

                xor_process(buffer, len);
                // 傳送給 Server
                send(sock, buffer, strlen(buffer), 0);
            }
        }
    }

    close(sock);
    return 0;
}