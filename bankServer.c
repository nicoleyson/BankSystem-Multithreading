#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "bankSystem.h"

// typedef enum { open, start, credit, debit, balance, finish, exit } operation;

// static void LOCK(pthread_mutex_t mutex);
// static void UNLOCK(pthread_mutex_t mutex);
static pid_t createChildProcess(void (func)(int), int opt);
static int allocateMEM();
static void deallocateMEM(int statusCode);
static int createSocket(const unsigned short port);
static void socketService(int clientID);
static void sessionSignalHandler(int SIGNAL);
static void sessionHandler(int port);
static void displayAccountStatus(int SIGNAL);
static void statusHandler(int timeout);
static void sig_handler(int SIGNAL);

static int sharedMemID = -1;
static int serverSocket = -1;
static pid_t childPID[MaxSessions+2];
static int serverPID[MaxSessions+2];
static struct bank *bank = NULL;		// bank's shared memory address

/* Thread scheduling functions */

/*
	The mutex object referenced by mutex shall be locked by calling this function.  If the mutex
	is already locked, the calling thread shall block until the mutex becomes available. This
	operation shall return with the mutex object referenced by mutex in the locked state with the
	calling thread as its owner.
*/
/*
static void LOCK(pthread_mutex_t mutex)
{
	if(pthread_mutex_lock(&mutex) != 0)
	{
		perror("Mutex lock error");
		exit(1);
	}
}
*/

/*
 This function will release the mutex object referenced by mutex.
*/
/*
static void UNLOCK(pthread_mutex_t mutex)
{
	if(pthread_mutex_unlock(&mutex) != 0)
	{
		perror("Mutex unlock error");
		exit(1);
	}
}
*/


/*
	 System call fork() is used to create processes. It takes no arguments and returns a process ID.
	 The purpose of fork() is to create a new process, which becomes the child process of the caller.
	 After a new child process is created, both processes will execute the next instruction following
	 the fork() system call. Therefore, we have to distinguish the parent from the child. This can be
	 done by testing the returned value of fork():
 */
static pid_t createChildProcess(void (func)(int), int opt)
{
	pid_t PID = fork();

	if (PID == 0)
	{
		func(opt);
	}
	else if (PID == -1)
	{
		perror("Server> Unable to create a process");
		exit(1);
	}
	else
	{
		printf("Server> Created child process PID %u\n", PID);
	}
	return PID;
}

/*
	Allocate Shared Memory
*/

static int allocateMEM()
{
	/*creates identifier to be used with the functions (semget, shmget, msgget) */
	key_t key = ftok("/", 6);
	if(key == -1)
	{
		perror("Unable to create identifier");
		return 1;
	}

	/* Ask for shared memory */
	sharedMemID = shmget(key, sizeof(struct bank), IPC_CREAT | IPC_EXCL | S_IRWXU | S_IRWXG | S_IROTH);
	if(sharedMemID == -1)
	{
		perror("Unable to allocate shared memory (-1)");
		return 1;
	}

	/* attach memory segment associated with this account's address */
	bank = (struct bank*) shmat(sharedMemID, NULL, SHM_RND);
	if(bank == (void*) -1)
	{
		perror("Unable to attach share memory");
		return 1;
	}

	/* initialize the bank's accounts */
	int idx1;
	for( idx1=0; idx1 < MaxAccounts; idx1++) pthread_mutex_init(&bank->accounts[idx1].mutex, NULL);

	bank->accountCounter = 0;
	if(pthread_mutex_init(&bank->mutex, NULL) != 0)
	{
		perror("Unable to initialize the thread");
		return 1;
	}
	memset (childPID, 0, sizeof(childPID));

	return 0;
}

/*
	Deallocate Shared Memory
*/
static void deallocateMEM(int statusCode)
{
	int idx1;

	if(bank == NULL) exit(statusCode);

	/* free mutex */
	pthread_mutex_destroy(&bank->mutex);
	/* destroy mutex */
	for(idx1=0; idx1 < MaxAccounts; idx1++) pthread_mutex_destroy(&bank->accounts[idx1].mutex);
	/* detach shared memory */
	if(shmdt(bank) == -1) perror("Unable to detach shared memory");
	/* deallocate shared memory */
	shmctl(sharedMemID, IPC_RMID, NULL);
	exit(statusCode);
}

