
CC = gcc
CFLAG = -g -I../include
LFLAG = -L../lib -llua51

OBJ = binary.o buffer.o

all : $(OBJ)
	$(CC) -lmingw32 --share -o binary.dll $(OBJ) $(LFLAG)

buffer.o : buffer.c
	$(CC) -c buffer.c $(CFLAG)
	
binary.o : binary.c
	$(CC) -c binary.c $(CFLAG)

clean :
	rm -f $(OBJ) binary.dll
