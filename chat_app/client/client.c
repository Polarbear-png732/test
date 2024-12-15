#include "client.h"

#define SERVER_IP "127.0.0.1" // 服务器IP地址
#define SERVER_PORT 10005     // 服务器端口

int client_fd;                                    // 客户端套接字
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // 互斥锁
char session_token[64];                           // 会话标识符
Polling polling;
char file_name[256];
int main()
{
    // 初始化客户端
    init_client();
    pthread_t send_thread, receive_thread, polling_pthread;
    // 创建线程发送请求
    if (pthread_create(&send_thread, NULL, send_request, NULL) != 0)
    {
        perror("创建发送线程失败");
        exit(1);
    }

    // 创建线程接收响应
    if (pthread_create(&receive_thread, NULL, receive_response, NULL) != 0)
    {
        perror("创建接收线程失败");
        exit(1);
    }
    /*
    sleep(20);                                //必须在登录之后才发送，否则服务器发生段错误
    if (pthread_create(&polling_pthread, NULL,send_polling, NULL) != 0) {
        perror("创建接收线程失败");
        exit(1);
    }*/
    // 等待线程结束
    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);
    // 关闭套接字
    close(client_fd);

    return 0;
}

// 发送请求函数
void *send_request(void *arg)
{

    int action;
    printf("1.登录 2.创建用户3.添加好友或删除4.处理好友请求\n5.发送私聊消息6.创建群或者删除7.邀请好友入群或者踢人\n8.处理群聊邀请.9.退出10.发送群聊消息11发送文件给好友\n");
    pthread_detach(pthread_self());
    while (1)
    {

        scanf("%d", &action);
        void *request = NULL;
        unsigned int len = 0;

        switch (action)
        {
        case 1:
            request = build_login_request();
            len = sizeof(LoginRequest);
            break;
        case 2:
            request = build_create_user_request();
            len = sizeof(CreateUser);
            break;
        case 3:
            request = build_friend_request();
            len = sizeof(FriendRequest);
            break;
        case 4:
            request = build_handle_friend_request();
            len = sizeof(HandleFriendRequest);
            break;
        case 5:
            request = build_private_message_request();
            len = sizeof(PrivateMessage);
            break;
        case 6:
            request = build_group_request();
            len = sizeof(GroupCreateRequest);
            break;
        case 7:
            request = build_invite_request();
            len = sizeof(InviteRequest);
            break;
        case 8:
            request = build_handle_group_request();
            len = sizeof(InviteRequest);
            break;
        case 9:
            exit_client();
            break;
        case 10:
            request = build_group_message();
            len = sizeof(GroupMessage);
            break;
        case 11:
            request = build_file_transfer_req();
            len = sizeof(FileTransferRequest);
            break;
        default:
            printf("无效的操作\n");
            continue;
        }

        if (request != NULL)
        {
            pthread_mutex_lock(&lock);
            printf("Sending request: length=%u, request_code=%u\n", ntohl(*(unsigned int *)request), ntohl(*(unsigned int *)(request + sizeof(unsigned int))));
            send(client_fd, request, len, 0);
            pthread_mutex_unlock(&lock);
            free(request); // 释放动态分配的内存
        }
    }
}

// 接收响应函数
void *receive_response(void *arg)
{

    char buffer[BUFSIZE];
    unsigned int req_length;
    unsigned int size_len = sizeof(req_length);
    while (1)
    {
        if (recv_full(client_fd, buffer, size_len) == 0)
        {
            break;
        } // 先接收报文长度
        req_length = ntohl(*(unsigned int *)buffer);                    // 将接收到的报文长度从网络字节序转换为主机字节序
        recv_full(client_fd, buffer + size_len, req_length - size_len); // 接收剩余的数据
        unsigned int response_code = ntohl(*(unsigned int *)(buffer + size_len));

        switch (response_code)
        {
        case RESPONSE_LOGIN:
        {
            LoginResponse *response = (LoginResponse *)buffer;
            printf("Login response:\n");
            printf("Status Code: %u\n", ntohl(response->status_code));
            strncpy(session_token, response->session_token, sizeof(session_token) - 1);
            session_token[sizeof(session_token) - 1] = '\0';
            printf("%s\n", session_token);
            break;
        }
        case SIMPLE_RESPONSE:
        {
            SimpleResponse *resp = (SimpleResponse *)buffer;
            printf("Server Response: %u\n", ntohl(resp->status_code));
            break;
        }
        case RESPONSE_FILE_ACK:
        {
            printf("第%u块，确认开始传输\n", *(unsigned *)(buffer + 8)); // 检查是否进入该 case
            file_transfer(buffer);
            break;
        }
        case REQUEST_FILE_TRANSFER:
        {
            printf("开始接收文件！ \n");
            file_recv(buffer);
            break;
        }
        default:
        {
            FeedbackMessage *message = (FeedbackMessage *)buffer;
            printf("%s\n", message->message);
            break;
        }
        }
    }
    pthread_detach(pthread_self());
    return NULL;
}