/*
Create a socket and set it up to begin accepting commands
*/

static int createSocket(const unsigned short port)
{
    struct sockaddr_in childSocket;
	int opt = 1;

	memset(&childSocket, 0, sizeof(childSocket));
	childSocket.sin_family = AF_INET;
    childSocket.sin_addr.s_addr = INADDR_ANY;
    childSocket.sin_port = htons(port);

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    /* Set socket option at socket level to allow reusable socket*/
	if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) == -1)
	{
		perror("setsockopt");
		return 1;
	}
	/* Bind server socket address structure */
    if (bind(serverSocket, (struct sockaddr *)&childSocket, sizeof childSocket) == -1)
    {
		perror("bind");
		return 1;
    }
    /* Listen to client connection */
    if (listen(serverSocket, 10) == -1)
    {
		perror("listen");
		return 1;
    }
    return 0;
}

/*
	This function validate and process commands received from client.

	The command syntax allows the user to open accounts, to start sessions to serve specific
	accounts, and to exit the client process altogether. Here is the command syntax:
		• open accountname
		• start accountname
		• credit amount
		• debit amount
		• balance
		• finish
		• exit

 */
static void socketService(int clientID)
{
	int inService = 1;					/* set service on for current child process (will be used for infinite loop*/
	char line[MaxInputSize+commandLen], *command, *value;
	int clientIdx = -1;					/* controls which account ID child process is holding,
										   negative value means no current account opened  */

	int inputSize, idx1;
	while(inService)
	{
		inputSize = socketRead(clientID, line, sizeof(line));	/* read a line from client socket */

		command = strtok(line, " ");	/* client command */
		value = strtok(NULL, "\n");		/* value after command */

		if(strcmp(command, "open") == 0)
			/*				!!!!!! OPEN COMMAND !!!!!!
				The open command opens a new account for the bank. It is an error if the bank already has a
				full list of accounts, or if an account with the specified name already exists. A client in a customer
				session cannot open new accounts, but another client who is not in a customer session can open
				new accounts. The name specified uniquely identifies the account. An account name will be at
				most 100 characters. The initial balance of a newly opened account is zero. It is only possible to
				open one new account at a time, no matter how many clients are currently connected to the server
			*/
		{	/* Validate provided value */
			if(clientIdx != -1)
			{
				inputSize =  snprintf(line, MaxInputSize, "Command Error: Session is already opened");
			}
			else if(value == NULL)
			{
				inputSize = snprintf(line, MaxInputSize, "Command Error: Please provide a name");
			}
			else
			{
				if(bank->accountCounter >= (MaxAccounts-1))
				{	/* if not sucessful */
					inputSize =  snprintf(line, MaxInputSize, "Command Error: Unable to allocate resources");
				}
				else
				{
					/* Verify if account exists */
					for(idx1=0; idx1 < bank->accountCounter; idx1++)
					{
						if(strcmp(bank->accounts[idx1].name, value) == 0)
						{
							inputSize =  snprintf(line, MaxInputSize, "Command Error: Account name already exists.  Try start command");
							break;
						}
					}

					if(idx1 == bank->accountCounter)
					{
						/* Create Account */
//						LOCK(bank->mutex);
						if(pthread_mutex_lock(&bank->mutex)	!= 0) { perror("pthread_mutex_lock ERROR"); }
						clientIdx = bank->accountCounter++;
						strcpy(bank->accounts[clientIdx].name, value);
						bank->accounts[clientIdx].balance = 0.0;
						bank->accounts[clientIdx].inSession = 1;	//only after start ?
						pthread_mutex_lock(&bank->accounts[clientIdx].mutex);
//						UNLOCK(bank->mutex);
						if(pthread_mutex_unlock(&bank->mutex) != 0)	{ perror("pthread_mutex_unlock ERROR"); }
						inputSize = snprintf(line, MaxInputSize, "Account sucessfully created - %s", bank->accounts[clientIdx].name);
					}
				}
			}

		}
		else
		/*				!!!!!! START COMMAND !!!!!!
			The start command starts a customer session for a specific account. The credit, debit, balance
			and finish commands are only valid in a customer session. It is not possible to start more than one
			customer session in any single client window, although there can be concurrent customer sessions
			for different accounts in different client windows. Under no circumstances can there be concurrent
			customer sessions for the same account. It is possible to have any number of sequential client
			sessions.
		*/
		if(strcmp(command, "start") == 0)
		{
			if(clientIdx != -1)
			{	/* Validate provided value */
				inputSize = snprintf(line, MaxInputSize, "Command Error: Session is already opened");
			}
			else if(value == NULL)
			{
				inputSize = snprintf(line, MaxInputSize, "Command Error: Please provide an account name");
			}
			else
			{
				/* find the account - it MUST exist */
				int accountIsLocked = 0;
//				LOCK(bank->mutex);
				if(pthread_mutex_lock(&bank->mutex)	!= 0)
				{
					perror("pthread_mutex_lock ERROR");
				}
				for(idx1=0; idx1 < bank->accountCounter; idx1++)
				{
					if(strcmp(bank->accounts[idx1].name, value) == 0)
					{
						clientIdx = idx1;	/* save account ID */
						accountIsLocked = pthread_mutex_trylock(&bank->accounts[idx1].mutex); /* try to lock */
						break;
					}
				}
//				UNLOCK(bank->mutex);
				if(pthread_mutex_unlock(&bank->mutex) != 0)	{ perror("pthread_mutex_unlock ERROR"); }

				if(clientIdx == -1)
				{
					inputSize = snprintf(line, MaxInputSize, "Account not found");
				}
				else
				if (accountIsLocked)
				{
					inputSize = snprintf(line, MaxInputSize, "Account is locked by other user");
					/*
					For extra credit. insert the logic to try to lock the mutex every 2 seconds.  If it fails,
					send message ”waiting to start customer session for account” message to the appropriate
					client process
					*/
				}
				else
				/* Reopen the session */
				{
					bank->accounts[clientIdx].inSession = 1;
					inputSize = snprintf(line, MaxInputSize, "%s session reopened", bank->accounts[idx1].name);
				}
			}
		}
		else if(strcmp(command, "credit") == 0)
			/*				!!!!!! CREDIT COMMAND !!!!!!
			    The credit command add amounts to an account balance. Amounts are specified as floating-point numbers.
				Credit command complains if the client is not in a customer session. There are no constraints on the
				size of a credit.
			*/
		{
			if(clientIdx == -1)
			{
				inputSize = snprintf(line, MaxInputSize, "Start or open an account before using credit function");
			}
			else if(value == NULL)
			{
				inputSize = snprintf(line, MaxInputSize, "Please enter an amount to credit");
			}
			else
			{	/* no need to lock account, since only one user per account can be in session */
				bank->accounts[clientIdx].balance += atof(value);
				float balance = bank->accounts[clientIdx].balance;
				inputSize = snprintf(line, MaxInputSize, "%s new balance: %.2f", bank->accounts[clientIdx].name, balance);
			}

		}
		else if(strcmp(command, "debit") == 0)
			/*				!!!!!! DEBIT COMMAND !!!!!!
			 	The debit command subtract amounts from an account balance. Amounts are specified as floating-point numbers.
			 	Debit command complains if the client is not in a customer session.  Debit is invalid if the requested
				amount exceeds the current balance for the account. Invalid debit attempts leave the current balance
				unchanged.
			*/
		{
			if(clientIdx == -1)
			{
				inputSize = snprintf(line, MaxInputSize, "Start or open an account before using debit function");
			}
			else if(value == NULL)
			{
				inputSize = snprintf(line, MaxInputSize, "Please enter an amount to credit");
			}
			else
			{
				float amount_f = atof(value);
				if(amount_f > bank->accounts[clientIdx].balance)
				{
					inputSize = snprintf(line, MaxInputSize, "Overdraft is not allowed for this account");
				}
				else
				{	/* no need to lock account, since only one user per account can be in session */
					bank->accounts[clientIdx].balance -= amount_f;
					float balance = bank->accounts[clientIdx].balance;
					inputSize = snprintf(line, MaxInputSize, "%s new balance: %.2f", bank->accounts[clientIdx].name, balance);
				}
			}
		}
		else if(strcmp(command, "balance") == 0)
			/*				!!!!!! BALANCE COMMAND !!!!!!
			 	 The balance command simply returns the current account balance.
			*/
		{
			if(clientIdx == -1)
			{
				inputSize = snprintf(line, MaxInputSize, "Start or open an account before using balance function");
			}
			else
			{
				inputSize = snprintf(line, MaxInputSize, "%s current balance: %.2f", bank->accounts[clientIdx].name, bank->accounts[clientIdx].balance);
			}
		}
		else if(strcmp(command, "finish") == 0)
			/*				!!!!!! FINISH COMMAND !!!!!!
				The finish command finishes the customer session. Once the customer session is ended, it is
				possible to open new accounts or start a new customer session.
			*/
		{
			if(clientIdx == -1)
			{
				inputSize = snprintf(line, MaxInputSize, "No open account to finish");
			}
			else
			{
				bank->accounts[clientIdx].inSession = 0;	/* clear user session */
				inputSize = snprintf(line, MaxInputSize, "%s transactions finished", bank->accounts[clientIdx].name);
//				UNLOCK(bank->accounts[clientIdx].mutex);	/* unlock account */
				if(pthread_mutex_unlock(&bank->accounts[clientIdx].mutex) != 0)	{ perror("pthread_mutex_unlock ERROR"); }
				clientIdx = -1;								/* reset account index */
			}
		}
		else if(strcmp(command, "exit") == 0)
			/*				!!!!!! EXIT COMMAND !!!!!!
				The exit command disconnects the client from the server and ends the client process. The server process
				should continue execution.
			*/
		{
			inputSize = snprintf(line, MaxInputSize, "Have a good day");
			inService = 0;
			bank->accounts[clientIdx].inSession = 0;	/* mark current account off session */
	//		UNLOCK(bank->accounts[clientIdx].mutex);	/* unlock it for other */
			if(pthread_mutex_unlock(&bank->accounts[clientIdx].mutex) != 0)	{ perror("pthread_mutex_unlock ERROR"); }
			printf("User %i disconnect request granted\n", clientID);
		}
		else if(strcmp(command, "SIGINT") == 0){
			int idx1, status;
			pid_t processID = getpid();

			printf("Abnormal Exit Signal From Client Process %d...\n", processID);

			for(idx1=0; idx1 < MaxSessions+2; idx1++)
			{
				if(childPID[idx1] == processID)
				{
				printf("Freeing Session and Mutex Locks For Child Process...\n");
				bank->accounts[idx1].inSession = 0;
				if(pthread_mutex_unlock(&bank->accounts[idx1].mutex) != 0)	{ perror("pthread_mutex_unlock ERROR"); }
				}
			}

			//Kill Child Process
			kill(childPID[idx1], SIGTERM);
			waitpid(childPID[idx1], &status, 0);
			childPID[idx1] = 0;
			deallocateMEM(0);	//free memory
		}
		else
		{
			inputSize = snprintf(line, MaxInputSize, "I don't understand that command!!!");
		}

		if(socketWrite(clientID, line, inputSize) == 0) snprintf(line, MaxInputSize, "Do you need help?");
	}
	if(clientIdx == -1)
	{ /* This is abnormal.  Display as much info we can for troubleshooting */
		printf ("In-SESSION = %i", bank->accounts[clientIdx].inSession);
		printf("Client disconnected for unknown reason\n");
	}

	/* close the socket service */
	shutdown(clientID, SHUT_RDWR);
	close(clientID);
}

