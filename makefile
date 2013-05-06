
CC = gcc
CFLAGS = -I../include -g
LFLAGS = -L../lib -llua51
TARGET = binary.dll

SRC = $(wildcard *.c)
OBJ = $(patsubst %.c, %.o, $(SRC))

%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<
	
all : $(OBJ)
	$(CC) -lmingw32 --share -o $(TARGET) $(OBJ) $(LFLAGS)

clean :
	rm -f $(TARGET) $(OBJ)