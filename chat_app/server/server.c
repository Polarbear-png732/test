#include "server.h"
pthread_mutex_t mysql_lock = PTHREAD_MUTEX_INITIALIZER;

char **friends = NULL;
char **online_friends = NULL; // 在线好友列表与好友列表和对应的好友数量
int friend_count = 0;
int online_friend_count;
struct session_name client_session = {0};        // 存储客户端会话信息

EventQueue *queue=NULL;

// 请求处理函数
void *handle_client(void *arg)
{
    queue = init_event_queue();
    pthread_t event_thread;

    // 创建线程处理事件队列
    pthread_create(&event_thread, NULL, process_events, queue);

    int client_fd = *(int *)arg;
    char buffer[1024];
    MYSQL *conn = db_connect();
    if (!conn)
    {
        printf("数据库连接失败！\n");
        close(client_fd);
        return NULL;
    }

    unsigned int req_length;
    unsigned int size_len = sizeof(req_length);

    // 设置接收超时时间为 10 秒
    struct timeval timeout;
    timeout.tv_sec = 300; // 超时时间秒数
    timeout.tv_usec = 0; // 微秒部分
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
        req_length = ntohl(*(unsigned int *)buffer);

        if (recv_full(client_fd, buffer + size_len, req_length - size_len) <= 0) // 接收剩余的数据
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                printf("客户端 10 秒未发送请求，下线处理。\n");
                delete_session(client_session.session);
            }
            else
            {
                printf("接收剩余数据失败或客户端断开！\n");
            }
            break;
        }

        // 解析请求类型
        unsigned int request_code = ntohl(*(unsigned int *)(buffer + size_len));
        switch (request_code)
        {
        case REQUEST_LOGIN:
            handle_login(client_fd, buffer, conn);
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
            invite_to_group(client_fd,buffer,conn);
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
void handle_login(int client_fd, char *buffer, MYSQL *conn)
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
    generate_session_id(&login_res.session_token);
    strcpy(&login_res.offline_messages, "no offline messige");

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
    offline_message_push(user_id, conn);
    on_off_push(1, friends);
}

