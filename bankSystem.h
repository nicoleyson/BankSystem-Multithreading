/*
Your server process should spawn a single session-acceptor thread. The session-acceptor thread
will accept incoming client connections from separate client processes. For each new connection, the
session-acceptor thread should spawn a separate client-service thread that communicates exclusively
with the connected client. You may have more than one client connecting to the server concurrently,
so there may be multiple client-service threads running concurrently in the same server process.
The bank server process will maintain a simple bank with multiple accounts. There will be a
maximum of 20 accounts. Initially your bank will have no accounts, but clients may open accounts
as needed. Information for each account will consist of:
• ACCOUNT NAME (a string up to 100 characters long)
• CURRENT BALANCE (a floating-point number)
• IN-SESSION FLAG(a boolean flag indicating whether or not the account is currently being
serviced) The server will handle each client in a separate client-service thread. Keep in mind that any
client can open a new account at any time, so adding accounts to your bank must be a mutex-protected
operation.

Command entry must be THROTLED. This means that a command can only be entered every TWO (2)
SECONDS. This deliberately slows down client interaction with the server and simulates many thousands
of clients using the bank server. Your client implementation would have two threads: a commandinput
thread to read commands from the user and send them to the server, and a response-output

The bank server has to print out a complete list of all accounts EVERY 20 SECONDS. The
information printed for each account will include the account name, balance and ”IN SERVICE” if
there is an account session for that particular account. New accounts cannot be opened while the bank
is printing out the account information. Your implementation will uses timers, signal handlers and
sempaphores.

 */

#ifndef BankSystem
#define BankSystem

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "bankSystem.h"

/*!!!!!!!!!!!  CONTROL SECTION !!!!!!!!!!!*/
#define ServerPort				8442	/* preferably from 1025 to 65535, avoid IANA standard port < 1025  */
#define AccountNameSize			100		/* (characters) Refer assignment descriptions */
#define MaxAccounts				20		/* Refer to assignment descriptions */
#define commandThrottle			1		/* seconds to wait to accept new input to avoid screen input/output overlapped */
#define connectRetryInterval	3		/* seconds before attempting reconnection */
#define commandLen				7		/* maximum command length + 1 (space) */
#define MaxSessions				20		/* Just an assumption.  Can be extended */
#define SessionTimeout			20		/* (seconds) Interval to display bank status */
#define MaxInputSize			256		/* Just an assumption.  Can be extended */

char msgDelimiter = '\n';

struct account
{
	char name[AccountNameSize];
	float balance;
	int inSession;
	pthread_mutex_t mutex;
};

struct bank
{
	struct account accounts[MaxAccounts];
	int accountCounter;
	pthread_mutex_t mutex;
};
/*
 	 Send message to client
 */
int socketWrite(int socket, char * socketBuffer, int bufferLen)
{
	socketBuffer[bufferLen++] = msgDelimiter;	/* Message terminated by line character */
	if(send(socket, socketBuffer, bufferLen, 0) != bufferLen)
	{	/* send to client */
		perror("socketWrite:  Unable to send message to client");
		bufferLen = 0;
	}
	return bufferLen;
};

/*
 	 Read message from/to server and client (server <--> client)
 */
int socketRead(int socket, char * socketBuffer, int bufferLen)
{
	int idx1=0;
	while((recv(socket, &socketBuffer[idx1], 1, 0) == 1) && (idx1 < bufferLen))
	{
		if(socketBuffer[idx1++] == msgDelimiter)	/* Message terminated by line character */
			break;
	}
	socketBuffer[--idx1] = '\0';
	return idx1;
}

#endif
