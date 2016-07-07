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

// Socket connection to binder
int binder_socket;

// RPC client socket fields
char rpc_socket_id;
int rpc_sock;
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
	rpc_sock = socket(rpc_ai->ai_family, rpc_ai->ai_socktype, rpc_ai->ai_protocol);
	bind(rpc_sock, rpc_ai->ai_addr, rpc_ai->ai_addrlen);

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

  // Address info is no longer needed
  freeaddrinfo(rpc_ai);

	// Connect to binder
	// Get Binder's address & port
	char* binder_address = getenv(BINDER_ADDRESS_S);
	char* binder_port = getenv(BINDER_PORT_S);

	// Validates that the address and port is set
	if(binder_address == NULL) {
		return ERROR_BINDER_ADDRESS_NOT_FOUND;
	}
	if(binder_port == NULL) {
		return ERROR_BINDER_PORT_NOT_FOUND;
	}

	// Make connection to binder
	int sockfd;
  struct addrinfo binder_hints, *binder_ai;

	// Get the address info of binder
  memset(&binder_hints, 0, sizeof binder_hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(binder_address, binder_port, &binder_hints, &binder_ai);

	// make connection to binder
	binder_socket = socket(binder_ai->ai_family, binder_ai->ai_socktype, binder_ai->ai_protocol);
	connect(binder_socket, binder_ai->ai_addr, binder_ai->ai_addrlen)

	// free address info
	freeaddrinfo(binder_ai);

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
	return 0;
}
