#include "server.h"

// 初始化服务器套接字
int init_server()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1; // 设置SO_REUSEADDR选项的值
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENT) == -1)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

int init_file_transfer_server()
{
   int file_transfer_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (file_transfer_server_fd == -1)
    {
        perror("File transfer socket creation failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;

    if (setsockopt(file_transfer_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(FILE_TRANSFER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(file_transfer_server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("File transfer bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(file_transfer_server_fd, MAX_CLIENT) == -1)
    {
        perror("File transfer listen failed");
        exit(EXIT_FAILURE);
    }

    return file_transfer_server_fd;
}
