#include "game.h"       // <--- 引入我們剛寫好的標頭檔
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

// IP 只有 Client 需要知道，所以留在這裡
// 【重要】：當你搬到第二台電腦時，這裡要改成第一台電腦(Server)的真實 IP
// 我們不再這裡寫死 IP，改用變數接收
// #define SERVER_IP "127.0.0.1"

int main(int argc, char *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char *target_ip;

    // --- 這裡加入了判斷邏輯 ---
    if (argc < 2) {
        // 如果使用者沒輸入 IP，預設連本機 (開發用)
        printf("未輸入 IP，預設連線到 127.0.0.1\n");
        target_ip = "127.0.0.1";
    } else {
        // 如果使用者有輸入，就用輸入的 IP (連線用)
        target_ip = argv[1];
    }
    // ------------------------

    // 1. 建立 Socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(DEFAULT_PORT);

    // 轉換 IP 地址 (連線到本機 127.0.0.1)
    if (inet_pton(AF_INET, target_ip, &serv_addr.sin_addr) <= 0) {
        printf("\n無效的 IP 位址: %s \n", target_ip);
        return -1;
    }

    printf("嘗試連線到 %s:%d ...\n", target_ip, DEFAULT_PORT);

    // 2. 連線
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\n連線失敗 (Connection Failed)\n");
        printf("請檢查: 1. Server開了嗎? 2. IP對嗎? 3. 防火牆關了嗎?\n");
        return -1;
    }

    printf("連線成功！(直接打字並按 Enter 即可發送)\n");

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
            printf("%s", buffer); // 直接印出 Server 講的話
            fflush(stdout);       // 強制刷新畫面
        }

        // 情況 B：使用者在鍵盤打字
        if (FD_ISSET(0, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            // 讀取鍵盤輸入
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                // 傳送給 Server
                send(sock, buffer, strlen(buffer), 0);
            }
        }
    }

    close(sock);
    return 0;
}