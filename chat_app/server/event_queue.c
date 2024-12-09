#include "server.h"
extern __thread pthread_mutex_t friend_lock;
extern __thread struct session_name client_session; // 线程局部变量存储客户端会话信息
extern __thread char **friends;
extern __thread char **online_friends; // 在线好友列表与好友列表和对应的好友数量
extern __thread int friend_count;
extern __thread int online_friend_count;

void destroy_event_queue(EventQueue *queue)
{
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

int push_event(EventQueue *queue, Event event)
{
    pthread_mutex_lock(&queue->mutex);

    if (queue->count == QUEUE_SIZE)
    {
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

int pop_event(EventQueue *queue, Event *event)
{
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0)
    {
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
void init_client_queues()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clientfd_queues_map[i].client_fd = -1;
        clientfd_queues_map[i].queue = NULL;
    }
}

// 销毁所有事件队列
void cleanup_client_queues()
{
    pthread_mutex_lock(&client_queues_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientfd_queues_map[i].queue)
        {
            destroy_event_queue(clientfd_queues_map[i].queue);
            free(clientfd_queues_map[i].queue);
        }
        clientfd_queues_map[i].client_fd = -1;
    }
    pthread_mutex_unlock(&client_queues_lock);
}
// 获取客户端事件队列
EventQueue *find_queue(int client_fd)
{
    pthread_mutex_lock(&client_queues_lock);

    // 查找现有队列
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientfd_queues_map[i].client_fd == client_fd)
        {
            pthread_mutex_unlock(&client_queues_lock);
            return clientfd_queues_map[i].queue; // 返回已存在的队列
        }
    }
    pthread_mutex_unlock(&client_queues_lock);
    return NULL; // 如果队列空间已满，返回 NULL
}

EventQueue *init_event_queue()
{
    EventQueue *queue = (EventQueue *)malloc(sizeof(EventQueue));
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
    return queue;
}

void *process_events(void *arg)
{
    event_pthread_arg *event_arg = (event_pthread_arg *)arg;
    EventQueue *queue = event_arg->queue;
    while (1)
    {
        Event *event = malloc(sizeof(Event));
        pop_event(queue, event); // 从事件队列中获取事件
        if (event->event_type == 1)
        {
            update_online_friends(event, event_arg);
        }
        else if (event->event_type == 0)
        {
            update_online_friends(event, event_arg);
        }
        // 执行其他事件相关逻辑
    }
    return NULL;
}

void update_online_friends(Event *event, event_pthread_arg *event_arg)
{
    if (event->event_type == 1)
    {
        strncpy(event_arg->online_friends[*event_arg->online_friend_count], event->username, MAX_USERNAME_LENGTH - 1);
        (*event_arg->online_friend_count)++;
        printf("客户端：test，当前在线好友数量：%d\n", *event_arg->online_friend_count);
    }
    else
    {

        for (int i = 0; i < *event_arg->online_friend_count; i++)
        {
            if (strcmp(event->username, event_arg->online_friends[i]) == 0) // 找到下线好友后，将之后的好友往前移，删除下线好友
            {
                for (int j = i; j < *event_arg->online_friend_count; j++)
                {
                    *event_arg->online_friends[j] = *event_arg->online_friends[j + 1];
                }
                break;
            }
        }
        (*event_arg->online_friend_count)--;
        printf("客户端：test，当前在线好友数量：%d\n", *event_arg->online_friend_count);
    }
}
