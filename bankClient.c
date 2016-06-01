
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include "bankSystem.h"

static void * getInput(void * param);
static void * displayResponse(void * param);
static int serviceConnect(const char * hostName);

static int clientSocket = -1;
static pthread_t inputThread = -1;
static pthread_t outputThread = -1;

/*
	Get Input
*/
static void * getInput(void * param)
{
	int lineLen;
	char line[MaxInputSize+commandLen];
	printf("client> ");
	while(1)
	{
		fgets(line, MaxInputSize, stdin);
		lineLen = strlen(line);
		if (lineLen > 1)
		{
			if(send(clientSocket, line, lineLen, 0) != lineLen)	/* send command to bank server */
				perror("bankClient: Unable to send to server");

			if(strncmp(line, "exit", 4) == 0) break;
			sleep(commandThrottle);
		}
		printf("client> ");
	}
	pthread_exit(NULL);
}

/*
	Display bank server's response
*/
static void * displayResponse(void * param)
{
	char line[MaxInputSize+1];

	while(socketRead(clientSocket, line, sizeof(line)) > 0)
	{
		printf("server> %s\n", line);
	}
	pthread_exit(NULL);
}

/*
	Connect to bank server service
*/
static int serviceConnect(const char * hostName)
{
	struct hostent *hostEntries;
	struct sockaddr_in serverAddress;
	char serverIP[INET_ADDRSTRLEN+1];

	/*create a socket for TCP and save its descriptor in socket */
	if( (clientSocket = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("Unable to create socket");
		return -1;
	}

	/* Extract IP address */
	hostEntries = gethostbyname(hostName);
	if(hostEntries == NULL)
	{
		perror("Unable to extract gethostbyname");
		return -1;
	}

	/* Construct detailed server structure */
	memset(&serverAddress, 0, sizeof(struct sockaddr_in));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port   = htons(ServerPort);
	memcpy(&serverAddress.sin_addr, hostEntries->h_addr_list[0], hostEntries->h_length);

	/* convert network address structure "h_addr_list" in the "h_addrtype" address family into serverIP */
	if(inet_ntop(hostEntries->h_addrtype, hostEntries->h_addr_list[0], serverIP, INET_ADDRSTRLEN) == NULL)
	{
		perror("Unable to convert network address structure to string IP address");
		return -1;
	}

	while(1)
	{
		printf("Connecting to %s ...\n", serverIP);
		if(connect(clientSocket, (struct sockaddr*) &serverAddress, sizeof(serverAddress)) == -1)
		{
			perror("Unable to connect to server");
			sleep(connectRetryInterval);
		}
		else
		{
			printf("Connected to server on %s port %i\n", hostName, ServerPort);
			break;
		}
	}

	return clientSocket;
}

static void sig_handler(int SIGNAL)
{
	char *line = "SIGINT";
	int times = 0;

	if (SIGNAL == SIGINT)
	{
		puts("Client> Exit signal detected.");
		if(clientSocket >= 0){
			printf("Client> Sending Shutdown Signal To Server. %s\n", line);
			
			while(times < 5){
				if(send(clientSocket, line, strlen(line)+1, 0) != strlen(line)+1){
					puts("Client> Unable To Send To Server. Trying Again!");
					times++;
				}else{
					puts("Client> Successfully Sent Shutdown Signal To Server!");
					break;
				}
			}
		}
		
		puts("Client> Shutting Down...");
		exit(0);
	}
}

int main(const int argc, char * const argv[])
{
	if(argc != 2)
	{
		fprintf(stderr, "Error!  Provide a hostname.  Usage: %s hostname\n", argv[0]);
		return 1;
	}

	clientSocket = serviceConnect(argv[1]);
	if(clientSocket < 0) return 1;

	if( (pthread_create(&inputThread, NULL, getInput,  NULL) != 0) ||
		(pthread_create(&outputThread,NULL, displayResponse, NULL) != 0) )
	{
		perror("Unable to create thread");
		return 1;
	}

	signal(SIGINT, sig_handler);

	pthread_join(outputThread, NULL);
	pthread_join(inputThread,  NULL);

	close(clientSocket);
	return 0;
}
