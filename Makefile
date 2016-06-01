CC=cc
CFLAGS=-Wall -ggdb
LDFLAGS=-pthread

all: bankServer bankClient

bankServer.o: bankServer.c
	$(CC) $(CFLAGS) -c bankServer.c

bankClient.o: bankClient.c
	$(CC) $(CFLAGS) -c bankClient.c

bankServer: bankServer.o
	$(CC) $(CFLAGS) bankServer.o -o bankServer $(LDFLAGS)

bankClient: bankClient.o
	$(CC) $(CFLAGS) bankClient.o -o bankClient $(LDFLAGS)

clean:
	rm *.o bankServer bankClient