// 创建用户处理函数
void handle_create_user(int client_fd, char *buffer, MYSQL *conn)
{

    unsigned int len_response = sizeof(SimpleResponse);
    SimpleResponse *response = (SimpleResponse *)malloc(len_response);
    response->length = htonl(len_response);
    response->request_code = htonl(SIMPLE_RESPONSE);

    printf("handle create user\n");
    CreateUser *create_req = (CreateUser *)buffer;
    // 插入用户数据到数据库
    char query[512];
    snprintf(query, sizeof(query), "INSERT INTO users (username,password) VALUES ('%s','%s' );",
             create_req->username, create_req->password);
    pthread_mutex_lock(&mysql_lock);

    if (mysql_query(conn, query))
    {
        response->status_code = htonl(FAIL);
        send(client_fd, response, len_response, 0);
        free(response);
        return;
    }
    pthread_mutex_unlock(&mysql_lock);

    // 返回成功响应
    response->status_code = htonl(SUCCESS);
    send(client_fd, response, len_response, 0);
    free(response);
    return;
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
// 根据会话标识符找到在线用户id
int find_uid(char *token)
{

    int i = find_session_index(0, token);
    int id = session_table[i].id;
    return id;
}

int find_session_index(int search_by, const char *value)
{
    for (int i = 0; i < session_table_index; ++i)
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

// 处理添加好友请求
void handle_add_friend(int client_fd, char *buffer, MYSQL *conn)
{
    FriendRequest *add_friend = (FriendRequest *)buffer;
    int i = find_session_index(0, add_friend->session_token);
    unsigned int user_id = 0;
    unsigned friend_id = 0;

    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    snprintf(query, sizeof(query),
             "SELECT id FROM users WHERE username='%s' OR username='%s' "
             "ORDER BY CASE username "
             "WHEN '%s' THEN 1 "
             "WHEN '%s' THEN 2 "
             "ELSE 3 END;",
             session_table[i].username,
             add_friend->friend_username,
             session_table[i].username,
             add_friend->friend_username);

    // 执行查询
    result = do_query(query, conn);
    if (result == NULL)
    {
        fprintf(stderr, "Store Result Error: %s\n", mysql_error(conn));
        return;
    }

    // 遍历结果集
    while ((row = mysql_fetch_row(result)))
    {
        if (user_id == 0)
        {
            user_id = atoi(row[0]);
        }
        else
        {
            friend_id = atoi(row[0]);
        }
    }
    printf("user_id:%d,  friend_id:%d\n", user_id, friend_id);

    if (ntohl(add_friend->action) == 0)
    {
        snprintf(query, sizeof(query), "DELETE FROM friends "
                                       "WHERE (user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d);",
                 user_id, friend_id, friend_id, user_id);
        pthread_mutex_lock(&mysql_lock);
        if (mysql_query(conn, query))
        {
            fprintf(stderr, "Error: %s\n", mysql_error(conn));
            return;
        }
        return;
    }
    pthread_mutex_unlock(&mysql_lock);
    // 插入用户数据到数据库
    snprintf(query, sizeof(query), "INSERT INTO friends (user_id, friend_id) VALUES ('%u','%u');",
             user_id, friend_id);
    pthread_mutex_lock(&mysql_lock);

    if (mysql_query(conn, query))
    {
        send_message(client_fd, "Add friend failed");
        mysql_free_result(result);
        return;
    }
    pthread_mutex_unlock(&mysql_lock);
    send_simple(client_fd, SUCCESS);
    mysql_free_result(result);
    return;
}

// 处理接受与拒绝添加好友的请求
void handle_accept_add(int client_fd, char *buffer, MYSQL *conn)
{
    HandleFriendRequest *handle_add = (HandleFriendRequest *)buffer;
    int i = find_session_index(0, handle_add->session_token);
    int friend_id = session_table[i].id; // 找到处理好友申请的用户id用于后续查询
    int user_id;
    int table_id; // 数据库中好友表的表项id
    char status[16];
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    if (ntohl(handle_add->action))
    {
        strcpy(status, "accepted");
    }
    else
    {
        strcpy(status, "blocked");
    }
    snprintf(query, sizeof(query),
             "SELECT id FROM users where username='%s'", handle_add->friend_username);

    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    user_id = atoi(row[0]);
    mysql_free_result(result); // 释放结果集

    snprintf(query, sizeof(query),
             "SELECT id FROM friends where friend_id='%d' and user_id='%d';", friend_id, user_id);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    table_id = atoi(row[0]);

    snprintf(query, sizeof(query),
             "UPDATE friends SET status = '%s' WHERE id='%d';", status, table_id);

    do_query(query, conn);

    send_simple(client_fd, SUCCESS);
    return;
}

// 推送好友请求
void push_friend(int client_fd, char *name, MYSQL *conn)

{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    snprintf(query, sizeof(query), "SELECT id FROM users WHERE username='%s'", name);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    int friend_id = atoi(row[0]);
    mysql_free_result(result);
    snprintf(query, sizeof(query),
             "SELECT u.username "
             "FROM friends f "
             "JOIN users u ON f.user_id = u.id "
             "WHERE f.friend_id = %d AND f.status = 'pending';",
             friend_id);

    result = do_query(query, conn);

    char push_friend_message[256] = "新的好友：";
    int length = strlen(push_friend_message);

    while ((row = mysql_fetch_row(result)))
    {
        if (row[0] != NULL && strlen(row[0]) > 0)
        { // 检查 row[0] 是否为空
            strncat(push_friend_message, row[0], 32);
            strncat(push_friend_message, " ", 1);
            length = strlen(push_friend_message); // 更新 length
        }
    }
    printf("%s\n", push_friend_message);
    if (length > strlen("新的好友："))
    { // 检查是否有新的内容被添加
        send_message(client_fd, push_friend_message);
    }
    mysql_free_result(result);
    return;
}

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

char **get_online_friends(char **friends, int *friend_count, int *online_friend_count)
{
    int i, j, k = 0;                                                        // k用于新数组索引
    char **new_friends = (char **)malloc(sizeof(char *) * (*friend_count)); // 新的动态数组

    if (!new_friends)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL; // 内存分配失败返回 NULL
    }

    for (i = 0; i < *friend_count; i++)
    {
        for (j = 0; j < session_table_index; j++)
        {
            if (strcmp(friends[i], session_table[j].username) == 0)
            { // 如果在线
                // 分配内存并复制字符串
                new_friends[k] = malloc(strlen(friends[i]) + 1);
                if (new_friends[k])
                {
                    strcpy(new_friends[k], friends[i]);
                    k++;
                }
                else
                {
                    fprintf(stderr, "Memory allocation failed for online friend\n");
                }
                break; // 匹配上了直接跳出内层循环
            }
        }
    }

    // 重新分配新的数组空间以匹配实际在线好友数
    char **temp = realloc(new_friends, sizeof(char *) * k);
    if (!temp)
    {
        fprintf(stderr, "无好友在线\n");
        // 如果 realloc 失败，保留原数组并继续
    }
    else
    {
        new_friends = temp;
    }

    *online_friend_count = k;
    return new_friends;
}

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
    online_friends = get_online_friends(friends, &friend_count, &online_friend_count);
    if (online_friends)
    {
        // 遍历并处理在线好友
        for (i = 0; i < online_friend_count; i++)
        {
            int index = find_session_index(1, online_friends[i]);
            send_message(session_table[index].client_fd, online_push);//向好友客户端发送上线消息

            EventQueue *queue =find_queue(session_table[index].client_fd);//向好友服务器进程的事件队列中插入上线事件
            push_event(queue, event);
        }
        return;
    }
}

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

