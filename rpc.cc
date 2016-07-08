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
#include <map>

// Structs
struct serverFuncKey {
	char* _name;
	int* _argTypes;

	void serverFuncKey(char* name, int* argTypes)
	{
		_name = name;
		_argTypes = argTypes;
	}
};

bool operator < (const serverFuncKey& key1, const serverFuncKey& key2);

// Binder socket file descriptor
int binder_socket_fd;

// RPC client socket fields
char rpc_socket_id;
int rpc_sock_fd;
int rpc_sock_port;

// Map
map<serverFuncKey, skeleton> server_functions;

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

	// Calls the binder, informing it that a server procedure with the
	// indicated name and list of argument types is available at this server
	if(binder_socket_fd < 0)
		return REGISTER_BINDER_DID_NOT_INITIATE;

	// Generate the msg
	// Get the local host name & port number
	char host_name[DEFAULT_CHAR_ARR_SIZE];
	gethostname(host_name, DEFAULT_CHAR_ARR_SIZE);

	struct sockaddr_in sock_ai;
	socklen_t sock_len = sizeof(sock_ai);
	getsockname(rpc_sock_fd, (struct sockaddr *)&sock_ai, &sock_len);
	unsigned short host_port = ntohs(sock_ai.sin_port);

	// Generate the data
	// The format will be:
	// char(server_id'\0')int(port)char(function_name'\0')int_arr(arg_types)
	int host_name_length = sizeof(host_name)/sizeof(char);
	int func_name_length = sizeof(name)/sizeof(char);
	int arg_types_length = 0;
	while(argTypes[arg_types_length])
		arg_types_length++;

	int msg_length =  host_name_length + func_name_length + 4 + arg_types_length * 4;
	char msg[msg_length];
	int cur_index = 0;
	// Add host_name'\0'
	memcpy(msg, host_name, host_name_length);
	cur_index += host_name_length;
	memcpy(msg + cur_index, &TERMINATING_CHAR, sizeof(char));
	cur_index += sizeof(char);

	// Add port
	memcpy(msg + cur_index, &host_port, sizeof(unsigned short));
	cur_index += sizeof(unsigned short);

	// Add function_name'\0'
	memcpy(msg + cur_index, &name, func_name_length);
	cur_index += func_name_length;
	memcpy(msg + cur_index, &TERMINATING_CHAR, sizeof(char));
	cur_index += sizeof(char);

	for(int i = 0; i < arg_types_length; i++)
	{
		memcpy(msg + cur_index, &argTypes[i], sizeof(int));
		cur_index += sizeof(int);
	}

	// Send the message
	int status_code = sendMessgae(binder_socket_fd, msg_length, MessageType.REGISTER, msg);
	if(status_code != SUCCESS)
		return status_code;

	// The first recieve is the REGISTER_SUCCESS / REGISTER_FAILURE
	int binder_message_type_int = recieveMessage_int(binder_socket_fd);
	MessageType binder_message_type = static_cast<MessageType>(binder_message_type_int);

	// The second recieve is the additional status code
	int binder_status_code = recieveMessage_int(binder_socket_fd);

	if(binder_message_type == MessageType.REGISTER_SUCCESS)
	{
		// If the binder register is successful, add the skeleton to map
		struct serverFuncKey key(name, argTypes);
		serverFuncKey[key] = f;
		return binder_status_code;
	} else if(binder_message_type == MessageType.REGISTER_FAILURE){
		return binder_status_code;
	}

	return REGISTER_BINDER_RET_UNRECON_TYPE;
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
int sendMessage(int socket_fd, unsigned int msg_len, MessageType msg_type, char msg_data[])
{
    // Format of the data is
    // int(msg_length)MessageType(type)char(msg)
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

int recieveMessage_int(int socket_fd)
{
	// Used to recieve status code from socket_fd
	// a 4 byte interget status code
	char ret[4];
	int rev_status_code = recieve_msg(socket_fd, 4, ret);
	if(rev_status_code != SUCCESS)
	 	return rev_status_code;

	// Convert the value to int
	return atoi(ret);
}

int recieve_msg(int socket_fd, int expect_len; char buf[])
{
	// Reads until expect_len is reached
	char* ptr = buf;
	while(expect_len > 0) {
		int rcv_len = recv(socket_fd, ptr, expect_len, 0);
		if(rcv_len == 0)
			break;
		else if(rcv_len < 0)
			return RECEIVE_ERROR;

		expect_len -= rcv_len;
		ptr += rcv_len;
	}

	return SUCCESS;
}
