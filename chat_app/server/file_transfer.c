#include "server.h"

/*
void file_transfer(int client_fd, char *buffer, MYSQL *conn)
{
    long file_size = get_file_size(file_name);
    float num = file_size / BUFSIZE - 12;
    printf("文件大小：%ld,分为% .2f次传输\n", file_size, num);
    FILE *file = fopen(file_name, "rb");
    int block_number = 1;
    while (1)
    {
        FileTransferResponse *ack = (FileTransferResponse *)buffer;
        ack->block_number = htonl(block_number);
        ack->request_code = htonl(RESPONSE_FILE_ACK);
        ack->length = htonl(12);
        fread(buffer + 12, 1, BUFSIZE - 12, file);

        send(client_fd, buffer, BUFSIZE, 0);
        recvfrom(client_fd, ack, 12, 0);
        printf("第%u块\n", ack->block_number);

        block_number++;
        if (feof(file))
        {
            printf("End of file reached.\n");
            break;
        }
    }
}
*/
void file_recv(int client_fd, char *buffer, MYSQL *conn)
{
    FileTransferRequest *req = (FileTransferRequest *)buffer;
    unsigned int file_size=ntohl(req->file_size);
    char filename[256];
    char recver[32];
    char session[64];
    strcpy(session, req->session_token);
    snprintf(filename, sizeof(filename), "../server_file/%s", req->file_name);
    strcpy(recver, req->receiver_username); // buffer 后续用于传文件，先存有用的信息
    
    FILE *file = fopen(filename, "wb");
    FileTransferResponse *ack = (FileTransferResponse *)buffer;

    ack->block_number = 0;
    ack->length = htonl(12);
    ack->request_code = htonl(RESPONSE_FILE_ACK); // 发送确认之后开始传文件
    send(client_fd, ack, 12, 0);
    int block_number = 1;
    int total_write=0;
    while (1)
    {
        ssize_t bytes_recv = recv(client_fd, buffer, BUFSIZE, 0);
        
        size_t bytes_write;
        bytes_write = fwrite(buffer + 12, 1, bytes_recv - 12, file);
        total_write +=bytes_write;
        printf("total_write%d\n",total_write);
        if (bytes_write == bytes_recv - 12)
        {                                                                                                                        // 写入成功，发送ack，开始下一轮接收
            ack->block_number = *(unsigned int *)(buffer + 8);
            printf("第%u块接收完成\n", ntohl(ack->block_number));
            send(client_fd, ack, 12, 0);
        }
        if(total_write==file_size){
            printf("接收完毕\n");
            break;
        }
    }
    fclose(file);
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