void create_group(int client_fd, char *buffer, MYSQL *conn)
{
    GroupCreateRequest *group = (GroupCreateRequest *)buffer;
    int action = ntohl(group->action);
    int creator_id = find_uid(group->session_token);
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    if (action == 1)
    {
        snprintf(query, sizeof(query), "INSERT INTO groups (group_name,creator_id) "
                                       "VALUES ('%s','%d'); ",
                 group->group_name, creator_id);
        do_query(query, conn);
        return;
    }
    snprintf(query, sizeof(query), "DELETE FROM groups "
                                   "where creator_id='%d' and group_name='%s'; ",
             creator_id, group->group_name);
    do_query(query, conn);
    return;
}

void invite_to_group(int client_fd, char *buffer, MYSQL *conn)
{
    InviteRequest *invite = (InviteRequest *)buffer;
    int action = ntohl(invite->action);
    // 好友在线,直接转发邀请，否则先存入数据库
    int friend_index=find_session_index(0,invite->friendname);
    if (online_query(invite->friendname))
    {
        char group_invite[128];
        snprintf(group_invite, sizeof(group_invite),
                 "%s邀请你进入群聊：%s", client_session.username, invite->group_name);
        send_message(session_table[friend_index].client_fd,group_invite);
        return;
    }
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    snprintf(query, sizeof(query),
         "SELECT u.id AS user_id, g.id AS group_id "
         "FROM users u "
         "JOIN groups g "
         "WHERE u.username = '%s' AND g.group_name = '%s';",
         invite->friendname, invite->group_name);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    int friend_id=atoi(row[0]);
    int group_id=atoi(row[1]);
    mysql_free_result(result);
    snprintf(query, sizeof(query),"INSERT INTO group_invites "
    "(group_id,sender_id,invitee_id,status) VALUES ('%d','%d','%d','pending');",group_id,client_session.id,friend_id);
    do_query(query,conn);
    return;
}
int online_query(char *friendname)
{
    int i = 0;
    for (i; i < online_friend_count; i++)
    {
        if (strcmp(online_friends[i],friendname)  == 0)
        {
            return 1;
        }
    }
    return 0;
}


 EventQueue * init_event_queue() {
    EventQueue *queue = (EventQueue *)malloc(sizeof(EventQueue));
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
    return queue;
}

void destroy_event_queue(EventQueue *queue) {
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

int push_event(EventQueue *queue, Event event) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->count == QUEUE_SIZE) {
        // 队列已满
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    queue->events[queue->tail] = event;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE; // 环形缓冲区
    queue->count++;

    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int pop_event(EventQueue *queue, Event *event) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0) {
        // 队列为空，等待
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    *event = queue->events[queue->head];

    queue->head = (queue->head + 1) % QUEUE_SIZE; // 环形缓冲区
    queue->count--;

    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

// 初始化
void init_client_queues() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientfd_queues_map[i].client_fd = -1;
        clientfd_queues_map[i].queue = NULL;
    }
}

// 销毁所有事件队列
void cleanup_client_queues() {
    pthread_mutex_lock(&client_queues_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientfd_queues_map[i].queue) {
            destroy_event_queue(clientfd_queues_map[i].queue);
            free(clientfd_queues_map[i].queue);
        }
        clientfd_queues_map[i].client_fd = -1;
    }
    pthread_mutex_unlock(&client_queues_lock);
}
// 获取客户端事件队列
EventQueue* find_queue(int client_fd) {
    pthread_mutex_lock(&client_queues_lock);

    // 查找现有队列
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientfd_queues_map[i].client_fd == client_fd) {
            pthread_mutex_unlock(&client_queues_lock);
            return clientfd_queues_map[i].queue; // 返回已存在的队列
        }
    }
    pthread_mutex_unlock(&client_queues_lock);
    return NULL; // 如果队列空间已满，返回 NULL
}
void *process_events(void *arg) {
    EventQueue *queue = (EventQueue *)arg;

    while (1) {
        Event *event = malloc(sizeof(Event));
         pop_event(queue,event); // 从事件队列中获取事件
        if (event->event_type == 1) {
            printf("好友 %s 上线\n", event->username);
        } else if (event->event_type == 0) {
            printf("好友 %s 下线\n", event->username);
        }
        // 执行其他事件相关逻辑
    }
    return NULL;
}