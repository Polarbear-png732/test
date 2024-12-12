#include "server.h"

__thread pthread_mutex_t friend_lock = PTHREAD_MUTEX_INITIALIZER;
__thread char **friends = NULL;
__thread char **online_friends = NULL; // 在线好友列表与好友列表和对应的好友数量
__thread int friend_count = 0;
__thread int online_friend_count = 0;
__thread struct session_name client_session;

EventQueue *queue = NULL;
char online_members[MAX_MEMBERS][MAX_USERNAME_LENGTH] = {0};
// 请求处理函数
void *handle_client(void *arg)
{
    pthread_t main_thread = pthread_self();
    pthread_t *queue_pthread = (pthread_t *)malloc(sizeof(pthread_t));

    queue = init_event_queue();

    int client_fd = *(int *)arg;
    char buffer[1024];
    MYSQL *conn = db_connect();
    if (!conn)
    {
        printf("数据库连接失败！\n");
        close(client_fd);
        return NULL;
    }
    get_groupmember(groups, conn);
    unsigned int req_length;
    unsigned int size_len = sizeof(unsigned int);

    // 设置接收超时时间为 10 秒
    struct timeval timeout;
    timeout.tv_sec = 300; // 超时时间秒数
    timeout.tv_usec = 0;  // 微秒部分
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

    printf("client connection!\n");
    while (1)
    {
        if (recv_full(client_fd, buffer, size_len) <= 0) // 接收报文长度
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                printf("客户端 10 秒未发送请求，下线处理。\n");
            }
            else
            {
                printf("接收报文长度失败或客户端断开！\n");
                on_off_push(0, friends);
                delete_session(client_session.session);
            }
            break;
        }
        else
        {
            printf("Received length field: %u bytes\n", ntohl(*(unsigned int *)buffer));
        }
        req_length = ntohl(*(unsigned int *)buffer);

        if (recv_full(client_fd, buffer + size_len, req_length - size_len) > 0) // 接收剩余的数据
        {
            printf("Received full message of %u bytes\n", req_length);
        }
        else
        {
            printf("Failed to receive full message\n");
        }
        // 解析请求类型
        unsigned int request_code = ntohl(*(unsigned int *)(buffer + size_len));
        switch (request_code)
        {
        case REQUEST_LOGIN:
            handle_login(client_fd, buffer, conn, queue_pthread);
            break;
        case REQUEST_CREATEUSER:
            handle_create_user(client_fd, buffer, conn);
            break;
        case REQUEST_ADD_FRIEND:
            handle_add_friend(client_fd, buffer, conn);
            break;
        case REQUEST_HANDELE_ADD:
            handle_accept_add(client_fd, buffer, conn);
            break;
        case REQUEST_POLLING:
        {
            Polling *p = (Polling *)buffer;
            int polling = find_session_index(0, p->token);
            // push_friend(client_fd, session_table[polling].username, conn);
            break;
        }
        case REQUEST_PRIVATE_MESSAGE:
            private_message(client_fd, buffer, conn);
            break;
        case REQUEST_CREATE_GROUP:
            create_group(client_fd, buffer, conn);
            break;
        case REQUEST_INVITE_TOGROUP:
            invite_to_group(client_fd, buffer, conn);
            break;
        case REQUEST_HANDLE_GROUP:
            handle_add_group(client_fd, buffer, conn);
            break;
        case CLIENT_EXIT:
            clietn_exit(queue_pthread);
            break;
        case REQUEST_GROUP_MESSAGE:
            group_message(client_fd, buffer, conn);
            break;
        default:
            printf("未知的请求代码: %u\n", request_code);
            break;
        }
    }
    // 清理资源
    mysql_close(conn);
    /*
    for (int i = 0; i < MAX_FRIENDS; i++)
    {
        free(friends[i]);
    }
    free(friends);
    // 释放 online_friends
    for (int i = 0; i < online_friend_count; i++)
    {
        free(online_friends[i]);
    }
    // free(online_friends);
    */
    close(client_fd);
    return NULL;
}

