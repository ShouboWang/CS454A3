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
#include <sys/select.h>
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
    std::string _name;
	int* _argTypes;

    serverFuncKey(std::string name, int* argTypes)
	{
		_name = name;
		_argTypes = argTypes;
	}

    bool operator < (const serverFuncKey& foo1) const {
        return _name.compare(foo1._name);
    }
};

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
//bool operator < (const serverFuncKey& key1, const serverFuncKey& key2);

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

int socket_connect(char* host_name, char* port_num)
{
     int socket_fd;
     // Make connection to binder
     struct addrinfo hints, *ai;
     
     // Get the address info of binder
     memset(&hints, 0, sizeof hints);
     hints.ai_family = AF_INET;
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
    binder_hints.ai_family = AF_INET;
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

// Wait for binder to sent terminate signal
void* wait_terminate(void* arg)
{
    // Format TERMINATE
    for(;;){
        int msg_type;
        recv(binder_socket_fd, &msg_type, sizeof(MessageType), 0);
        if(msg_type == TERMINATE)
        {
            terminate = true;
            pthread_exit(NULL);
        }
    }
    return 0;
}

void* client_request_handler(void* arg)
{
    // Increase the thread count
    alt_thread_count(1);
    
    int client_fd = *(int*) arg;
    int msg_type;
    int msg_length;
    char function_name[DEFAULT_CHAR_ARR_SIZE];
    
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
        int arg_type = get_arg_type(&arg_types[index]);
        int arg_type_size = size_of_type(arg_type);
        int arr_size = get_arg_length(&arg_types[index]);
        
        //temp holder
        void* args_holder = (void*) malloc(arr_size * arg_type_size);
        
        //copy the address
        args[index] = args_holder;
        
        //copy the contents of array into temp holder
        for (int j = 0; j < arr_size; j++)
        {
            void* temp = (char*) args_holder + j * arg_type_size;
            memcpy(temp, args_index, arg_type_size);
            args_index = (void*) ((char*) args_index + arg_type_size);
        }
    }
    
    int result = EXECUTE_FAILURE;
    struct serverFuncKey key(std::string(function_name), arg_types);
    skeleton s = server_functions[key];
    if(s != NULL) {
        result = s(arg_types, args);
    }
    
    if(result == EXECUTE_SUCCESS)
    {
        int ret_msg_type = EXECUTE_SUCCESS;
        send(client_fd, &msg_length, sizeof(int), 0);
        send(client_fd, &ret_msg_type, sizeof(MessageType), 0);
        send(client_fd, function_name, sizeof(function_name), 0);
        send(client_fd, arg_types, arg_types_length * 4, 0);
        
        for(int arg = 0; arg < arg_types_length; arg++)
        {
            int arg_type = get_arg_type(&arg_types[arg]);
            int arg_length = get_arg_length(&arg_types[arg]);
            int arg_size = size_of_type(arg_type);
            send(client_fd, (void*) args[arg], arg_length * arg_size, 0);
        }
        
    } else
    {
        // EXECUTE_FAILURE, reasonCode
        int ret_msg_type = EXECUTE_FAILURE;
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
    pthread_exit(NULL);
    return 0;
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
	// Connect to binder
	// Get Binder's address & port
	char* binder_address = getenv(BINDER_ADDRESS_S);
	char* binder_port = getenv(BINDER_PORT_S);

	// Validates that the address and port is set
	if(binder_address == NULL)
		return CALL_BINDER_ADDRESS_NOT_FOUND;
	else if(binder_port == NULL)
		return CALL_BINDER_PORT_NOT_FOUND;

	int binder = socket_connect(binder_address, binder_port);

	// Make the msg
	// string(name'\0')int(argTypes)
	int arg_types_length = 0;
	while(argTypes[arg_types_length])
		arg_types_length++;

    // length, LOC_REQUEST, name, argTypes
	int msg_length = sizeof(int) + sizeof(MessageType) + DEFAULT_CHAR_ARR_SIZE + arg_types_length * sizeof(int);

	// Sent the length & type
	send(binder, &msg_length, sizeof(int), 0);
    int msg_type = LOC_REQUEST;
	send(binder, &msg_type, sizeof(MessageType), 0);

	// Send the msg
	send(binder, name, DEFAULT_CHAR_ARR_SIZE, 0);
	send(binder, argTypes, arg_types_length * sizeof(int), 0);

	// Recieve the response type
	int res_type;
	recv(binder, &res_type, sizeof(MessageType), 0);

	char server_address[DEFAULT_CHAR_ARR_SIZE];
	char server_port[DEFAULT_CHAR_ARR_SIZE];
	if(res_type == LOC_SUCCESS)
	{
		// successful response, get the address and port
		recv(binder, server_address, DEFAULT_CHAR_ARR_SIZE, 0);
		recv(binder, server_port, DEFAULT_CHAR_ARR_SIZE, 0);
	} else if (res_type == LOC_FAILURE)
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
		int arg_type = get_arg_type(&argTypes[arg]);

        int type_length = get_arg_length(&argTypes[arg]);
		int type_size = size_of_type(arg_type);

		arg_size += type_size * type_length;
	}
    
    // length EXECUTE, name, argTypes, args
	msg_length = sizeof(int) + sizeof(MessageType) + DEFAULT_CHAR_ARR_SIZE + arg_types_length * sizeof(int) + arg_size;

	// Send the message to server
	// Message type and length
	send(server_fd, &msg_length, sizeof(int), 0);
    msg_type = EXECUTE;
	send(server_fd, &msg_type, sizeof(MessageType), 0);

	// function name and signiture
	send(server_fd, name, DEFAULT_CHAR_ARR_SIZE, 0);
	send(server_fd, argTypes, arg_types_length * sizeof(int), 0);

	// Send each arg
	for(int arg = 0; arg < arg_types_length; arg++)
	{
		int arg_type = get_arg_type(&argTypes[arg]);
		int arg_length = get_arg_length(&argTypes[arg]);
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

	if(recv_msg_type == EXECUTE_SUCCESS)
	{
		char function_name[DEFAULT_CHAR_ARR_SIZE];
		recv(server_fd, function_name, DEFAULT_CHAR_ARR_SIZE, 0);
		recv(server_fd, argTypes, arg_types_length * 4, 0);

		for (int arg = 0; arg < arg_types_length - 1; arg++) //for each argument
		{
			int arg_type = get_arg_type(&argTypes[arg]);
			int arg_length = get_arg_length(&argTypes[arg]);
			int arg_size = size_of_type(arg_type);

			recv(server_fd, args[arg], arg_length * arg_size, 0);
		}
	} else if (recv_msg_type == EXECUTE_FAILURE)
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
    
    return 0;
}

int rpcCacheCall(char* name, int* argTypes, void** args)
{
	return 0;
}

int rpcRegister(char* name, int* argTypes, skeleton f)
{

    std::cout << "==========" << std::endl;
    std::cout << "In rpc register" << std::endl;
    // Calls the binder, informing it that a server procedure with the
	// indicated name and list of argument types is available at this server
	if(binder_socket_fd < 0)
		return REGISTER_BINDER_DID_NOT_INITIATE;

	// Generate the msg
	// Get the local host name & port number
	char host_name[DEFAULT_CHAR_ARR_SIZE];
	gethostname(host_name, DEFAULT_CHAR_ARR_SIZE);
    std::cout << "host name: " << host_name << std::endl;


	struct sockaddr_in sock_ai;
	socklen_t sock_len = sizeof(sock_ai);
	getsockname(rpc_sock_fd, (struct sockaddr *)&sock_ai, &sock_len);
	unsigned short host_port = ntohs(sock_ai.sin_port);

    std::cout << "port host: " << host_port << std::endl;
	// Generate the data
	// The format will be:
	// char(server_id'\0')int(port)char(function_name'\0')int_arr(arg_types)
	int host_name_length = sizeof(host_name)/sizeof(char);
    int func_name_length = (int)std::string(name).size();
	int arg_types_length = 0;
	while(argTypes[arg_types_length])
		arg_types_length++;

    std::cout << "arg_types_length: " << arg_types_length << std::endl;
    std::cout << "host_name_length: " << host_name_length << std::endl;
    std::cout << "func_name_length: " << func_name_length << std::endl;

		// int(size) + messagetype(type) + DEFAULT_CHAR_ARR_SIZE + unsigned short + DEFAULT_CHAR_ARR_SIZE + int_arr(arg_types)
		int msg_length =  sizeof(int) + sizeof(MessageType) + DEFAULT_CHAR_ARR_SIZE*2 + sizeof(unsigned short) + arg_types_length * sizeof(int);

		send(binder_socket_fd, &msg_length, sizeof(int), 0);
		std::cout << "sending message length " << msg_length << std::endl;

		int msg_type = REGISTER;
    send(binder_socket_fd, &msg_type, sizeof(MessageType), 0);
    std::cout << "sending message type " << msg_type << std::endl;

    // Sending serverid, port, function_name, arg_type
    send(binder_socket_fd, host_name, DEFAULT_CHAR_ARR_SIZE, 0);
    std::cout << "sending host_name " << host_name << std::endl;

    send(binder_socket_fd, &host_port, sizeof(unsigned short), 0);
    std::cout << "sending host_port " << host_port << std::endl;

    send(binder_socket_fd, name, DEFAULT_CHAR_ARR_SIZE, 0);
    std::cout << "sending function name " << name << std::endl;

    std::cout << "arg length "<< arg_types_length * sizeof(int) << std::endl;
    send(binder_socket_fd, argTypes, arg_types_length * sizeof(int), 0);
    for(int i = 0; i < arg_types_length; i++)
    {
      std::cout << "sending argTypes " << argTypes[i] << std::endl;
    }

		int response_length;
		recv(binder_socket_fd, &response_length, sizeof(int), 0);
		std::cout << "response_length: " << response_length << std::endl;

		// The first recieve is the REGISTER_SUCCESS / REGISTER_FAILURE
    int response_status;
    recv(binder_socket_fd, &response_status, sizeof(MessageType), 0);
		std::cout << "response_status: " << response_status << std::endl;

	// The second recieve is the additional status code
    int response_code;
    recv(binder_socket_fd, &response_code, sizeof(int), 0);
		std::cout << "response_code: " << response_code << std::endl;

	if(response_status == REGISTER_SUCCESS)
	{
		// If the binder register is successful, add the skeleton to map
        struct serverFuncKey key(std::string(name), argTypes);
		server_functions[key] = f;
		std::cout << "out1" << std::endl;
		return response_code;
	} else if(response_status == REGISTER_FAILURE)
		return response_code;

	std::cout << "out3" << std::endl;
	return REGISTER_BINDER_RET_UNRECON_TYPE;
}

int rpcExecute()
{

    fd_set master;      // Master file descriptor
    fd_set read_fds;    // Temp file descriptor
    int fdmax;          // Max number of file descirptors

    int listener;       // Port listener
    struct sockaddr_storage remoteaddr; // connector's address information
    socklen_t addrlen;                  // Address length

    struct addrinfo hints, *ai;     // Address info
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

    int msg_type = REQUEST_SUCCESS;
    send(binder_socket_fd, &msg_type, sizeof(MessageType), 0);
    close(binder_socket_fd);

    return msg_type;
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