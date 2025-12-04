#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

int main() {
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
                    // 【修正 3】Server 端再次去換行 (防止 Client 沒弄乾淨)
                    buffer[strcspn(buffer, "\n")] = 0;
                    buffer[strcspn(buffer, "\r")] = 0;

                    // 如果去掉換行後是空的，就不印了 (忽略純按 Enter 的狀況)
                    if (strlen(buffer) == 0) {
                        continue;
                    }

                    printf("Received from Client %d: %s\n", i, buffer);
                    
                    // 回傳訊息
                    send(sd, "Server received your message\n", 29, 0);
                }
            }
        }
    }
    return 0;
}