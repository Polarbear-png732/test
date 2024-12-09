#include "server.h"
extern __thread struct session_name client_session;        // 线程局部变量存储客户端会话信息
extern __thread char **friends ;
extern __thread char **online_friends; // 在线好友列表与好友列表和对应的好友数量
extern __thread  int friend_count;
extern __thread int online_friend_count;
extern __thread  pthread_mutex_t friend_lock ;
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