/*
 	 Signal handler for the sessionHandler
*/
static void sessionSignalHandler(int SIGNAL)
{
	int idx1 = 0;
	char *end = "TERMINATE";
	puts("Terminating Session Handler Child Processes...");
	if(SIGNAL == SIGTERM){
		for(;idx1 < MaxSessions+2; idx1++){
			printf("Terminating Child Process %d\n", childPID[idx1]);
			kill(childPID[idx1], SIGTERM);
			waitpid (childPID[idx1], 0, 0);
		}
	}
}

static void sessionHandler(int port)
{
	struct sockaddr_in childSocket;
	socklen_t socketLen = sizeof(childSocket);
	int idx1, clientIdx;

	signal(SIGCHLD, sessionSignalHandler); /* register for the child's signal */
	if(createSocket(port) == 1)
	{
		fprintf(stderr, "sessionHandler: Unable to setup socket\n");
		exit(1);
	}

	while((clientIdx = accept(serverSocket, (struct sockaddr *)&childSocket, &socketLen)) > 0)
	{
		printf("sessionHandler: Client connection from %s, client index %d\n", inet_ntoa(childSocket.sin_addr), clientIdx);
		for(idx1=0; idx1 < MaxSessions; idx1++)
		{
			if(childPID[idx1] == 0)
			{
				serverPID[idx1] = clientIdx;
				childPID[idx1] = createChildProcess(socketService, clientIdx);
				break;
			}
		}

		if(idx1 == MaxSessions)
			fprintf(stderr, "sessionHandler: Max clients reached\n");

		socketLen = sizeof(childSocket);
	}
	shutdown(serverSocket, SHUT_RDWR);
	close(serverSocket);
}

