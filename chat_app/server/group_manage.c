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
    int friend_index = find_session_index(0, invite->friendname);
    if (online_query(invite->friendname))
    {

        char group_invite[128];
        snprintf(group_invite, sizeof(group_invite),
                 "%s邀请你进入群聊：%s", client_session.username, invite->group_name);
        send_message(session_table[friend_index].client_fd, group_invite);
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
    int friend_id = atoi(row[0]);
    int group_id = atoi(row[1]);
    mysql_free_result(result);
    snprintf(query, sizeof(query), "INSERT INTO group_invites "
                                   "(group_id,sender_id,invitee_id,status) VALUES ('%d','%d','%d','pending');",
             group_id, client_session.id, friend_id);
    do_query(query, conn);
    return;
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
    while (row = mysql_fetch_row(result))
    {
        snprintf(temp, sizeof(temp), "%s邀请你进入群聊%s\n", row[0], row[1]);
        strcat(push, temp);
    }
    mysql_free_result(result);
    printf("%s\n", push);
    send_message(client_fd, push);
}

//处理同意和拒绝进群的请求
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

    int grou_id = find_group_id(handle_group->group_name,conn);
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
    }
    else
    {
        strcpy(status, "rejected");
        snprintf(query, sizeof(query),
                 "UPDATE group_invites "
                 "SET status= '%s' "
                 "where group_id=%d and invitee_id=%d;",
                 status, grou_id, invitee_id);
        do_query(query,conn);
    }
}

int  find_group_id(char *groupname, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    int group_id;
    snprintf(query, sizeof(query),
             "SELECT id FROM groups where group_name='%s';", groupname);
    result = do_query(query, conn);
    row = mysql_fetch_row(result);
    group_id=atoi(row[0]);
    mysql_free_result(result);
    return group_id;
}