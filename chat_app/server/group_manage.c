#include "server.h"
extern __thread struct session_name client_session; // 线程局部变量存储客户端会话信息
extern __thread char **friends;
extern __thread char **online_friends; // 在线好友列表与好友列表和对应的好友数量
extern __thread int friend_count;
extern __thread int online_friend_count;
extern __thread pthread_mutex_t friend_lock;

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
        int groupnum = get_groupnum(conn);
        int grou_id = find_group_id(group->group_name, conn);
        snprintf(query, sizeof(query), "INSERT INTO group_members (group_id,user_id) VALUES('%d','%d');", grou_id, creator_id);
        do_query(query, conn);
        char message[32];
        snprintf(message, sizeof(message), "创建成功，群聊id：%d", grou_id);
        send_message(client_fd, message);
        print_groups(groups, conn);
        return;
    }
    int groupnum = get_groupnum(conn);
    int grou_id = find_group_id(group->group_name, conn);
    snprintf(query, sizeof(query), "DELETE FROM groups "
                                   "where creator_id='%d' and group_name='%s'; ",
             creator_id, group->group_name);
    do_query(query, conn);
    send_message(client_session.client_fd,"删除成功");
    get_groupmember(groups,conn);
    return;
}

void invite_to_group(int client_fd, char *buffer, MYSQL *conn)
{
    InviteRequest *invite = (InviteRequest *)buffer;
    int action = ntohl(invite->action);
    // 好友在线,直接转发邀请，否则先存入数据库
    int friend_index = find_session_index(1, invite->friendname);
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    int friend_id = find_id_mysql(invite->friendname, conn);
    int group_id = ntohl(invite->group_id);
    if (action == 0)
    { // 如果是删除成员的请求，删除并更新结构体数组
        snprintf(query, sizeof(query), "select creator_id from groups where id=%d", group_id);
        result = do_query(query, conn);
        row = mysql_fetch_row(result);
        int creator_id = atoi(row[0]);
        if (client_session.id != creator_id)
        {
            send_message(client_session.client_fd, "只有群主才能删除群成员 ");
            return;
        }
        snprintf(query, sizeof(query), "DELETE FROM group_members "
                                       "WHERE group_id = %d AND user_id = %d;",
                 group_id, friend_id);
        do_query(query, conn);
        int groupnum = get_groupnum(conn);
        get_groupmember(groups, conn);
        mysql_free_result(result);
        return;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO  group_invites (group_id,sender_id,invitee_id) VALUES ('%d','%d','%d');",
             group_id, client_session.id, friend_id);
    do_query(query, conn);

    if (online_query(invite->friendname))
    {
        snprintf(query, sizeof(query), "select group_name from groups where id=%d", group_id);
        result = do_query(query, conn);
        row = mysql_fetch_row(result);
        char group_invite[128];
        snprintf(group_invite, sizeof(group_invite),
                 "%s邀请你进入群聊：%s", client_session.username, row[0]);
        send_message(session_table[friend_index].client_fd, group_invite);
        mysql_free_result(result);
        return;
    }
}

// 用户上线时，从数据库中查询，群聊邀请，并推送给用户
void group_invite_push(int client_fd, MYSQL *conn)
{
    char query[512];
    char push[256];
    char temp[128];
    MYSQL_RES *result;
    MYSQL_ROW row;
    int invitee_id = client_session.id;
    snprintf(query, sizeof(query),
             "SELECT u.username AS inviter_name, g.group_name "
             "FROM group_invites gi "
             "JOIN groups g ON gi.group_id = g.id "
             "JOIN users u ON gi.sender_id = u.id "
             "WHERE gi.invitee_id = %d AND gi.status = 'pending' "
             "ORDER BY u.username ASC, g.group_name ASC;",
             invitee_id);
    result = do_query(query, conn);
    int num_rows = (int)mysql_num_rows(result);
    if (num_rows == 0)
    {
        return;
    }
    while (row = mysql_fetch_row(result))
    {
        snprintf(temp, sizeof(temp), "%s邀请你进入群聊%s\n", row[0], row[1]);
        strcat(push, temp);
    }
    mysql_free_result(result);
    printf("%s\n", push);
    send_message(client_fd, push);
}