// 登录处理函数
void handle_login(int client_fd, char *buffer, MYSQL *conn, pthread_t *queue_pthread)
{
    LoginRequest *login_req = (LoginRequest *)buffer;

    // 查询数据库，验证用户名和密码
    char query[256];

    snprintf(query, sizeof(query), "SELECT * FROM users WHERE username='%s' AND password='%s'",
             login_req->username, login_req->password);
    MYSQL_RES *res = do_query(query, conn);
    if (mysql_num_rows(res) == 0)
    {
        send_message(client_fd, "Invalid username or password");
        mysql_free_result(res);
        return;
    }
    // 获取id
    MYSQL_ROW row = mysql_fetch_row(res);
    unsigned int user_id = atoi(row[0]);
    // 登录成功，生成会话标识符

    LoginResponse login_res;
    login_res.status_code = htonl(SUCCESS);
    login_res.length = htonl(sizeof(login_res));
    login_res.request_code = htonl(RESPONSE_LOGIN);
    generate_session_id(login_res.session_token);
    strcpy(login_res.offline_messages, "no offline messige");

    send(client_fd, &login_res, sizeof(login_res), 0);

    client_session.client_fd = client_fd; // 保存用户会话
    client_session.id = user_id;
    strncpy(client_session.username, login_req->username, MAX_USERNAME_LENGTH - 1);
    client_session.username[MAX_USERNAME_LENGTH - 1] = '\0';
    strncpy(client_session.session, login_res.session_token, TOKEN_LEN - 1);
    client_session.session[TOKEN_LEN - 1] = '\0';

    // 存储客户端文件描述符和事件队列的映射关系
    pthread_mutex_lock(&client_queues_lock);
    clientfd_queues_map[map_index].client_fd = client_fd;
    clientfd_queues_map[map_index].queue = queue;
    map_index++;
    pthread_mutex_unlock(&client_queues_lock);

    // 保存会话标识符与用户名和id的映射
    store_session(login_req->username, login_res.session_token, user_id, client_fd);
    mysql_free_result(res);
    push_friend(client_fd, login_req->username, conn);

    // 获取好友列表
    friends = get_friend_list(user_id, &friend_count, conn);
    push_fri_list(friends, friend_count, client_fd, conn);
    online_friends = get_online_friends(friends, &friend_count, &online_friend_count);
    printf("客户端：%s登录时在线好友数量%d\n", client_session.username, online_friend_count);

    pthread_t event_thread;

    event_pthread_arg *event_arg = (event_pthread_arg *)malloc(sizeof(event_pthread_arg));
    event_arg->online_friends = online_friends;
    event_arg->queue = queue;
    event_arg->online_friend_count = &online_friend_count;
    event_arg->friend_count = &friend_count;
    event_arg->friends = friends;
    // 创建线程处理事件队列
    pthread_create(&event_thread, NULL, process_events, (void *)event_arg);
    *queue_pthread = event_thread;

    // 上线时推送消息
    group_invite_push(client_fd, conn);
    offline_message_push(user_id, conn);
    on_off_push(1, friends);
}

// 数据库连接函数
MYSQL *db_connect()
{
    MYSQL *conn = mysql_init(NULL);
    if (!conn)
    {
        perror("MySQL initialization failed");
        exit(EXIT_FAILURE);
    }

    if (!mysql_real_connect(conn, "localhost", "polarbear", "polarbear", "chat_app", 0, NULL, 0))
    {
        perror("MySQL connection failed");
        exit(EXIT_FAILURE);
    }

    return conn;
}

// 存储会话标识符与用户名的映射
void store_session(const char *username, const char *session_token, unsigned int id, int client_fd)
{
    // 确保不会越界
    if (session_table_index < 10)
    {
        // 使用 strncpy 避免溢出
        strncpy(session_table[session_table_index].username, username, sizeof(session_table[session_table_index].username) - 1);
        session_table[session_table_index].username[sizeof(session_table[session_table_index].username) - 1] = '\0'; // 保证字符串以 '\0' 结尾

        strncpy(session_table[session_table_index].session, session_token, sizeof(session_table[session_table_index].session) - 1);
        session_table[session_table_index].session[sizeof(session_table[session_table_index].session) - 1] = '\0'; // 保证字符串以 '\0' 结尾

        session_table[session_table_index].id = id;
        session_table[session_table_index].client_fd = client_fd;
        session_table_index++;
    }
    else
    {
        // 如果会话表已满，输出错误信息或处理方式
        printf("Error: Session table is full.\n");
    }
}
// 保证接收完整
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
// 生成会话标识符
void generate_session_id(char *session_id)
{
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz123456789&*%$#*)(*:)";

    unsigned int charset_size = sizeof(charset);

    for (int i = 0; i < 64; i++)
    {
        int random_index = rand() % charset_size;
        session_id[i] = charset[random_index];
    }

    session_id[63] = '\0'; // 确保字符串以 NULL 结尾
}
// 发送错误消息,也用来发送其他消息
void send_message(int sockfd, const char *feedback)
{
    FeedbackMessage response;
    response.length = htonl(sizeof(response));
    response.request_code = htonl(RESPONSE_MESSAGE);
    strncpy(response.message, feedback, sizeof(response.message) - 1);
    // 确保字符串以 null 结尾
    response.message[sizeof(response.message) - 1] = '\0';

    // 发送响应
    send(sockfd, &response, ntohl(response.length), 0);
    return;
}