void exit_client()
{
    unsigned int len = sizeof(ClientExit);
    ClientExit *exit = (ClientExit *)malloc(len);
    exit->length = htonl(len);
    exit->request_code = htonl(CLIENT_EXIT);
    send(client_fd, exit, len, 0);
    free(exit);
    pthread_mutex_lock(&lock);
    close(client_fd); // 确保socket资源释放
    pthread_mutex_unlock(&lock);
    pid_t pid = getpid();
    kill(pid, SIGKILL);
}
// 构造请求函数实现
LoginRequest *build_login_request()
{
    LoginRequest *request = malloc(sizeof(LoginRequest));
    if (!request)
    {
        perror("内存分配失败");
        exit(1);
    }
    request->request_code = htonl(REQUEST_LOGIN);
    printf("请输入用户名: ");
    scanf("%s", request->username);
    printf("请输入密码: ");
    scanf("%s", request->password);
    request->length = htonl(sizeof(LoginRequest));
    return request;
}

CreateUser *build_create_user_request()
{
    CreateUser *request = malloc(sizeof(CreateUser));
    if (!request)
    {
        perror("内存分配失败");
        exit(1);
    }
    request->request_code = htonl(REQUEST_CREATEUSER);
    printf("请输入用户名: ");
    scanf("%s", request->username);
    printf("请输入密码: ");
    scanf("%s", request->password);
    request->length = htonl(sizeof(CreateUser));
    return request;
}

FriendRequest *build_friend_request()
{
    FriendRequest *request = malloc(sizeof(FriendRequest));
    if (!request)
    {
        perror("内存分配失败");
        exit(1);
    }
    request->request_code = htonl(REQUEST_ADD_FRIEND);
    printf("添加好友输入1，删除好友输入0\n");
    int action;
    scanf("%d", &action);
    printf("请输入好友名字：\n");
    scanf("%s", request->friend_username);
    request->action = htonl(action);
    strncpy(request->session_token, session_token, sizeof(session_token) - 1);
    request->session_token[sizeof(session_token) - 1] = '\0';
    request->length = htonl(sizeof(FriendRequest));
    return request;
}

HandleFriendRequest *build_handle_friend_request()
{
    HandleFriendRequest *request = malloc(sizeof(HandleFriendRequest));
    if (!request)
    {
        perror("内存分配失败");
        exit(1);
    }
    request->request_code = htonl(REQUEST_HANDELE_ADD);
    printf("处理哪一个好友请求？请输入对方名称：\n");
    scanf("%s", request->friend_username);
    printf("是否接受好友请求，是输入1，否输入0\n");
    int action;
    scanf("%d", &action);
    request->action = htonl(action);
    strncpy(request->session_token, session_token, sizeof(session_token) - 1);
    request->session_token[sizeof(session_token) - 1] = '\0';
    request->length = htonl(sizeof(HandleFriendRequest));
    return request;
}

