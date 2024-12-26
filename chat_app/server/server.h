#ifndef SERVER_H
#define SERVER_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include <arpa/inet.h>
#include <sys/time.h> // 用于设置套接字超时
#include <pthread.h>
#include <signal.h>
#define PORT 10005
#define FILE_TRANSFER_PORT 10007
#define MAX_CLIENT 100
#define TOKEN_LEN 64
#define MAX_FRIENDS 10
#define MAX_USERNAME_LENGTH 32
#define MAX_CLIENTS 10
#define QUEUE_SIZE 10
#define MAX_MEMBERS 10

struct session_name
{
    char session[64];
    char username[32];
    int client_fd;
    unsigned id;
};
extern __thread pthread_mutex_t friend_lock;
extern __thread struct session_name client_session; // 线程局部变量存储客户端会话信息
extern __thread char **friends;
extern __thread char **online_friends; // 在线好友列表与好友列表和对应的好友数量
extern __thread int friend_count;
extern __thread int online_friend_count;

typedef struct
{
    unsigned int group_id;
    char group_name[32];
    char members[MAX_MEMBERS][MAX_USERNAME_LENGTH];
    int member_count;
} Group;
extern Group groups[10];
// 以下三个结构体用于事件队列机制，实现上下线消息的及时推送
typedef struct
{
    int event_type;                     // 1 = 上线通知, 0 = 下线通知
    char username[MAX_USERNAME_LENGTH]; // 好友用户名
} Event;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    Event events[QUEUE_SIZE]; // 固定大小的事件数组
    int head;                 // 队列头部索引
    int tail;                 // 队列尾部索引
    int count;
    int stop;                // 队列中的事件数
} EventQueue;
// 存储每个客户端文件描述符和其事件队列的映射关系
typedef struct
{
    int client_fd;
    EventQueue *queue;
} ClientEventQueueMap;

typedef struct
{
    EventQueue *queue;
    char **online_friends;
    char **friends;
    int *friend_count;
    int *online_friend_count;
} event_pthread_arg;

// 使用 extern 关键字声明全局变量
extern struct session_name session_table[10];
extern int session_table_index;
extern ClientEventQueueMap clientfd_queues_map[MAX_CLIENTS]; // 存储客户端事件队列与描述符号的映射
extern pthread_mutex_t client_queues_lock;
extern int map_index;
extern char online_members[MAX_MEMBERS][MAX_USERNAME_LENGTH];
extern int file_sock;

// 请求码定义
#define REQUEST_LOGIN 10001
#define RESPONSE_LOGIN 10021
#define REQUEST_LOGOUT 10002
#define REQUEST_ADD_FRIEND 10003
#define REQUEST_HANDELE_ADD 10011
#define REQUEST_DELETE_FRIEND 10004
#define REQUEST_REMARK_FRIEND 10005

#define REQUEST_INVITE_TOGROUP 10013
#define REQUEST_CREATE_GROUP 10006
#define REQUEST_HANDLE_GROUP 10031
#define REQUEST_GROUP_MESSAGE 10007
#define  REQUEST_GROUPNAME_RESET 10032

#define REQUEST_PRIVATE_MESSAGE 10008

#define REQUEST_FILE_TRANSFER 10009
#define RESPONSE_FILE_TRANSFER 10016
#define RESPONSE_FILE_ACK 10017
#define REQUEST_CREATEUSER 10010
#define REQUEST_POLLING 10012


#define CLIENT_EXIT 10000

#define RESPONSE_MESSAGE 10040
#define SIMPLE_RESPONSE 10050
#define SUCCESS 200
#define FAIL 500
#define BUFSIZE 4096
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    char message[2048];
} FeedbackMessage;

// 登录请求结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    char username[32];
    char password[32];
} LoginRequest;

// 创建用户请求结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code;
    char username[32];
    char password[32];
} CreateUser;

// 登录响应结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code;
    unsigned int status_code;    // 200=成功, 401=认证失败
    char session_token[64];      // 会话标识符
    char offline_messages[1024]; // 离线消息
} LoginResponse;

// 添加/删除好友请求结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    char session_token[64];
    char friend_username[32];
    unsigned int action; // 1=添加好友, 0=删除好友
} FriendRequest;

// 处理好友请求
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    unsigned int action;       // 1 接受，0拒绝
    char friend_username[32];
    char session_token[64]; // 会话标识符
} HandleFriendRequest;

// 响应用简单的回复报文
typedef struct
{
    unsigned int length;
    unsigned int request_code;
    unsigned int status_code; // 200=成功,  500=失败
} SimpleResponse;

// 好友备注请求结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    char session_token[64];
    char friend_username[32];
    char remark[32]; // 好友备注
} FriendRemarkRequest;
// 响应用简单的回复报文

// 创建群组,处理群聊请求，请求结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code;
    int action;
    char session_token[64];
    char group_name[64];
} GroupCreateRequest, HandleGroupInvite;
// 邀请好友进群的结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code;
    int action;
    char friendname[32];
    char session_token[64];
    char group_name[64];
} InviteRequest;
// 群组消息广播结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    unsigned int group_id;
    char session_token[64];
    char message[256];
} GroupMessage;

typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    unsigned int group_id;
    char group_newname[64];
} GroupNameRestet;