// 处理同意和拒绝进群的请求
void handle_add_group(int client_fd, char *buffer, MYSQL *conn)
{
    // 根据请求报文中的群聊名称，和会话标识符，找到对应的用户的记录，将群聊邀请状态改为接受
    // 再将用户插入到对应的群组成员表中

    HandleGroupInvite *handle_group = (HandleGroupInvite *)buffer;
    int action = ntohl(handle_group->action);
    char status[16];
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    int grou_id = find_group_id(handle_group->group_name, conn);
    int invitee_id = client_session.id;
    if (action)
    {
        strcpy(status, "accepted");
        snprintf(query, sizeof(query),
                 "UPDATE group_invites "
                 "SET status= '%s' "
                 "where group_id=%d and invitee_id=%d;",
                 status, grou_id, invitee_id);
        do_query(query, conn);
        snprintf(query, sizeof(query),
                 "INSERT INTO group_members (group_id,user_id) VALUES(%d,%d);", grou_id, invitee_id);
        do_query(query, conn);

        char message[32];
        snprintf(message, sizeof(message), "加入群聊成功，群id%d", grou_id);
        send_message(client_fd, message);
        print_groups(groups, conn);
    }
    else
    {
        strcpy(status, "rejected");
        snprintf(query, sizeof(query),
                 "UPDATE group_invites "
                 "SET status= '%s' "
                 "where group_id=%d and invitee_id=%d;",
                 status, grou_id, invitee_id);
        do_query(query, conn);
    }
}
// 从数据库中查询某个群的id
int find_group_id(char *groupname, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    int group_id;
    snprintf(query, sizeof(query),
             "SELECT id FROM groups where group_name='%s';", groupname);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    group_id = atoi(row[0]);
    mysql_free_result(result);
    return group_id;
}
// 处理发送群聊消息的请求
void group_message(int client_fd, char *buffer, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    GroupMessage *message = (GroupMessage *)buffer;
    unsigned int group_id = ntohl(message->group_id);
    int sender_id = find_uid(message->session_token);

    snprintf(query, sizeof(query), // 先将消息插入群聊消息表
             "INSERT INTO group_messages (group_id,sender_id,message) VALUES ('%u','%d','%s'); ",
             group_id, sender_id, message->message);
    do_query(query, conn);
    online_groupmembers(groups, group_id, online_members); // 找到群中除了发消息者之外在线的人，并转发群消息
    for (int i = 0; i < MAX_MEMBERS; i++)
    {
        if (strcmp(online_members[i], client_session.username) == 0 || strcmp(online_members[i], "") == 0) // 是发送这或者为空,跳过
        {
            continue;
        }
        int index = find_session_index(1, online_members[i]);
        char group_message[512];
        snprintf(group_message, sizeof(group_message), "来自'%s'群聊消息：%s\n", client_session.username, message->message);
        send_message(session_table[index].client_fd, group_message);
    }
}

void online_groupmembers(Group *groups, int group_id, char (*members)[MAX_USERNAME_LENGTH])
{

    for (int i = 0; i < MAX_MEMBERS; i++)
    {
        if (groups[i].group_id == group_id)
            for (int j = 0; j < groups[i].member_count; j++)
            {
                if (online_query(groups[i].members[j]))
                {
                    strcpy(online_members[j], groups[i].members[j]);
                }
            }
    }
}

// 获取当前群组数量
int get_groupnum(MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    strcpy(query, "SELECT COUNT(DISTINCT id) FROM groups;");
    result = mysql_query(conn, query) == 0 ? mysql_store_result(conn) : NULL;
    if (!result)
    {
        return -1;
    }
    row = mysql_fetch_row(result);
    int group_num = row ? atoi(row[0]) : 0;
    mysql_free_result(result);
    return group_num;
}

// 查询某个用户加入的群和群成员，群主, 并推送给用户
void users_group_query(int client_fd, int user_id, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    snprintf(query, sizeof(query), "select group_id from group_members where user_id=%d;", user_id);

    result = do_query(query, conn);
    int num_rows = (int)mysql_num_rows(result);

    int group[num_rows];
    int i = 0;
    while ((row = mysql_fetch_row(result)))
    {
        group[i] = atoi(row[0]);
        i++;
    }
    mysql_free_result(result);
    char group_push[512] = {0};
    for (i = 0; i < num_rows; i++)
    {
        snprintf(query, sizeof(query),
                 "SELECT "
                 "g.group_name, "
                 "u.username AS member_name, "
                 "u_creator.username AS creator_name "
                 "FROM "
                 "groups g "
                 "JOIN group_members gm ON g.id = gm.group_id "
                 "JOIN users u ON gm.user_id = u.id "
                 "JOIN users u_creator ON g.creator_id = u_creator.id "
                 "WHERE "
                 "g.id = %d;",
                 group[i]);
        result = do_query(query, conn);
        int j = 0;

        while ((row = mysql_fetch_row(result)))
        {
            if (j == 0)
            {
                int len = strlen(group_push);
                snprintf(group_push + len, sizeof(group_push) - len, "群聊:%s  id：%d\n", row[0], group[i]);
            }
            strcat(group_push, row[1]);
            if (strcmp(row[1], row[2]) == 0)
            {
                strcat(group_push, "（群主）");
            }
            strcat(group_push, "\n");
            j++;
        }
        mysql_free_result(result);
    }
    printf("%s\n", group_push);
    send_message(client_fd, group_push);
}

