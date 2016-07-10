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
#include <iostream>
#include <map>
#include <thread>
#include <pthread.h>

// Structs
struct serverFuncKey {
	char* _name;
	int* _argTypes;

    serverFuncKey(char* name, int* argTypes)
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

bool terminate;

int thread_count;
pthread_mutex_t thread_count_lock;

void alt_thread_count(int count)
{
	pthread_mutex_lock(&thread_count_lock);
	thread_count += count;
	pthread_mutex_unlock(&thread_count_lock);
}

// Map
std::map<serverFuncKey, skeleton> server_functions;

int size_of_type(int type)
{
	if(type == ARG_CHAR)
		return sizeof(char);
	else if(type == ARG_SHORT)
		return sizeof(short);
	else if(type == ARG_INT)
		return sizeof(int);
	else if(type == ARG_LONG)
		return sizeof(long);
	else if(type == ARG_DOUBLE)
		return sizeof(double);
	else if(type == ARG_FLOAT)
		return sizeof(float);
	return -1;
}


/*
 * Connection to binder via env varible
 * if connection is successful, binder_socket_fd will be set and success code
 * is returned
 * else, error code is returned
 */
int connect_binder()
{
    std::cout << "in connect_binder" << std::endl;
    // Check if binder is already connected
    std::cout <<binder_socket_fd << std::endl;
    if (binder_socket_fd > 0)
        return SUCCESS;

    // Get Binder's address & port
    char* binder_address = getenv(BINDER_ADDRESS_S);
    char* binder_port = getenv(BINDER_PORT_S);
    
    std::cout << "binder address: " << binder_address << std::endl;
    std::cout << "binder port: " << binder_port << std::endl;

    // Validates that the address and port is set
    if(binder_address == NULL)
        return INIT_BINDER_ADDRESS_NOT_FOUND;
    else if(binder_port == NULL)
        return INIT_BINDER_PORT_NOT_FOUND;

    // Make connection to binder
    struct addrinfo binder_hints, *binder_ai;

    // Get the address info of binder
    memset(&binder_hints, 0, sizeof binder_hints);
    binder_hints.ai_family = AF_UNSPEC;
    binder_hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(binder_address, binder_port, &binder_hints, &binder_ai);

    // Open socket
    binder_socket_fd = socket(binder_ai->ai_family, binder_ai->ai_socktype, binder_ai->ai_protocol);
    std::cout << "binder_socket_fd: " <<binder_socket_fd << std::endl;
    if (binder_socket_fd < 0)
        return INIT_BINDER_SOCKET_OPEN_FAILURE;

    // Make connection
    if (connect(binder_socket_fd, binder_ai->ai_addr, binder_ai->ai_addrlen) < 0)
        return INIT_BINDER_SOCKET_BIND_FAILURE;
    
    std::cout << "connected" << std::endl;

    return SUCCESS;
}


int rpcInit()
{
    std::cout << "in init"<< std::endl;
	// Create socket for client
	struct addrinfo rpc_hints, *rpc_ai;	// Address info

	// Set the address info
	memset(&rpc_hints, 0, sizeof rpc_hints);
	rpc_hints.ai_family = AF_UNSPEC;
	rpc_hints.ai_socktype = SOCK_STREAM;
	rpc_hints.ai_flags = AI_PASSIVE;

	// Get address info
	getaddrinfo(NULL, "0", &rpc_hints, &rpc_ai);
	rpc_sock_fd = socket(rpc_ai->ai_family, rpc_ai->ai_socktype, rpc_ai->ai_protocol);
    std::cout << "rpc_sock_fd: " << rpc_sock_fd << std::endl;
	if (rpc_sock_fd < 0)
	{
		return INIT_LOCAL_SOCKET_OPEN_FAILURE;
	}
	if (bind(rpc_sock_fd, rpc_ai->ai_addr, rpc_ai->ai_addrlen))
	{
		return INIT_LOCAL_SOCKET_BIND_FAILURE;
	}

	// start listening
	listen(rpc_sock_fd, RPC_BACKLOG);

	// Store rpc info
	//struct sockaddr_in sock_addr;
	//socklen_t sock_addr_len = sizeof sock_addr;

	// Get rpc sock name
	//rpc_socket_id = char[128];
	//getnameinfo((struct sockaddr*)&sock_addr, &sock_addr_len, rpc_socket_id, 128, NULL, 0, 0);

	// Get the rpc dock port
	//getsockname(rpc_sock, (struct sockaddr* ) &sock_addr, &sock_addr_len);
	//rpc_sock_port = ntohs(sock_addr.sin_port);

	// Connect to binder
	int binder_status_code = connect_binder();
	if (binder_status_code != SUCCESS)
		return binder_status_code;
    std::cout << "init complete" << std::endl;
	return SUCCESS;
}

// Called by client
int rpcCall(char* name, int* argTypes, void** args)
{
    /*
	// Connect to binder
	// Get Binder's address & port
	char* binder_address = getenv(BINDER_ADDRESS_S);
	char* binder_port = getenv(BINDER_PORT_S);

	// Validates that the address and port is set
	if(binder_address == NULL)
		return CALL_BINDER_ADDRESS_NOT_FOUND;
	else if(binder_port == NULL)
		return CALL_BINDER_PORT_NOT_FOUND;

	binder = socket_connect(binder_address, binder_port);

	// Make the msg
	// string(name'\0')int(argTypes)
	int func_name_length = string(name).size();
	int arg_types_length = 0;
	while(argTypes[arg_types_length])
		arg_types_length++;

	int msg_length = func_name_length + 1 + arg_types_length * 4;
	char msg[msg_length];

	// Sent the length & type
	send(binder, &msg_length, sizeof(int), 0);
	send(binder, &MessageType.LOC_REQUEST, sizeof(MessageType), 0);

	// Send the msg
	send(binder, name, func_name_length, 0);
	send(binder, TERMINATING_CHAR, sizeof(char), 0);
	send(binder, argTypes, arg_types_length * sizeof(int), 0);

	// Recieve the response type
	int res_type;
	recv(binder, &res_type, sizeof(MessageType), 0);

	char server_address[DEFAULT_CHAR_ARR_SIZE];
	char server_port[DEFAULT_CHAR_ARR_SIZE];
	if(res_type == MessageType.LOC_REQUEST_SUCCESS)
	{
		// successful response, get the address and port
		recv(binder, server_address, DEFAULT_CHAR_ARR_SIZE, 0);
		recv(binder, server_port, DEFAULT_CHAR_ARR_SIZE, 0);
	} else if (res_type == MessageType.LOC_REQUEST_FAILURE)
	{
		// If failure, get the reason code and return it
		int reason;
		recv(binder, &reason, sizeof(int), 0);
		close(binder);
		return reason;
	} else
	{
		close(binder);
		return UNKNOW_MSG_TYPE_RESPONSE;
	}

	// sent to server and to EXECUTE
	// Connection
	int server_fd = socket_connect(server_address, server_port);

	// Get the size
	int arg_size = 0;
	for(int arg = 0; arg < arg_types_length; arg++)
	{
		// For each arg
		int arg_type = get_arg_type(argTypes[arg]);

		int type_length = get_arg_length(argTypes[arg])
		int type_size = size_of_type(arg_type);

		arg_size += type_size * type_length;
	}

	msg_length = func_name_length + 1 + arg_types_length * 4 + arg_size;

	// Send the message to server
	// Message type and length
	send(server_fd, &msg_length, sizeof(int), 0);
	send(server_fd, MessageType.EXECUTE, sizeof(MessageType), 0);

	// function name and signiture
	send(server_fd, name, func_name_length, 0);
	send(server_fd, TERMINATING_CHAR, sizeof(char), 0);
	send(server_fd, argTypes, arg_types_length * 4, 0);

	// Send each arg
	for(int arg = 0; arg < arg_types_length; arg++)
	{
		int arg_type = get_arg_type(argTypes[args]);
		int arg_length = get_arg_length(argTypes[args]);
		int arg_size = size_of_type(arg_type);
		send(server_fd, (void*) args[arg], arg_length * arg_size, 0);
	}

	// Server response
	// Possible responses:
	// EXECUTE_SUCCESS, name, argTypes, args
	// EXECUTE_FAILURE, reasonCode
	int recv_msg_length;
	int recv_msg_type;
	recv(server_fd, &recv_msg_length, sizeof(int), 0);
	recv(server_fd, &recv_msg_type, sizeof(MessageType), 0);

	if(recv_msg_type == MessageType.EXECUTE_SUCCESS)
	{
		char function_name[DEFAULT_CHAR_ARR_SIZE];
		recv(server_fd, function_name, DEFAULT_CHAR_ARR_SIZE, 0);
		recv(server_fd, argTypes, arg_types_length * 4, 0);

		for (int arg = 0; arg < arg_types_length - 1; arg++) //for each argument
		{
			int arg_type = get_arg_type(argTypes[arg]);
			int arg_length = get_arg_length(argTypes[arg]);
			int arg_size = size_of_type(arg_type);

			recv(server_fd, args[arg], arg_length * arg_size, 0);
		}
	} else if (recv_msg_type == MessageType.EXECUTE_FAILURE)
	{
			int reasonCode;
			recv(server_fd, &reasonCode, sizeof(int), 0);
			close(server_fd);
			return reasonCode;
	} else
	{
		close(server_fd);
		return UNKNOW_MSG_TYPE_RESPONSE;
	}
     */
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
    int func_name_length = std::string(name).size();
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
    /*
	fd_set master;      // Master file descriptor
	fd_set read_fds;    // Temp file descriptor
	int fdmax;          // Max number of file descirptors

	int listener;       // Port listener
	struct sockaddr_storage remoteaddr; // connector's address information
	socklen_t addrlen;                  // Address length

	struct addrinfo hints, *ai, *p;     // Address info
	int rv;             // Result from getaddressinfo
	int ONE = 1;

	// clear the master and temp sets
	FD_ZERO(&master);
	FD_ZERO(&read_fds);

	// Set the address info
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	// Get address info
	if ((rv = getaddrinfo(NULL, "0", &hints, &ai)) != 0)
		return EXECUTE_ADDRINFO_ERROR;


	listener = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if(listener < 0)
		return EXECUTE_SOCKET_OPEN_FAILURE;

	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &ONE, sizeof(int));
	if(bind(listener, ai->ai_addr, ai->ai_addrlen) < 0)
		return EXECUTE_SOCKET_BIND_FAILURE;

	// Address info is no longer needed
	freeaddrinfo(ai);

	// Allow 5 connections from client
	listen(listener, RPC_BACKLOG);

	// add the listener to the master set
	FD_SET(listener, &master);

	// keep track of the biggest file descriptor
	fdmax = listener;

	pthread_t terminate_listener;

	// listen for binder termination
	terminate = false;
	pthread_create(&terminate_listener, NULL, wait_terminate, NULL);


	// Server main loop
	while(!terminate) {
		// copy master
		read_fds = master;
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) < 0)
			return EXECUTE_SELECTION_FAILURE;

		// Iterate through the connectons
		for(int index = 0; index < fdmax+1; index++)
		{
		    if (FD_ISSET(index, &read_fds))
				{
					if (index == listener)
					{
					    // New connection
					    addrlen = sizeof remoteaddr;
					    int newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);
							FD_SET(newfd, &master);     // Add new address to master
							if (newfd > fdmax)         // keep track of the max
								fdmax = newfd;
		        } else {
								// Create a new thread to handle the client's request so that it
								// wont be blocking
								pthread_t client_thread;
								alt_thread_count(1);
								pthread_create(&client_thread, NULL, client_request_handler, (void*) &index);
		        }
		    }
		}
	}

	while(thread_count < 0){}

	int msg_type = reasonCode.REQUEST_SUCCESS;
	send(binder_socket_fd, &msg_type, sizeof(MessageType), 0);
	close(binder_socket_fd);

	return msg_type;
     */
	return 0;
}

int rpcTerminate()
{
    /*
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
     */
	return 0;
}

// Wait for binder to sent terminate signal
void* wait_terminate(void* arg)
{
    /*
	// Format TERMINATE
	for(;;){
		int msg_type;
		recv(binder_socket_fd, &msg_type, sizeof(MessageType), 0);
		if(msg_type == MessageType.TERMINATE)
		{
			terminate = true;
			pthread_exit(NULL);
		}
	}
     */
    return 0;
}

void* client_request_handler(void* arg)
{
    /*
	// Increase the thread count
	alt_thread_count(1);

	int client_fd = *(int*) arg;
	int msg_type;
	int msg_length;
	int function_name[DEFAULT_CHAR_ARR_SIZE];

	recv(client_fd, &msg_length, sizeof(int), 0);
	recv(client_fd, &msg_type, sizeof(MessageType), 0);

	//func_name_length + 1 + arg_types_length * 4 + arg_size;
	// Get the function name
	recv(client_fd, function_name, DEFAULT_CHAR_ARR_SIZE, 0);

	int arg_size_tot = msg_length - DEFAULT_CHAR_ARR_SIZE;
	int *client_args = (int*) malloc(arg_size_tot);
	recv(client_fd, client_args, arg_size_tot, MSG_WAITALL);

	// Get the number of args
	int arg_types_length = get_arg_length(client_args);

	int *arg_types = (int*) malloc(arg_types_length * 4);
	void** args = (void**) malloc(arg_types_length * sizeof(void*));
	void* args_index = client_args + arg_types_length;

	for (int index = 0; index < arg_types_length; index++)
    {
        //see what type/len of arg we're dealing with
        int arg_type = get_arg_type(argTypes[index]);
        int arg_type_size = size_of_type(arg_type);
        int arr_size = get_arg_length(argTypes[index]);

        //temp holder
        void* args_holder = (void*) malloc(arr_size * arg_type_size);

        //copy the address
        *(args[index]) = args_holder;

        //copy the contents of array into temp holder
        for (int j = 0; j < arr_size; j++)
        {
            void* temp = (char*) args_holder + j * arg_type_size;
            memcpy(temp, argsIndex, arg_type_size);
            argsIndex = (void*) ((char*) argsIndex + arg_type_size);
        }
    }

		int result = MessageType.EXECUTE_FAILURE;
		skeleton s = server_functions[serverFuncKey(function_name, arg_types)];
		if(s != NULL) {
			result = s(arg_types, args);
		}

		if(result == MessageType.EXECUTE_SUCCESS)
		{
			int ret_msg_type = MessageType.EXECUTE_SUCCESS;
			send(client_fd, &msg_len, sizeof(int), 0);
			send(client_fd, &ret_msg_type, sizeof(MessageType), 0);
			send(client_fd, function_name, sizeof(function_name), 0);
			send(client_fd, arg_types, arg_types_length * 4, 0);

			for(int arg = 0; arg < arg_types_length; arg++)
			{
				int arg_type = get_arg_type(argTypes[args]);
				int arg_length = get_arg_length(argTypes[args]);
				int arg_size = size_of_type(arg_type);
				send(client_fd, (void*) args[arg], arg_length * arg_size, 0);
			}

		} else
		{
			// EXECUTE_FAILURE, reasonCode
			int ret_msg_type = MessageType.EXECUTE_FAILURE;
			int ret_msg_length = sizeof(int);
			send(client_fd, &ret_msg_length, sizeof(int), 0);
			send(client_fd, &ret_msg_type, sizeof(MessageType), 0);
			send(client_fd, &result, sizeof(int), 0);
		}

		close(client_fd);
		free(args);
		free(arg_types);
		free(client_args);

		alt_thread_count(-1);
		pthread_exit();
     */
    return 0;
}

/*
*	Sends the message to socket fd with package size of 64 bytes each
* returns SUCCESS is execution did not encounter error
* else return the error status code
*/
int sendMessage(int socket_fd, unsigned int msg_len, MessageType msg_type, char msg_data[])
{
    /*
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
     */
    return SUCCESS;
}

int recieveMessage_int(int socket_fd)
{
    /*
	// Used to recieve status code from socket_fd
	// a 4 byte interget status code
	char ret[4];
	int rev_status_code = recieve_msg(socket_fd, 4, ret);
	if(rev_status_code != SUCCESS)
	 	return rev_status_code;

	// Convert the value to int
	return atoi(ret);
     */
    return 0;
}

int recieve_msg(int socket_fd, int expect_len, char buf[])
{
    /*
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
     */
	return SUCCESS;
}

int socket_connect(char* host_name, char* port_num)
{
    /*
	int socket_fd;
	// Make connection to binder
	struct addrinfo hints, *ai;

	// Get the address info of binder
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(host_name, port_num, &hints, &ai);

	// Open socket
	socket_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (socket_fd < 0)
		return SOCKET_OPEN_FAILURE;

	// Make connection
	if (connect(socket_fd, ai->ai_addr, ai->ai_addrlen) < 0)
		return SOCKET_BIND_FAILURE;

	return socket_fd;
     */
    return 0;
}

int get_arg_length(int* arg_type)
{
	// The length is the last two values
	int length = (*arg_type) & 0x0000FFFF;
	if(length == 0)
	 	return 1;
	return length;
}

int get_arg_type(int* arg_type)
{
	// Second byte is the type
	int type = (*arg_type) & 0x00FF0000;
	return type >> 16;
}
