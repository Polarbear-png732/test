#include "server.h"
extern __thread struct session_name client_session;
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

    if (mysql_query(conn, query))
    {
        response->status_code = htonl(FAIL);
        send(client_fd, response, len_response, 0);
        free(response);
        return;
    }

    // 返回成功响应
    response->status_code = htonl(SUCCESS);
    send(client_fd, response, len_response, 0);
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

        if (mysql_query(conn, query))
        {
            fprintf(stderr, "Error: %s\n", mysql_error(conn));
            return;
        }
        return;
    }

    // 插入用户数据到数据库
    snprintf(query, sizeof(query), "INSERT INTO friends (user_id, friend_id) VALUES ('%u','%u');",
             user_id, friend_id);


    if (mysql_query(conn, query))
    {
        send_message(client_fd, "Add friend failed");
        mysql_free_result(result);
        return;
    }

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