typedef struct
{
    unsigned int length;
    unsigned int status_code; // 200=成功, 500=失败
    char group_id[64];        // 创建成功的群组ID
} GroupCreateResponse;

// 点对点消息结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    char session_token[64];
    char receiver_username[32];
    char message[256];
} PrivateMessage;

// 响应用简单的回复报文

// 文件传输请求结构体
typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    unsigned int file_size;
    char receiver_username[32];
    char session_token[64];
    char file_name[128];
} FileTransferRequest;

typedef struct
{
    unsigned int length;
    unsigned int request_code;  //ack
    unsigned int block_number;  //分块编号
} FileTransferResponse;
    typedef struct {
    int num_files;
    char **offline_file;
} OfflineFileData;

typedef struct
{
    unsigned int length;
    unsigned int request_code; // 请求码
    char token[64];            // 用于后续传输
} Polling;

// 初始化服务器套接字
int init_server();

// 处理客户端请求
void *handle_client(void *arg);

// 登录功能处理
void handle_login(int client_fd, char *buffer, MYSQL *conn, pthread_t *queue_pthread,event_pthread_arg *event_arg);
// 添加好友处理
void handle_add_friend(int client_fd, char *buffer, MYSQL *conn);
// 创建用户处理
void handle_create_user(int client_fd, char *buffer, MYSQL *conn);
// 处理好友请求
void handle_accept_add(int client_fd, char *buffer, MYSQL *conn);
// 数据库连接
MYSQL *db_connect();

// 存储会话标识符与用户名的映射
void store_session(const char *username, const char *session_token, unsigned int id, int client_fd);
// 推送好友
void push_friend(int client_fd, char *username, MYSQL *conn);
// 错误反馈
void send_message(int sockfd, const char *feedback);

// 从数据库中获取好友列表
char **get_friend_list(int user_id, int *friend_count, MYSQL *conn);
// 生成会话标识符
void generate_session_id(char *session_id);

// 推送好友列表
void push_fri_list(char **list, int count, int client_fd, MYSQL *conn);

int recv_full(int sock, void *buf, size_t len);
// 根据会话标识符找到用户在表中的索引
int find_session_index(int search_by, const char *value);
// 将一个会话从表中删除
int delete_session(const char *session);
void send_simple(int sockfd, int success);
// 获取在线用户的id
int find_uid(char *token);

void private_message(int client, char *buffer, MYSQL *conn);

MYSQL_RES *qurey(char *query, MYSQL *conn); // 执行一个查询语句，返回结果集合
int init_file_transfer_server() ;
int find_id_mysql(char *name, MYSQL *conn); // 获取离线用户的id
char **get_online_friends(char **friends, int *friend_count, int *online_friend_count);

void offline_message_push(unsigned int user_id, MYSQL *conn);
void on_off_push(int on);
void create_group(int client_fd, char *buffer, MYSQL *conn);
int online_query(char *friendname);
void invite_to_group(int client_fd, char *buffer, MYSQL *conn);
MYSQL_RES *do_query(char *query, MYSQL *conn);

EventQueue *init_event_queue();
int push_event(EventQueue *queue, Event event);
int pop_event(EventQueue *queue, Event *event);
void init_client_queues();
void cleanup_client_queues();
EventQueue *find_queue(int client_fd);
void *process_events(void *arg);
void update_online_friends(Event *event, event_pthread_arg *event_arg);
void group_invite_push(int client_fd, MYSQL *conn);
int find_group_id(char *groupname, MYSQL *conn);
void handle_add_group(int client_fd, char *buffer, MYSQL *conn);
void group_message(int client_fd, char *buffer, MYSQL *conn);
int get_groupnum(MYSQL *conn);
void online_groupmembers(Group *groups, int group_id, char (*members)[MAX_USERNAME_LENGTH]);

// 声明获取群组成员的函数
void get_groupmember(Group *groups, MYSQL *conn);
void users_group_query(int client_fd,int user_id, MYSQL *conn);
// 声明向群组添加成员的函数
int add_member_to_group(Group *groups, unsigned int group_id, const char *username);

// 声明从群组中移除成员的函数
int remove_member_from_group(Group *groups, unsigned int group_id, const char *username);

// 声明添加新群组的函数
int add_group(Group *groups, unsigned int group_id, const char *group_name);
// 声明解散群组的函数
int dissolve_group(Group *groups, unsigned int group_id);
void print_groups(Group *groups, MYSQL *conn);

void groupname_reset(int client_fd,char *buffer,MYSQL *conn);

void offline_file_push(int client_fd, char *buffer, MYSQL *conn);
void file_transfer(int client_fd, char *filename);
void file_recv(int client_fd, char *buffer, MYSQL *conn);

void clietn_exit(pthread_t *event_pthread, event_pthread_arg *event_arg, int client_fd, MYSQL *conn);

OfflineFileData *offline_file_query(int recver_id, MYSQL* conn);
long get_file_size(const char *filename);
void free_offline_file(OfflineFileData *data);
void filetransfer_req(int client_fd,char *filename);

int delete_client_map(int client_fd);
void free_friend_list(char **friend_list);
void destroy_event_queue(EventQueue *queue);
void stop_event_queue(EventQueue *queue);
#endif
