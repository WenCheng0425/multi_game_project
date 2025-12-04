#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080        // 定義我們要使用的 Port 號
#define BUFFER_SIZE 1024 // 定義緩衝區大小

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    char *hello = "Hello from Server!"; // 伺服器要傳送的訊息

    // 1. 建立 Socket (IPv4, TCP)
    // AF_INET = IPv4, SOCK_STREAM = TCP
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 設定 Address 結構 (告訴系統我們要聽哪個 IP 和 Port)
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 聽所有的網卡介面 (0.0.0.0)
    address.sin_port = htons(PORT);       // 把 Port 轉成網路用的格式

    // 2. Bind (綁定)
    // 把 Socket 綁定到我們剛剛設定的 Port 上
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // 3. Listen (監聽)
    // 開始等待連線，同時最多允許 3 個等待中的連線
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d...\n", PORT);

    // 4. Accept (接受連線)
    // 程式會卡在這裡(阻塞)，直到有 Client 連上來
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    // 連線成功！讀取 Client 傳來的資料
    read(new_socket, buffer, BUFFER_SIZE);
    printf("Message from Client: %s\n", buffer);

    // 傳送訊息回 Client
    send(new_socket, hello, strlen(hello), 0);
    printf("Hello message sent\n");

    // 5. 關閉 Socket
    close(new_socket);
    close(server_fd);
    return 0;
}