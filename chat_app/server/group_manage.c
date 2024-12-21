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
        add_group(groups, grou_id, group->group_name);
        add_member_to_group(groups, grou_id, group->group_name);

        return;
    }
    int groupnum = get_groupnum(conn);
    int grou_id = find_group_id(group->group_name, conn);
    snprintf(query, sizeof(query), "DELETE FROM groups "
                                   "where creator_id='%d' and group_name='%s'; ",
             creator_id, group->group_name);
    do_query(query, conn);
    dissolve_group(groups, grou_id);
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

    snprintf(query, sizeof(query),
             "SELECT u.id AS user_id, g.id AS group_id "
             "FROM users u "
             "JOIN groups g "
             "WHERE u.username = '%s' AND g.group_name = '%s';",
             invite->friendname, invite->group_name);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    int friend_id = atoi(row[0]);
    int group_id = atoi(row[1]);
    mysql_free_result(result);

    if (action == 0)
    { // 如果是删除成员的请求，删除并更新结构体数组
        snprintf(query, sizeof(query), "DELETE FROM group_members "
                                       "where group_id='%d and user_id='%d';",
                 group_id, friend_id);
        do_query(query, conn);
        int groupnum = get_groupnum(conn);
        remove_member_from_group(groups, group_id, invite->friendname);
        return;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO  group_invites (group_id,sender_id,invitee_id) VALUES ('%d','%d','%d');",
             group_id, client_session.id, friend_id);
    do_query(query, conn);
    if (online_query(invite->friendname))
    {
        char group_invite[128];
        snprintf(group_invite, sizeof(group_invite),
                 "%s邀请你进入群聊：%s", client_session.username, invite->group_name);
        send_message(session_table[friend_index].client_fd, group_invite);
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
        int groupnum = get_groupnum(conn);
        add_member_to_group(groups, grou_id, client_session.username);
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

// 获取群组成员
void get_groupmember(Group *groups, MYSQL *conn)
{
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
        return;
    }

    int group_index = -1;
    int group_id = -1;

    while ((row = mysql_fetch_row(result)))
    {
        int current_group_id = atoi(row[0]);

        // 检查是否是新的群组
        if (current_group_id != group_id)
        {
            group_id = current_group_id;
            group_index++;
            groups[group_index].group_id = current_group_id;
            strncpy(groups[group_index].group_name, row[1], sizeof(groups[group_index].group_name) - 1);
            groups[group_index].group_name[sizeof(groups[group_index].group_name) - 1] = '\0'; // 确保字符串结尾
            groups[group_index].member_count = 0;                                              // 初始化成员计数
        }

        // 添加成员到当前群组
        int member_index = groups[group_index].member_count;
        if (member_index < MAX_MEMBERS)
        {
            strncpy(groups[group_index].members[member_index], row[3], MAX_USERNAME_LENGTH - 1);
            groups[group_index].members[member_index][MAX_USERNAME_LENGTH - 1] = '\0'; // 确保字符串结尾
            groups[group_index].member_count++;
        }
    }

    mysql_free_result(result);
}

// 添加成员到群组
int add_member_to_group(Group *groups, unsigned int group_id, const char *username)
{
    for (int i = 0; i < 10; i++)
    {
        if (groups[i].group_id == group_id)
        {
            // 检查是否有足够的空间加入新成员
            if (groups[i].member_count < MAX_MEMBERS)
            {
                // 确保成员名不重复
                for (int j = 0; j < groups[i].member_count; j++)
                {
                    if (strcmp(groups[i].members[j], username) == 0)
                    {
                        return 0; // 用户已经是群组成员
                    }
                }
                // 加入新成员
                strncpy(groups[i].members[groups[i].member_count], username, MAX_USERNAME_LENGTH - 1);
                groups[i].members[groups[i].member_count][MAX_USERNAME_LENGTH - 1] = '\0'; // 确保字符串结尾
                groups[i].member_count++;
                return 1; // 成员加入成功
            }
            return 0; // 群组已满，无法加入新成员
        }
    }
    return 0; // 找不到指定的群组
}