/*
	Server Side State Printout
	The bank server has to print out a complete list of all accounts every 20 seconds. The
	information printed for each account will include the account name, balance and ”IN SERVICE” if
	there is an account session for that particular account. New accounts cannot be opened while the bank
	is printing out the account information. Your implementation will uses timers, signal handlers and
	semaphores.
*/
static void displayAccountStatus(int SIGNAL)
{
	int idx1;
	char *header1 = "Name", *header2 = "Balance";

	if(SIGNAL == SIGTERM){
		puts("Ending Display....");
		return;
	}

//	LOCK(bank->mutex);
	if(pthread_mutex_lock(&bank->mutex)	!= 0)	{	perror("pthread_mutex_lock ERROR"); }
	if(bank->accountCounter == 0)
	{
		puts("Bank Status: No accounts in bank");
	}
	else
	{
		printf("%-100s %-12s\n", header1, header2);
		printf("---------------------------------------------------------------------------------------------------- ----------\n");

		for(idx1=0; idx1 < bank->accountCounter; idx1++)
		{	/* display status of each account */
			if(bank->accounts[idx1].inSession)
			{
				printf("%-100s %.2f\tIN SERVICE\n", bank->accounts[idx1].name, bank->accounts[idx1].balance);
			}
			else
			{
				printf("%-100s\t%.2f\n", bank->accounts[idx1].name, bank->accounts[idx1].balance);
			}
		}
	}
//	UNLOCK(bank->mutex);
	if(pthread_mutex_unlock(&bank->mutex) != 0)	{ perror("pthread_mutex_unlock ERROR"); }
};

