#include "server.h"

struct session_name session_table[10];
int session_table_index = 0;
ClientEventQueueMap clientfd_queues_map[MAX_CLIENTS];
int map_index = 0;
pthread_mutex_t client_queues_lock =PTHREAD_MUTEX_INITIALIZER;
Group  groups[10]={0};

int main()
{
    // 初始化
    int server_fd = init_server();
    printf("Server started, waiting for connections...\n");
    init_client_queues();

    // 循环接受客户端连接，并为每个客户端创建一个新线程
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            perror("Failed to accept connection");
            continue;
        }

        // 创建一个线程处理该客户端
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, (void *)&client_fd);
    }

    return 0;
}