// 从群组中移除成员
int remove_member_from_group(Group *groups, unsigned int group_id, const char *username)
{
    for (int i = 0; i < 10; i++)
    {
        if (groups[i].group_id == group_id)
        {
            for (int j = 0; j < groups[i].member_count; j++)
            {
                if (strcmp(groups[i].members[j], username) == 0)
                {
                    // 找到成员并将其移除
                    for (int k = j; k < groups[i].member_count - 1; k++)
                    {
                        strcpy(groups[i].members[k], groups[i].members[k + 1]);
                    }
                    groups[i].member_count--;
                    return 1; // 成员移除成功
                }
            }
            return 0; // 成员未找到
        }
    }
    return 0; // 找不到指定的群组
}

// 添加新群组
int add_group(Group *groups, unsigned int group_id, const char *group_name)
{
    // 检查是否已经有10个群组
    for (int i = 0; i < 10; i++)
    {
        if (groups[i].group_id == 0)
        { // 0 表示空的群组
            groups[i].group_id = group_id;
            strncpy(groups[i].group_name, group_name, sizeof(groups[i].group_name) - 1);
            groups[i].group_name[sizeof(groups[i].group_name) - 1] = '\0'; // 确保字符串结尾
            groups[i].member_count = 0;
            return 1; // 群组创建成功
        }
    }
    return 0; // 群组已满，无法创建新群组
}

// 删除群组
int dissolve_group(Group *groups, unsigned int group_id)
{
    for (int i = 0; i < 10; i++)
    {
        if (groups[i].group_id == group_id)
        {
            groups[i].group_id = 0; // 通过设置 group_id 为 0 来标记群组已解散
            groups[i].member_count = 0;
            return 1; // 群组解散成功
        }
    }
    return 0; // 找不到指定的群组
}

void print_groups(Group *groups, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;

    // SQL 查询语句
    snprintf(query, sizeof(query),
             "SELECT g.id AS group_id, g.group_name, gm.user_id, u.username "
             "FROM groups g JOIN group_members gm ON g.id = gm.group_id "
             "JOIN users u ON gm.user_id = u.id "
             "ORDER BY g.id, gm.user_id;");

    // 执行查询
    result = mysql_query(conn, query) == 0 ? mysql_store_result(conn) : NULL;
    if (!result)
    {
        fprintf(stderr, "Query execution failed or no results.\n");
        return;
    }

    int group_index = -1;
    int group_id = -1;

    // 遍历查询结果
    while ((row = mysql_fetch_row(result)))
    {
        int current_group_id = atoi(row[0]);

        // 检查是否是新的群组
        if (current_group_id != group_id)
        {
            group_id = current_group_id;
            group_index++;
            groups[group_index].group_id = current_group_id;
            strncpy(groups[group_index].group_name, row[1], sizeof(groups[group_index].group_name) - 1);
            groups[group_index].group_name[sizeof(groups[group_index].group_name) - 1] = '\0'; // 确保字符串结尾
            groups[group_index].member_count = 0;                                              // 初始化成员计数
        }

        // 添加成员到当前群组
        int member_index = groups[group_index].member_count;
        if (member_index < MAX_MEMBERS)
        {
            strncpy(groups[group_index].members[member_index], row[3], MAX_USERNAME_LENGTH - 1);
            groups[group_index].members[member_index][MAX_USERNAME_LENGTH - 1] = '\0'; // 确保字符串结尾
            groups[group_index].member_count++;
        }
    }

    // 打印群组信息
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

    // 释放查询结果
    mysql_free_result(result);
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
            if(strcmp(row[1],row[2])==0){
                strcat(group_push,"（群主）");
            }
            strcat(group_push, "\n");
            j++;
        }
        mysql_free_result(result);
    }
    printf("%s\n", group_push);
    send_message(client_fd, group_push);
}
