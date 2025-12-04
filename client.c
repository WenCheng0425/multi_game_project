#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    // 1. 建立 Socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 設定 IP
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 2. Connect (連線)
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }
    
    printf("已連線到 Server! (輸入 exit 可以離開)\n");

    // ==========================================
    // 3. 修改重點：加入 While 迴圈
    // ==========================================
    while(1) {
        // 清空 buffer，以免留有上一次的垃圾資料
        memset(buffer, 0, 1024);

        printf("Enter message: ");
        // 使用 fgets 讓你可以輸入鍵盤打的字
        fgets(buffer, 1024, stdin);

        // (選用) 處理 fgets 會多吃一個換行符號的問題，把它拿掉
        buffer[strcspn(buffer, "\n")] = 0;
        // 【新增這裡】如果輸入長度為 0 (代表只按了 Enter)，就跳過這次迴圈
            if (strlen(buffer) == 0) {
                continue;
            }

        // 如果輸入 "exit" 就跳出迴圈，結束程式
        if (strcmp(buffer, "exit") == 0) {
            printf("Exiting...\n");
            break;
        }

        // 傳送訊息給 Server
        send(sock, buffer, strlen(buffer), 0);

        // --- 這裡可以選擇要不要讀 Server 的回覆 ---
        // 為了簡單測試，我們這邊先不讀 Server 回傳的東西
        // 因為如果 Server 沒回傳，Client 卡在這裡 read 會動不了
        // ----------------------------------------
    }

    // 4. 關閉 Socket
    close(sock);
    return 0;
}