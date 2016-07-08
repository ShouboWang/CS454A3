#include "rpc.h"
#include "binder.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

// Binder socket file descriptor
int binder_socket_fd;

// RPC client socket fields
char rpc_socket_id;
int rpc_sock_fd;
int rpc_sock_port;


int rpcrpcInit()
{
	// Create socket for client
	struct addrinfo rpc_hints, *rpc_ai;	// Address info

	// Set the address info
	memset(&rpc_hints, 0, sizeof rpc_hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	// Get address info
	getaddrinfo(NULL, "0", &rpc_hints, &rpc_ai);
	rpc_sock_fd = socket(rpc_ai->ai_family, rpc_ai->ai_socktype, rpc_ai->ai_protocol);
	if (rpc_sock_fd < 0)
	{
		return INIT_LOCAL_SOCKET_OPEN_FAILURE;
	}
	if (bind(rpc_sock, rpc_ai->ai_addr, rpc_ai->ai_addrlen))
	{
		return INIT_LOCAL_SOCKET_BIND_FAILURE;
	}

	// start listening
	listen(rpc_sock, RPC_BACKLOG);

	// Store rpc info
	struct sockaddr_in sock_addr;
	socklen_t sock_addr_len = sizeof sock_addr;

	// Get rpc sock name
	rpc_socket_id = new char[128];
	getnameinfo((struct sockaddr*)&sock_addr, &lengths, rpc_socket_id, 128, NULL, 0, 0);

	// Get the rpc dock port
	getsockname(rpc_sock, (struct sockaddr* ) &sock_addr, &sock_addr_len);
	rpc_sock_port = ntohs(sock_addr.sin_port);

	// Connect to binder
	int binder_status_code = binderConnection();
	if (binder_status_code != SUCCES)
		return binder_status_code;
	return SUCCESS;
}
int rpcCall(char* name, int* argTypes, void** args)
{
	return 0;
}
int rpcCacheCall(char* name, int* argTypes, void** args)
{
	return 0;
}
int rpcRegister(char* name, int* argTypes, skeleton f)
{
	return 0;
}
int rpcExecute()
{
	return 0;
}
int rpcTerminate()
{
	// call binder to inform servcers to terminate
	// if no binder is created, then return with warning
	if(binder_socket_fd < 0)
		return TERMINATE_BINDER_DID_NOT_INITIATE;

	// Sent terminate request to binder
	char data[0];
	int status = sendMessgae(binder_socket_fd, 0, MessageType.TERMINATE ,data);

	// Close binder socket
	close(binder_socket_fd);
	return status;
}

/*
* Connection to binder via env varible
* if connection is successful, binder_socket_fd will be set and success code
* is returned
* else, error code is returned
*/
int binderConnection()
{
	// Check if binder is already connected
	if (binder_socket_fd >= 0)
		return SUCCESS;

	// Get Binder's address & port
	char* binder_address = getenv(BINDER_ADDRESS_S);
	char* binder_port = getenv(BINDER_PORT_S);

	// Validates that the address and port is set
	if(binder_address == NULL)
		return INIT_BINDER_ADDRESS_NOT_FOUND;
	else if(binder_port == NULL)
		return INIT_BINDER_PORT_NOT_FOUND;

	// Make connection to binder
	struct addrinfo binder_hints, *binder_ai;

	// Get the address info of binder
	memset(&binder_hints, 0, sizeof binder_hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(binder_address, binder_port, &binder_hints, &binder_ai);

	// Open socket
	binder_socket_fd = socket(binder_ai->ai_family, binder_ai->ai_socktype, binder_ai->ai_protocol);
	if (binder_socket_fd < 0)
		return INIT_BINDER_SOCKET_OPEN_FAILURE;

	// Make connection
	if (connect(binder_socket_fd, binder_ai->ai_addr, binder_ai->ai_addrlen) < 0)
		return INIT_BINDER_SOCKET_BIND_FAILURE;

	return SUCCESS;
}

/*
*	Sends the message to socket fd with package size of 64 bytes each
* returns SUCCESS is execution did not encounter error
* else return the error status code
*/
int sendMessgae(int socket_fd, unsigned int msg_len, MessageType msg_type, char msg_data[])
{
    // Format of the data is
    // msg_length(int)type(int)msg(char)
    // so the total would be 4 byte + 4 byte + msg_len
    int data_len = msg_len + 8;
    char data[data_len];

    // copy the data length
    memcpy(data, &msg_len, sizeof(int));
    memcpy(data + INT_BYTE_PADDING, &msg_type, sizeof(MessageType));
    memcpy(data + INT_BYTE_PADDING*2, msg_data, msg_len);

		// Send the data through socket_fd
		int have_sent = 0;
		while(have_sent < data_len)
		{
			// Send data in chunks
			int sent_len = send(socket_fd, data + have_sent, SOCKET_DATA_CHUNK_SIZE, 0);
			if(send_len < 0)
					return send_len;

			// If 0 is returned then the sent is terminated
			if(send_len == 0)
				break;

			have_sent += send_len;
		}

		return SUCCESS;
}
