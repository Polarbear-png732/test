CC = gcc
CFLAGS = -O0 -g -pthread
TARGET = client
SRC = client.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)


