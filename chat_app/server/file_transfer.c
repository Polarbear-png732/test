#include "server.h"

void offline_file_push(int client_fd, char *buffer, MYSQL *conn)
{

    OfflineFileData *filedata = offline_file_query(client_session.id, conn);
    if (filedata == NULL)
    {
        return;
    }
    char *file_name = strstr(filedata->offline_file[0], "文件路径："); // 只传一个文件。多个文件之后再扩展
    if (file_name != NULL)
    {
        file_name += strlen("文件路径："); // 跳过“文件路径：”这部分，直接指向文件路径的内容
    }
    filetransfer_req(client_fd, file_name);
    file_transfer(client_fd, file_name, buffer);
    free_offline_file(filedata);
    char query[512];
    snprintf(query, sizeof(query), "DELETE FROM offline_files where receiver_id='%d' ;", client_session.id);
    do_query(query, conn);
}
// 文件从服务器发送给客户端
void file_transfer(int client_fd, char *filename, char *buffer)
{
    long file_size = get_file_size(filename);
    float num = file_size / (BUFSIZE - 12);
    printf("文件大小：%ld,分为% .2f次传输\n", file_size, num);
    printf("文件名：%s\n", filename);
    FILE *file = fopen(filename, "rb");
    int block_number = 1;

    char buf[BUFSIZE];
    FileTransferResponse *ack = (FileTransferResponse *)buf;

    recv(client_fd, ack, 12, 0);
    printf("  块编号：%u,请求码：%u,len:%u\n", ntohl(ack->block_number), ntohl(ack->request_code), ntohl(ack->length));
    ack->request_code = htonl(RESPONSE_FILE_ACK);
    ack->length = htonl(12);
    while (1)
    {
        ack->block_number = htonl(block_number);
        size_t bytes_read;
        bytes_read = fread(buf + 12, 1, BUFSIZE - 12, file);
        ssize_t sent;
        sent = send(client_fd, buf, bytes_read + 12, 0);
        if (sent <= 0)
        {
            // 如果发送失败，打印详细的错误信息
            perror("Send error");
            printf("Error code: %d\n", errno);
            printf("Error description: %s\n", strerror(errno));
        }

        printf("sent:%lu\n", sent);
        recv(client_fd, ack, 12, 0);
        printf("第%u块\n", ntohl(ack->block_number));

        block_number++;
        if (feof(file))
        {
            printf("End of file reached.\n");
            break;
        }
    }
    fclose(file);
}

void file_recv(int client_fd, char *buffer, MYSQL *conn)
{
    FileTransferRequest *req = (FileTransferRequest *)buffer;
    unsigned int file_size = ntohl(req->file_size);
    char filename[256];
    char recver[32];
    char session[64];
    strcpy(session, req->session_token);
    snprintf(filename, sizeof(filename), "../server_file/%s", req->file_name);
    strcpy(recver, req->receiver_username); // 先存有用的信息, 传文件给另一个客户端用专门的缓冲区

    // 检查套接字是否可写
    fd_set writefds;
    struct timeval timeout;
    FD_ZERO(&writefds);
    FD_SET(client_fd, &writefds);
    timeout.tv_sec = 10; // 设置超时时间
    timeout.tv_usec = 0;
    int retval = select(client_fd + 1, NULL, &writefds, NULL, &timeout);
    if (retval == -1)
    {
        perror("select failed");
        return;
    }
    else if (retval == 0)
    {
        printf("套接字不可写，连接可能已关闭\n");
        return;
    }
    char buf[BUFSIZE];
    FILE *file = fopen(filename, "wb");

    FileTransferResponse *ack = (FileTransferResponse *)buf;

    ack->block_number = 0;
    ack->length = htonl(12);
    ack->request_code = htonl(RESPONSE_FILE_ACK); // 发送确认之后开始传文件
    send(client_fd, ack, 12, 0);
    int block_number = 1;
    int total_write = 0;
    while (1)
    {
        ssize_t bytes_recv = recv(client_fd, buf, BUFSIZE, 0);

        size_t bytes_write;
        bytes_write = fwrite(buf + 12, 1, bytes_recv - 12, file);
        total_write += bytes_write;
        printf("total_write%d\n", total_write);
        if (bytes_write == bytes_recv - 12)
        { // 写入成功，发送ack，开始下一轮接收
            ack->block_number = *(unsigned int *)(buf + 8);
            printf("第%u块接收完成\n", ntohl(ack->block_number));
            send(client_fd, ack, 12, 0);
        }
        if (total_write >= file_size)
        {
            printf("接收完毕\n");
            break;
        }
    }
    fclose(file);                  // 如果在线，调用转发文件多函数直接发过去给接受者
    if (online_query(recver) == 0) // 如用户不在线，现将文件信息存入数据库
    {
        int sender_id = client_session.id;
        int recver_id = find_id_mysql(recver, conn);

        char query[512];
        MYSQL_RES *result;
        MYSQL_ROW row;
        snprintf(query, sizeof(query), "INSERT INTO offline_files "
                                       "(sender_id,receiver_id,file_path) VALUES ('%d','%d','%s');",
                 sender_id, recver_id, filename);
        do_query(query, conn);
    }
    else
    {
        int index = find_session_index(1, recver);

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_file_fd = accept(file_sock, (struct sockaddr *)&client_addr, &client_len);

        filetransfer_req(session_table[index].client_fd, filename);
        pthread_mutex_lock(&client_queues_lock);
        file_transfer(client_file_fd, filename, buf); // 发送给客户端的文件通过专门多套接字处理
        pthread_mutex_unlock(&client_queues_lock);
        close(client_file_fd);
    }
}

