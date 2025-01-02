#include "server.h"
extern __thread pthread_mutex_t friend_lock;
extern __thread struct session_name client_session; // 线程局部变量存储客户端会话信息
extern __thread char **friends;
extern __thread char **online_friends; // 在线好友列表与好友列表和对应的好友数量
extern __thread int friend_count;
extern __thread int online_friend_count;
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
    char message[64] = "创建用户成功！";
    if (mysql_query(conn, query))
    {
        snprintf(message, sizeof(message), "用户已经存在，请换个用户名");
        send_message(client_fd, message);
        free(response);
        return;
    }

    send_message(client_fd, message);
    free(response);
    return;
}

// 处理添加或删除好友请求
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

        do_query(query, conn);
        char message[64];
        snprintf(message, sizeof(message), "%s删除成功", add_friend->friend_username);
        send_message(client_session.client_fd, message);
        return;
    }

    if (online_query(add_friend->friend_username)) // 用户在线，发送消息提醒
    {
        char message[128];
        int i = find_session_index(1, add_friend->friend_username);
        snprintf(message, sizeof(message), "收到来自%s的好友请求\n", client_session.username);
        send_message(session_table[i].client_fd, message);
    }
    // 如果用户不在线，数据插入到数据库，上线时推送
    snprintf(query, sizeof(query), "INSERT INTO friends (user_id, friend_id) VALUES ('%u','%u');",
             user_id, friend_id);
    do_query(query, conn);

    send_simple(client_fd, SUCCESS);
    mysql_free_result(result);
    return;
}

// 处理接受与拒绝添加好友的请求
void handle_accept_add(int client_fd, char *buffer, MYSQL *conn)
{
    HandleFriendRequest *handle_add = (HandleFriendRequest *)buffer;
    int i = find_session_index(0, handle_add->session_token);
    int friend_id = session_table[i].id; // 找到处理好友申请的用户id用于后续查询，user_id是发起好友申请的用户的id
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
    if (strcmp("accepted", status) == 0)
    {
        char onoff[64];
        if (online_query(handle_add->friend_username))
        {
            snprintf(onoff, sizeof(onoff), "添加成功%s在线", handle_add->friend_username);
            send_message(client_fd, onoff);
            int i = find_session_index(1, handle_add->friend_username);
            snprintf(onoff, sizeof(onoff), "添加成功%s在线", client_session.username);
            send_message(session_table[i].client_fd, onoff);
        }
        else
        {
            snprintf(onoff, sizeof(onoff), "添加成功%s不在线", handle_add->friend_username);
            send_message(client_fd, onoff);
        }
    }
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

void friend_remark(int client_fd, char *buffer, MYSQL *conn)
{
    FriendRemarkRequest *req = (FriendRemarkRequest *)buffer;
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    int user_id = find_id_mysql(client_session.username, conn);
    int friend_id = find_id_mysql(req->friendname, conn);

    // 检查是否存在记录
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM friend_aliases WHERE user_id=%d AND friend_id=%d",
             user_id, friend_id);
    if (mysql_query(conn, query))
    {
        fprintf(stderr, "查询失败: %s\n", mysql_error(conn));
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL)
    {
        fprintf(stderr, "获取查询结果失败: %s\n", mysql_error(conn));
        return;
    }
    row = mysql_fetch_row(res);
    int exists = row && atoi(row[0]) > 0; // 检查是否已有数据
    mysql_free_result(res);

    if (exists)
    {
        // 如果记录存在，执行更新操作
        snprintf(query, sizeof(query),
                 "UPDATE friend_aliases SET alias='%s' WHERE user_id=%d AND friend_id=%d",
                 req->remark, user_id, friend_id);
    }
    else
    {
        // 如果记录不存在，执行插入操作
        snprintf(query, sizeof(query),
                 "INSERT INTO friend_aliases (user_id, friend_id, alias) VALUES (%d, %d, '%s')",
                 user_id, friend_id, req->remark);
    }
    do_query(query, conn);
    send_message(client_fd, "修改成功");
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

// 客户端发来获取好友列表请求时，将列表发过去给客户端
void push_friend_list(MYSQL *conn)
{
    free_friend_list(friends);
    friends = NULL;
    friends = get_friend_list(client_session.id, &friend_count, conn);
    push_fri_list(friends, friend_count, client_session.client_fd, conn);
}