PrivateMessage *build_private_message_request()
{
    PrivateMessage *request = malloc(sizeof(PrivateMessage));
    if (!request)
    {
        perror("内存分配失败");
        exit(1);
    }
    request->request_code = htonl(REQUEST_PRIVATE_MESSAGE);
    printf("请输入接收消息的用户：\n");
    scanf("%s", request->receiver_username);
    printf("请输入消息：\n");
    scanf("%s", request->message);
    strncpy(request->session_token, session_token, TOKEN_LEN - 1);
    request->session_token[TOKEN_LEN - 1] = '\0';
    request->length = htonl(sizeof(PrivateMessage));
    return request;
}

GroupCreateRequest *build_group_request()
{
    GroupCreateRequest *request = malloc(sizeof(GroupCreateRequest));
    if (!request)
    {
        perror("内存分配失败");
        exit(1);
    }
    request->request_code = htonl(REQUEST_CREATE_GROUP);
    printf("创建群输入1, 删除群输入0\n");
    int action;
    scanf("%d", &action);
    printf("请输入群聊名称：\n");
    scanf("%s", request->group_name);
    request->action = htonl(action);
    strncpy(request->session_token, session_token, TOKEN_LEN - 1);
    request->session_token[TOKEN_LEN - 1] = '\0';
    request->length = htonl(sizeof(GroupCreateRequest));
    return request;
}

InviteRequest *build_invite_request()
{
    InviteRequest *request = malloc(sizeof(InviteRequest));
    if (!request)
    {
        perror("内存分配失败");
        exit(1);
    }
    request->request_code = htonl(REQUEST_INVITE_TOGROUP);
    printf("邀请好友进群输入1，删除群成员输入0\n");
    int action;
    scanf("%d", &action);
    printf("请输入群聊名称：\n");
    scanf("%s", request->group_name);
    printf("请输入好友名字：\n");
    scanf("%s", request->friendname);
    request->action = htonl(action);
    strncpy(request->session_token, session_token, TOKEN_LEN - 1);
    request->session_token[TOKEN_LEN - 1] = '\0';
    request->length = htonl(sizeof(InviteRequest));
    return request;
}

HandleGroupInvite *build_handle_group_request()
{
    HandleGroupInvite *request = malloc(sizeof(HandleGroupInvite));

    printf("请输入要处理的群聊名称：\n");
    scanf("%s", request->group_name);
    printf("同意输入1，拒绝0\n");
    int action;
    scanf("%d", &action);

    strncpy(request->session_token, session_token, TOKEN_LEN - 1);
    request->session_token[TOKEN_LEN - 1] = '\0';
    request->request_code = htonl(REQUEST_HANDLE_GROUP);
    request->action = htonl(action);

    request->length = htonl(sizeof(HandleGroupInvite));
    return request;
}

GroupMessage *build_group_message()
{
    GroupMessage *request = malloc(sizeof(GroupMessage));

    request->request_code = htonl(REQUEST_GROUP_MESSAGE);

    printf("请输入群聊 ID：\n");
    unsigned int group_id;
    scanf("%u", &group_id);
    request->group_id = htonl(group_id);

    printf("请输入消息内容：\n");
    scanf("%s", request->message);

    strncpy(request->session_token, session_token, TOKEN_LEN - 1);
    request->session_token[TOKEN_LEN - 1] = '\0';

    request->length = htonl(sizeof(GroupMessage));
    return request;
}

FileTransferRequest *build_file_transfer_req()
{

    FileTransferRequest *request = malloc(sizeof(FileTransferRequest));

    request->request_code = htonl(REQUEST_FILE_TRANSFER);

    request->length = htonl(sizeof(FileTransferRequest));

    printf("请输入好友名称： \n");
    scanf("%s", request->receiver_username);

    printf("请输入文件名称（绝对路径）：\n");
    scanf("%s", request->file_name);
    strcpy(file_name, request->file_name);
    long file_size = get_file_size(request->file_name);
    request->file_size = htonl((unsigned int)file_size);
    char *filename;
    // 使用 strrchr 找到最后一个斜杠的位置
    filename = strrchr(request->file_name, '/');

    if (filename)
    {
        // 跳过斜杠，指向文件名
        filename++;
        strcpy(request->file_name, filename);
    } // 如果没有找到斜杠，整个路径就是文件名

    strncpy(request->session_token, session_token, TOKEN_LEN - 1);
    request->session_token[TOKEN_LEN - 1] = '\0';
    printf("%s\n", request->file_name);
    return request;
}
// 初始化客户端
void init_client()
{
    struct sockaddr_in server_addr;

    // 创建套接字
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0)
    {
        perror("创建套接字失败");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // 连接服务器
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("连接服务器失败");
        close(client_fd);
        exit(1);
    }

    printf("成功连接到服务器 %s:%d\n", SERVER_IP, SERVER_PORT);
}

