#include "client.h"

#define SERVER_IP "127.0.0.1" // 服务器IP地址
#define SERVER_PORT 10005     // 服务器端口

int client_fd;                                    // 客户端套接字
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // 互斥锁
char session_token[64];                           // 会话标识符
Polling polling;
int main()
{
    pthread_t send_thread, receive_thread, polling_pthread;

    // 初始化客户端
    init_client();

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
    //pthread_join(polling_pthread, NULL);

    // 关闭套接字
    close(client_fd);

    return 0;
}

// 发送请求函数
void *send_request(void *arg)
{
    int action;
    printf("1.登录 2.创建用户3.添加好友或删除4.处理好友请求5.发送私聊消息6.创建群或者删除\n");

    while (1)
    {
        scanf("%d", &action);
        switch (action)
        {
        case 1:
        {
            // 登录请求
            LoginRequest login_request;
            login_request.request_code = htonl(REQUEST_LOGIN);

            printf("请输入用户名: ");
            scanf("%s", login_request.username);
            printf("请输入密码: ");
            scanf("%s", login_request.password);

            login_request.length = sizeof(login_request);
            login_request.length = htonl(login_request.length);
            pthread_mutex_lock(&lock);
            send(client_fd, &login_request, sizeof(login_request), 0);
            pthread_mutex_unlock(&lock);
            break;
        }
        case 2:
        {
            // 创建用户请求
            CreateUser create_user_request;
            create_user_request.request_code = htonl(REQUEST_CREATEUSER);
            printf("请输入用户名: ");
            scanf("%s", create_user_request.username);
            printf("请输入密码: ");
            scanf("%s", create_user_request.password);

            create_user_request.length = sizeof(create_user_request);
            create_user_request.length = htonl(create_user_request.length);
            pthread_mutex_lock(&lock);
            send(client_fd, &create_user_request, sizeof(create_user_request), 0);
            pthread_mutex_unlock(&lock);
            break;
        }
        case 3:
        {
            FriendRequest add_friend;
            unsigned len = sizeof(add_friend);

            add_friend.length = htonl(len);                                              // 报文长度
            add_friend.request_code = htonl(REQUEST_ADD_FRIEND);                         // 报文码
            strncpy(add_friend.session_token, session_token, sizeof(session_token) - 1); // 令牌
            add_friend.session_token[sizeof(session_token) - 1] = '\0';
            int action;
            printf("添加好友输入1，删除好友输入0\n");
            scanf("%d", &action);
            printf("请输入好友名字：\n");
            scanf("%s", add_friend.friend_username); // 好友名字

            add_friend.action = htonl(action);
            pthread_mutex_lock(&lock);
            send(client_fd, &add_friend, len, 0);
            pthread_mutex_unlock(&lock);
            break;
        }
        case 4:
        {
            HandleFriendRequest handle_friend_req;
            unsigned len = sizeof(handle_friend_req);

            handle_friend_req.length = htonl(len);                                              // 报文长度
            handle_friend_req.request_code = htonl(REQUEST_HANDELE_ADD);                        // 报文码
            strncpy(handle_friend_req.session_token, session_token, sizeof(session_token) - 1); // 令牌
            handle_friend_req.session_token[sizeof(session_token) - 1] = '\0';

            printf("处理哪一个好友请求？请输入对方名称：\n");
            scanf("%s", handle_friend_req.friend_username);
            printf("是否接受好友请求，是输入1，否输入0\n");
            int action;
            scanf("%d", &action);
            handle_friend_req.action = htonl(action);
            pthread_mutex_lock(&lock);
            send(client_fd, &handle_friend_req, len, 0);
            pthread_mutex_unlock(&lock);
            break;
        }
                case 5:
        {
            PrivateMessage message;
            int len=sizeof(message);
            message.length=htonl(len);
            message.request_code=htonl(REQUEST_PRIVATE_MESSAGE);
            strncpy(message.session_token,session_token,TOKEN_LEN-1);
            message.session_token[TOKEN_LEN-1]='\0';
            printf("请输入接收消息的用户：\n");
            scanf("%s",message.receiver_username);
            printf("请输入消息：\n");
            scanf("%s",message.message);

            pthread_mutex_lock(&lock);
            send(client_fd,&message,len,0);
            pthread_mutex_unlock(&lock);
            break;
        }
        case 6:
        {
            GroupCreateRequest req;
            char groupname[64];
            int len=sizeof(req);
            int action;
            req.length=htonl(len);
            req.request_code=htonl(REQUEST_CREATE_GROUP);
            printf("创建群输入1,删除群输入0\n");
            scanf("%d",&action);
            printf("请输入群聊名称：\n");
            scanf("%s",groupname);
            strncpy(req.group_name,groupname,sizeof(groupname)-1);
            req.group_name[sizeof(groupname)-1]='\0';

            strncpy(req.session_token,session_token,TOKEN_LEN-1);
            req.session_token[TOKEN_LEN-1]='\0';
            send(client_fd,&req,len,0);

        }
        case 7:
        {
            InviteRequest req;
            char groupname[64];
            int len=sizeof(req);
            int action;
            req.length=htonl(len);
            req.request_code=htonl(REQUEST_INVITE_TOGROUP);
            printf("添加好友输入1，删除好友输入0\n");
            scanf("%d",&action);
            printf("请输入群聊名称：\n");
            scanf("%s",groupname);
            strncpy(req.group_name,groupname,sizeof(groupname)-1);
            req.group_name[sizeof(groupname)-1]='\0';
            printf("请输入好友名字：\n");
            scanf("%s",req.friendname);
            req.action=htonl(action);
            strncpy(req.session_token,session_token,TOKEN_LEN-1);
            req.session_token[TOKEN_LEN-1]='\0';
            send(client_fd,&req,len,0);
            break;
        }
        default:
            printf("无效的操作\n");
            break;
        }
    }

    return NULL;
}

// 接收响应函数
void *receive_response(void *arg)
{

    char buffer[2048];
    unsigned int req_length;
    unsigned int size_len = sizeof(req_length);
    while (1)
    {

        recv_full(client_fd, buffer, size_len);                         // 先接收报文长度
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
    return NULL;
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

int recv_full(int sock, void *buf, size_t len) {
    size_t total_received = 0;  // 已接收字节数
    ssize_t bytes_received = 0; // 每次调用 recv 接收到的字节数

    while (total_received < len) {
        bytes_received = recv(sock, (char *)buf + total_received, len - total_received, 0);
        if (bytes_received < 0) {
            if (errno == EINTR) {
                // 如果是EINTR错误，继续尝试接收
                continue;
            } else {
                perror("Error in recv");
                return -1; // 接收失败
            }
        } else if (bytes_received == 0) {
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
