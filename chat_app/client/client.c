#include "client.h"

#define SERVER_IP "127.0.0.1" // 服务器IP地址
#define SERVER_PORT 10005     // 服务器端口

int client_fd;                                    // 客户端套接字
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // 互斥锁
char session_token[64];                           // 会话标识符
Polling polling;
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
    printf("1.登录 2.创建用户3.添加好友或删除4.处理好友请求\n5.发送私聊消息6.创建群或者删除7.邀请好友入群或者踢人\n8.处理群聊邀请.9.退出10.发送群聊消息");
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
            request=build_group_message();
            len = sizeof(GroupMessage);
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

    char buffer[2048];
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
    scanf("%s",request->message);

    strncpy(request->session_token, session_token, TOKEN_LEN - 1);
    request->session_token[TOKEN_LEN - 1] = '\0';

    request->length = htonl(sizeof(GroupMessage));
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

/*
void send_polling(void *arg)
{

    polling.length = htonl(sizeof(polling));
    polling.request_code = htonl(REQUEST_POLLING);
    strncpy(&polling.token, session_token, sizeof(session_token) - 1);
    polling.token[sizeof(session_token) - 1] = '\0';
    while (1)
    {
        pthread_mutex_lock(&lock);
        send(client_fd, &polling, sizeof(polling), 0);
        pthread_mutex_unlock(&lock);
        sleep(5);
    }
}
*/