static void statusHandler(int timeout)
{
	signal(SIGALRM, displayAccountStatus);
	while(1)
	{
//		printf ("Timeout = %i", timeout);
		alarm(timeout);
		sleep(timeout+1);
	}
}


/*
	Server signal handler
*/
static void sig_handler(int SIGNAL)
{
	if (SIGNAL == SIGINT){
		printf("Server> Exit signal detected Process %d\n", );
		printf("Server> Terminating Session Handler %d\n", childPID[MaxSessions+1]);
		printf("Server> Terminating Display Handler %d\n", childPID[MaxSessions]);
		kill(childPID[MaxSessions+1], SIGTERM);
		kill(childPID[MaxSessions], SIGTERM);
	}
}

int main(void)
{
	int status;

	memset(childPID, 0, sizeof(childPID));
	if(allocateMEM() == 1)  deallocateMEM(1);

	/* Create child processes */
	childPID[MaxSessions] 	= createChildProcess(statusHandler, SessionTimeout);
	childPID[MaxSessions+1]	= createChildProcess(sessionHandler, ServerPort);
	signal(SIGINT,  sig_handler);
	signal(SIGTERM, sig_handler);

	/* request status information from a child process whose process ID is childPID[x] */
	waitpid(childPID[MaxSessions], &status, 0);
	waitpid(childPID[MaxSessions+1], &status, 0);

	/* Cleanup */
	deallocateMEM(0);

	return 0;
}