long get_file_size(const char *filename)
{
    FILE *file = fopen(filename, "rb"); // 以二进制只读模式打开文件
    if (!file)
    {
        perror("Error opening file");
        return -1;
    }
    fseek(file, 0, SEEK_END);     // 移动文件指针到文件末尾
    long file_size = ftell(file); // 获取当前文件指针的位置，即文件大小
    fclose(file);                 // 关闭文件
    return file_size;
}

OfflineFileData *offline_file_query(int recver_id, MYSQL *conn)
{
    char query[512];
    MYSQL_RES *result;
    MYSQL_ROW row;
    snprintf(query, sizeof(query), "SELECT u.username AS sender, of.file_path "
                                   "FROM offline_files of "
                                   "JOIN users u ON of.sender_id = u.id "
                                   "WHERE of.receiver_id = %d;",
             recver_id);
    result = do_query(query, conn);
    unsigned long num_rows = mysql_num_rows(result);
    if (num_rows == 0)
    {
        return NULL;
    }
    printf("Number of rows: %lu\n", num_rows);

    OfflineFileData *filedata = (OfflineFileData *)malloc(sizeof(filedata));

    int max_path_len = 64; // 文件路径最大长度
    char **offline_file = (char **)malloc(num_rows * sizeof(char *));
    if (offline_file == NULL)
    {
        printf("Memory allocation failed\n");
        return NULL;
    }

    int i = 0;
    for (i; i < num_rows; i++) // 分配空间存储查询到的离线文件信息
    {
        offline_file[i] = (char *)malloc(max_path_len * sizeof(char));
        if (offline_file[i] == NULL)
        {
            printf("Memory allocation failed for offline_file[%d]\n", i);
            return NULL;
        }
    }
    i = 0;
    while ((row = mysql_fetch_row(result)))
    {
        snprintf(offline_file[i], max_path_len, "发送者：%s,文件路径：%s", row[0], row[1]);
        i++;
    }
    filedata->num_files = num_rows;
    filedata->offline_file = offline_file;
    mysql_free_result(result);
    return filedata;
}

void free_offline_file(OfflineFileData *data)
{
    for (int i = 0; i < data->num_files; i++)
    {
        free(data->offline_file[i]);
    }
    free(data->offline_file);
    data->offline_file = NULL;
}

// 用于发送一个文件传输请求给客户端，接收确认之后开始调用函数传输文件
void filetransfer_req(int client_fd, char *filename)
{
    FileTransferRequest *req = (FileTransferRequest *)malloc(sizeof(FileTransferRequest));
    memset(req, 0, sizeof(FileTransferRequest));
    req->request_code = htonl(REQUEST_FILE_TRANSFER);
    req->length = htonl(sizeof(FileTransferRequest));
    long file_size = get_file_size(filename);
    printf("文件大小：%ld", file_size);
    req->file_size = htonl((unsigned int)file_size);

    filename = strstr(filename, "../server_file/"); // 只传一个文件。多个文件之后再扩展
    if (filename != NULL)
    {
        filename += strlen("../server_file/"); // 跳过“文件路径：”这部分，直接指向文件路径的内容
    }
    printf("%s\n", filename);
    strcpy(req->file_name, filename);

    ssize_t sent = send(client_fd, req, sizeof(FileTransferRequest), 0);
    printf("sent:%ld\n,", sent);
    free(req);
}