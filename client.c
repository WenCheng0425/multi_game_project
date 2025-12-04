#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char *hello = "Hello from Client!";
    char buffer[1024] = {0};

    // 1. 建立 Socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 將 IP 位址從字串 "127.0.0.1" 轉為二進位格式
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 2. Connect (連線)
    // 嘗試連線到 Server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    // 連線成功！傳送訊息給 Server
    send(sock, hello, strlen(hello), 0);
    printf("Hello message sent\n");

    // 讀取 Server 回傳的訊息
    read(sock, buffer, 1024);
    printf("Message from Server: %s\n", buffer);

    // 3. 關閉 Socket
    close(sock);
    return 0;
}