// 将在线用户从全局会话表中删除
int delete_session(const char *session)
{
    for (int i = 0; i < session_table_index; i++)
    {
        if (strcmp(session_table[i].session, session) == 0)
        {
            // 删除会话，将后续会话前移
            for (int j = i; j < session_table_index - 1; j++)
            {
                session_table[j] = session_table[j + 1];
            }
            session_table_index--;
            return 1; // 成功删除
        }
    }
    return 0; // 未找到会话
}

void send_simple(int sockfd, int success)
{
    unsigned int len_response = sizeof(SimpleResponse);
    SimpleResponse *response = (SimpleResponse *)malloc(len_response);
    response->length = htonl(len_response);
    response->request_code = htonl(SIMPLE_RESPONSE);
    if (success == SUCCESS)
    {
        response->status_code = htonl(SUCCESS);
        send(sockfd, response, len_response, 0);
        free(response);
        return;
    }
    response->status_code = htonl(FAIL);
    send(sockfd, response, len_response, 0);
    free(response);
    return;
}

// 用户上线推送好友列表
void push_fri_list(char **list, int count, int client_fd, MYSQL *conn)
{

    char fri_list[255] = "好友列表：\n";

    for (int i = 0; i < count; i++)
    {
        strncat(fri_list, list[i], MAX_USERNAME_LENGTH - 1); // 拼接好友用户名
        strncat(fri_list, ":", 1);
        int found = 0; // 标志变量，初始值为未找到
        for (int j = 0; j < session_table_index; j++)
        {
            if (strcmp(session_table[j].username, list[i]) == 0)
            {
                strncat(fri_list, " online", 16);
                found = 1;
                break;
            }
        }
        if (!found)
        {
            strncat(fri_list, " offline", 16); // 添加 "offline" 到字符串末尾
        }
        strncat(fri_list, "\n", 1);
    }
    send_message(client_fd, fri_list); // 发送给客户端
    return;
}
// 发送私聊消息
void private_message(int client, char *buffer, MYSQL *conn)
{
    PrivateMessage *message = (PrivateMessage *)buffer;
    int recver_fd;
    int online = 0;
    int i = 0;
    for (i; i < session_table_index; i++)
    {
        if (strcmp(session_table[i].username, message->receiver_username) == 0)
        {
            online = 1;
            recver_fd = session_table[i].client_fd;
        }
    }
    i = find_session_index(0, message->session_token);

    char send_online[512];
    snprintf(send_online, sizeof(send_online), "%s:%s", session_table[i].username, message->message);

    // 在线直接发送，否则插入到数据库
    if (online)
    {
        send_message(recver_fd, send_online);
        return;
    }

    int sender_id = find_uid(message->session_token);
    int recver_id = find_id_mysql(message->receiver_username, conn);

    char query[512];
    snprintf(query, sizeof(query), "INSERT INTO offline_messages (sender_id,receiver_id,message) "
                                   "VALUES ('%d', '%d','%s');",
             sender_id, recver_id, message->message);
    do_query(query, conn);

    return;
}