int recv_full(int sock, void *buf, size_t len)
{

    size_t total_received = 0;  // 已接收字节数
    ssize_t bytes_received = 0; // 每次调用 recv 接收到的字节数

    while (total_received < len)
    {
        bytes_received = recv(sock, (char *)buf + total_received, len - total_received, 0);
        if (bytes_received < 0)
        {
            if (errno == EINTR)
            {
                // 如果是EINTR错误，继续尝试接收
                continue;
            }
            else
            {
                perror("Error in recv");
                return -1; // 接收失败
            }
        }
        else if (bytes_received == 0)
        {
            fprintf(stderr, "Connection closed by peer\n");
            return 0; // 对端关闭连接
        }
        total_received += bytes_received; // 累计已接收字节数
    }

    return 1; // 成功接收完整数据
}

long get_file_size(const char *filename)
{
    FILE *file = fopen(filename, "rb"); // 以二进制只读模式打开文件
    if (!file)
    {
        perror("Error opening file");
        return -1;
    }
    fseek(file, 0, SEEK_END);     // 移动文件指针到文件末尾
    long file_size = ftell(file); // 获取当前文件指针的位置，即文件大小
    fclose(file);                 // 关闭文件
    return file_size;
}

void file_transfer(char *buffer)
{
    long file_size = get_file_size(file_name);
    float num = (float)file_size / (BUFSIZE - 12);
    printf("文件大小：%ld,分为% .2f次传输\n", file_size, num);
    FILE *file = fopen(file_name, "rb");
    int block_number = 1;

    while (1)
    {
        FileTransferResponse *ack = (FileTransferResponse *)buffer;
        ack->block_number = htonl(block_number);
        ack->request_code = htonl(RESPONSE_FILE_ACK);
        ack->length = htonl(12);
        size_t bytes_read;
        bytes_read = fread(buffer + 12, 1, BUFSIZE - 12, file);
        pthread_mutex_lock(&lock);

        ssize_t sent;
        sent = send(client_fd, buffer, bytes_read + 12, 0);

        printf("sent:%ld\n", sent);

        recv(client_fd, ack, 12, 0);
        printf("第%u块已经确认\n", ntohl(ack->block_number));
        pthread_mutex_unlock(&lock);
        block_number++;
        if (feof(file))
        {
            printf("End of file reached.\n");
            break;
        }
    }
    fclose(file);
}

void file_recv(char *buffer)
{
    FileTransferRequest *req = (FileTransferRequest *)buffer;
    unsigned int file_size = ntohl(req->file_size);
    char filename[256];
    strcpy(filename, req->file_name);

    FILE *file = fopen(filename, "wb");
    FileTransferResponse *ack = (FileTransferResponse *)buffer;

    ack->block_number = 0;
    ack->length = htonl(12);
    ack->request_code = htonl(RESPONSE_FILE_ACK); // 发送确认之后开始传文件
    send(client_fd, ack, 12, 0);

    int total_write = 0;
    while (1)
    {
        ssize_t bytes_recv = recv(client_fd, buffer, BUFSIZE, 0);

        size_t bytes_write;
        bytes_write = fwrite(buffer + 12, 1, bytes_recv - 12, file);
        total_write += bytes_write;
        printf("total_write%d\n", total_write);
        if (bytes_write == bytes_recv - 12)
        { // 写入成功，发送ack，开始下一轮接收
            ack->block_number = *(unsigned int *)(buffer + 8);
            printf("第%u块接收完成\n", ntohl(ack->block_number));
            send(client_fd, ack, 12, 0);
        }
        if (total_write == file_size)
        {
            printf("接收完毕\n");
            break;
        }
    }
    fclose(file);
}
