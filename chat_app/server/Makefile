CC = gcc
CFLAGS = -g -pthread
LIBS = -lmysqlclient

SRCS = main.c init.c server.c event_queue.c group_manage.c friend_manage.c
OBJS = $(SRCS:.c=.o)

TARGET =server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS) $(CFLAGS)

%.o: %.c
	$(CC) -c $(CFLAGS) $<

clean:
	rm -f $(OBJS)