void groupname_reset(int client_fd, char *buffer, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    GroupNameRestet *req = (GroupNameRestet *)buffer;
    unsigned int group_id = ntohl(req->group_id);

    snprintf(query, sizeof(query), "select creator_id from groups where id=%u;", group_id);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    unsigned int creator = atoi(row[0]);

    if (creator != client_session.id)
    {
        send_message(client_fd, "只有群主才能修改！\n");
    }
    mysql_free_result(result);
    snprintf(query, sizeof(query), "update groups set group_name='%s' where id=%d", req->group_newname, group_id);
    do_query(query, conn);
    send_message(client_fd, "修改成功");
    print_groups(groups, conn);
}

void fetch_group_data(Group *groups, MYSQL *conn, int print_results)
{
    memset(groups, 0, 10 * sizeof(Group));
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    snprintf(query, sizeof(query),
             "SELECT g.id AS group_id, g.group_name, gm.user_id, u.username "
             "FROM groups g JOIN group_members gm ON g.id = gm.group_id "
             "JOIN users u ON gm.user_id = u.id "
             "ORDER BY g.id, gm.user_id;");

    result = mysql_query(conn, query) == 0 ? mysql_store_result(conn) : NULL;
    if (!result)
    {
        if (print_results)
        {
            fprintf(stderr, "Query execution failed or no results.\n");
        }
        return;
    }

    int group_index = -1;
    int group_id = -1;

    while ((row = mysql_fetch_row(result)))
    {
        int current_group_id = atoi(row[0]);

        if (current_group_id != group_id)
        {
            group_id = current_group_id;
            group_index++;
            groups[group_index].group_id = current_group_id;
            strncpy(groups[group_index].group_name, row[1], sizeof(groups[group_index].group_name) - 1);
            groups[group_index].group_name[sizeof(groups[group_index].group_name) - 1] = '\0';
            groups[group_index].member_count = 0;
        }

        int member_index = groups[group_index].member_count;
        if (member_index < MAX_MEMBERS)
        {
            strncpy(groups[group_index].members[member_index], row[3], MAX_USERNAME_LENGTH - 1);
            groups[group_index].members[member_index][MAX_USERNAME_LENGTH - 1] = '\0';
            groups[group_index].member_count++;
        }
    }

    if (print_results)
    {
        for (int i = 0; i <= group_index; i++)
        {
            printf("Group ID: %u, Group Name: %s, Members (%d):\n",
                   groups[i].group_id,
                   groups[i].group_name,
                   groups[i].member_count);

            for (int j = 0; j < groups[i].member_count; j++)
            {
                printf("  - %s\n", groups[i].members[j]);
            }
        }
    }

    mysql_free_result(result);
}

// 打印所有群组的信息，这些信息存储在全局结构体数组groups中

void print_groups(Group *groups, MYSQL *conn)
{
    fetch_group_data(groups, conn, 1);
}

// 获取数据库中所有群组及其成员，存到结构体数组groups中
void get_groupmember(Group *groups, MYSQL *conn)
{
    fetch_group_data(groups, conn, 0);
}

void group_info(char *buffer)
{
    GetGroup *group = (GetGroup *)buffer;

    char message[255];
    int message_size = sizeof(message);
    int i;
    for (i = 0; i < 10; i++)
    {
        if (ntohl(group->goup_id) == groups[i].group_id)
        {
            break;
        }
    }
    int offset = 0;
    offset += snprintf(message + offset, message_size - offset,
                       "Group ID: %u, Group Name: %s, Members (%d):\n",
                       groups[i].group_id,
                       groups[i].group_name,
                       groups[i].member_count);

    for (int j = 0; j < groups[i].member_count; j++)
    {
        offset += snprintf(message + offset, message_size - offset,
                           "  - %s\n", groups[i].members[j]);
    }
    printf("%s", message);
    send_message(client_session.client_fd, message);
}

void group_lsit()
{
    char message[1024]; // 增大缓冲区以适应所有群组信息
    int message_size = sizeof(message);
    int offset = 0;
    offset += snprintf(message + offset, message_size - offset, "Group Information:\n");
    for (int i = 0; i < 10; i++)
    {
        if (groups[i].group_id == 0)
        {
            // 跳过无效群组
            continue;
        }
        // 格式化群组信息
        offset += snprintf(message + offset, message_size - offset,
                           "Group ID: %u, Group Name: %s, Members (%d):\n",
                           groups[i].group_id,
                           groups[i].group_name,
                           groups[i].member_count);
        // 格式化成员信息
        for (int j = 0; j < groups[i].member_count; j++)
        {
            offset += snprintf(message + offset, message_size - offset,
                               "  - %s\n", groups[i].members[j]);
        }
    }
    // 确保字符串以 null 结尾
    message[message_size - 1] = '\0';
    // 打印结果
    printf("%s", message);
    send_message(client_session.client_fd,message);
}