MYSQL_RES *do_query(char *query, MYSQL *conn)
{
    if (mysql_query(conn, query))
    {
        fprintf(stderr, "Query Error: %s\n", mysql_error(conn));
        return NULL;
    }

    if (mysql_field_count(conn) == 0)
    {
        // Query did not return a result set
        my_ulonglong affected_rows = mysql_affected_rows(conn);
        if (affected_rows == (my_ulonglong)-1)
        {
            fprintf(stderr, "Error fetching affected rows: %s\n", mysql_error(conn));
        }
        else
        {
            printf("Query OK, %llu rows affected\n", affected_rows);
        }
        return NULL;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    if (result == NULL)
    {
        fprintf(stderr, "Store Result Error: %s\n", mysql_error(conn));
        return NULL;
    }

    return result;
}
// 用户上线时获取在线好友列表
char **get_online_friends(char **friends, int *friend_count, int *online_friend_count)
{
    int i, j, k = 0;                                                    // k用于新数组索引
    char **new_friends = (char **)malloc(sizeof(char *) * MAX_FRIENDS); // 新的动态数组

    for (i = 0; i < MAX_FRIENDS; i++)
    {
        new_friends[i] = (char *)malloc(MAX_USERNAME_LENGTH * sizeof(char));
    }

    for (i = 0; i < *friend_count; i++)
    {
        for (j = 0; j < session_table_index; j++)
        {
            if (strcmp(friends[i], session_table[j].username) == 0)
            {
                strcpy(new_friends[k], friends[i]);
                k++;
                break; // 匹配上了直接跳出内层循环
            }
        }
    }

    *online_friend_count = k;
    return new_friends;
}
// 从数据库中获取用户好友列表
char **get_friend_list(int user_id, int *friend_count, MYSQL *conn)
{
    // 分配结果数组
    char **friend_list = malloc(MAX_FRIENDS * sizeof(char *));
    for (int i = 0; i < MAX_FRIENDS; i++)
    {
        friend_list[i] = malloc(MAX_USERNAME_LENGTH * sizeof(char));
    }
    MYSQL_RES *res;
    MYSQL_ROW row;
    // 初始化计数
    *friend_count = 0;

    // 构建 SQL 查询
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT u.username AS friend_username "
             "FROM friends f "
             "JOIN users u "
             "ON (f.friend_id = u.id AND f.user_id = %d AND f.status = 'accepted') "
             "OR (f.user_id = u.id AND f.friend_id = %d AND f.status = 'accepted');",
             user_id, user_id);

    res = do_query(query, conn);

    if (res == NULL)
    {
        fprintf(stderr, "mysql_store_result() failed: %s\n", mysql_error(conn));

        exit(EXIT_FAILURE);
    }

    // 解析结果
    while ((row = mysql_fetch_row(res)))
    {
        if (*friend_count >= MAX_FRIENDS)
            break; // 防止超出数组范围

        // 将用户名复制到结果数组中
        strncpy(friend_list[*friend_count], row[0], MAX_USERNAME_LENGTH);
        (*friend_count)++;
    }

    // 释放资源
    mysql_free_result(res);

    return friend_list;
}
// 上下线推送消息给在线好友
void on_off_push(int on, char **friends)
{

    Event event;
    memset(&event, 0, sizeof(Event));
    event.event_type = on ? 1 : 0; // 1 = 上线, 0 = 下线
    strncpy(event.username, client_session.username, MAX_USERNAME_LENGTH);
    char online_push[128];
    memset(online_push, 0, sizeof(online_push));
    strcat(online_push, client_session.username);
    if (on == 1)
    {
        strcat(online_push, "已上线！");
    }
    else
    {
        strcat(online_push, "已下线！");
    }

    int i;
    if (online_friends)
    {
        // 遍历并处理在线好友
        for (i = 0; i < online_friend_count; i++)
        {
            int index = find_session_index(1, online_friends[i]);
            send_message(session_table[index].client_fd, online_push); // 向好友客户端发送上线消息

            EventQueue *queue = find_queue(session_table[index].client_fd); // 向好友服务器进程的事件队列中插入上线事件
            push_event(queue, event);
        }
        return;
    }
}
// 离线消息推送
void offline_message_push(unsigned int user_id, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    snprintf(query, sizeof(query),
             "SELECT u.username AS sender, om.message, om.created_at "
             "FROM offline_messages om "
             "JOIN users u ON om.sender_id = u.id "
             "WHERE om.receiver_id = '%d'; ",
             user_id);
    result = do_query(query, conn);
    char message[1024];
    while ((row = mysql_fetch_row(result)))
    {
        strcat(message, row[0]);
        strcat(message, ":");
        strcat(message, row[1]);
        strcat(message, "\n");
    }
    mysql_free_result(result);
    send_message(client_session.client_fd, message);
    snprintf(query, sizeof(query), "delete from offline_messages where receiver_id='%u'", user_id);
    do_query(query, conn);
    return;
}
// 检查用户是否在线
int online_query(char *name)
{
    int i = 0;
    for (i; i < session_table_index; i++)
    {
        if (strcmp(session_table[i].username, name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// 根据会话标识符找到在线用户id
int find_uid(char *token)
{

    int i = find_session_index(0, token);
    int id = session_table[i].id;
    return id;
}
// 根据会话标识符找到在线用户在全局会话表中的索引
int find_session_index(int search_by, const char *value)
{
    for (int i = 0; i < session_table_index; i++)
    {
        if (search_by == 0)
        { // 按 session 字段查找
            if (strcmp(session_table[i].session, value) == 0)
            {
                return i;
            }
        }
        else if (search_by == 1)
        { // 按 username 字段查找
            if (strcmp(session_table[i].username, value) == 0)
            {
                return i;
            }
        }
    }
    return -1; // 如果未找到匹配项，返回 -1
}

// 从数据库中根据用户名查找用户id
int find_id_mysql(char *name, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    snprintf(query, sizeof(query), "SELECT id FROM users WHERE username='%s'", name);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    int id = atoi(row[0]);
    mysql_free_result(result);
    return id;
}

void clietn_exit(pthread_t *event_pthread)
{
    pthread_cancel(*event_pthread);
    pthread_cancel(pthread_self());
}
