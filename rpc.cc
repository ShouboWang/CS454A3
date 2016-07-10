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
#include <sstream>
#include <map>
#include <thread>
#include <pthread.h>

// Structure used as key for mapping skeleton
struct FuncSignature {
    std::string name;
    int* argTypes;
    int argSize;
    FuncSignature(std::string name, int* argTypes, int argSize) : name(name), argTypes(argTypes), argSize(argSize) {}
};

bool operator == (const FuncSignature &l, const FuncSignature &r) {
    
    if (l.name == r.name && l.argSize == r.argSize) {
        int i = 0;
        while (i < l.argSize) {
            if (l.argTypes[i] != r.argTypes[i]) {
                return false;
            }
            i++;
        }
        return true;
    }
    return false;
    
}

// Mapping of signiture to skeleton
std::map<FuncSignature*, skeleton> server_functions;

// Binder and server socket file descriptor
int binder_socket_fd;
int rpc_sock_fd;

// Server execute terminating condition
bool terminate;

// Thread mutex lock
int thread_count;
pthread_mutex_t thread_count_lock;

// Inc/Dec thread count
void alt_thread_count(int count)
{
	pthread_mutex_lock(&thread_count_lock);
	thread_count += count;
	pthread_mutex_unlock(&thread_count_lock);
}

// given the arg type, return the length of arg
int get_arg_length(int* arg_type)
{
    // The length is the last two values
    int length = (*arg_type) & 0x0000FFFF;
    if(length == 0)
        return 1;
    return length;
}

// given the arg type, return the type of arg
int get_arg_type(int* arg_type)
{
    // Second byte is the type
    int type = (*arg_type) & 0x00FF0000;
    return type >> 16;
}

// given the int type value, return the size of type
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

// Given the host name and port number, establish connection
int socket_connect(char* host_name, char* port_num)
{
    
    int socket_fd;
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

// Threading //
// Thread for waiting on termination signal from binder
void* wait_terminate(void* arg)
{
    for(;;){
        int msg_type;
        recv(binder_socket_fd, &msg_type, sizeof(MessageType), 0);
       
        // Terminate
        if(msg_type == TERMINATE)
        {
            terminate = true;
            break;
        }
    }
    
    // Close the server socket, and exit thread
    close(rpc_sock_fd);
    pthread_exit(NULL);
    return 0;
}

// Thread for handling user request
void* client_request_handler(void* arg)
{
    // Increase the thread count
    alt_thread_count(1);

    int client_fd = *(int*) arg;
    
    // Recieve the needed values
    int msg_type;
    int msg_length;
    int arg_type_length;
    
    recv(client_fd, &msg_length, sizeof(int), 0);
    recv(client_fd, &msg_type, sizeof(MessageType), 0);
    recv(client_fd, &arg_type_length, sizeof(int), 0);
    
    // Get the message body
    char *buffer = new char[msg_length - 3 * sizeof(int)];
    recv(client_fd, buffer, msg_length - 3 * sizeof(int), MSG_WAITALL);
    
    // Get the function name
    char *function_name = new char[DEFAULT_CHAR_ARR_SIZE];
    int arg_size_tot = msg_length - 3 * sizeof(int) - DEFAULT_CHAR_ARR_SIZE;
    int *client_args = (int*) malloc(arg_size_tot);

    // Extract the function namd and args
    memcpy(function_name, buffer, DEFAULT_CHAR_ARR_SIZE);
    memcpy(client_args, buffer + DEFAULT_CHAR_ARR_SIZE, arg_size_tot);
    
    //
    int *arg_types = (int*) malloc(arg_type_length * sizeof(int));
    memcpy(arg_types, client_args, arg_type_length * sizeof(int));
    
    // client_args
    void** args = (void**) malloc(arg_type_length * sizeof(void*));
    void* args_index = client_args + arg_type_length;

    for (int index = 0; index < arg_type_length; index++)
    {
        //see what type/len of arg we're dealing with
        int arg_type = get_arg_type(&arg_types[index]);
        int arg_type_size = size_of_type(arg_type);
        int arr_size = get_arg_length(&arg_types[index]);

        void* holder = (void*) malloc(arr_size * arg_type_size);
        *(args + index) = holder;
        for (int i = 0; i < arr_size; i++)
        {
            void* temp = (char*) holder + i * arg_type_size;
            memcpy(temp, args_index, arg_type_size);
            args_index = (void*) ((char*) args_index + arg_type_size);
        }
    }

    int result = EXECUTE_FAILURE;
    FuncSignature key(std::string(function_name), arg_types, arg_type_length);
    skeleton s;
    for (std::map<FuncSignature*, skeleton>::iterator it = server_functions.begin(); it != server_functions.end(); it ++) {
        FuncSignature f = *(it->first);
        if(f == key)
        {
            s = it->second;
            break;
        }
    }
    
    if(s != NULL)
        result = s(arg_types, args);

    if(result == 0) // success
    {
        int ret_msg_type = EXECUTE_SUCCESS;
        send(client_fd, &msg_length, sizeof(int), 0);
        send(client_fd, &ret_msg_type, sizeof(MessageType), 0);
        send(client_fd, &arg_type_length, sizeof(int), 0);
        send(client_fd, function_name, DEFAULT_CHAR_ARR_SIZE, 0);
        send(client_fd, arg_types, arg_type_length * 4, 0);

        for(int arg = 0; arg < arg_type_length; arg++)
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
        int ret_msg_length = 12;
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
	if (rpc_sock_fd < 0)
		return INIT_LOCAL_SOCKET_OPEN_FAILURE;
	if (bind(rpc_sock_fd, rpc_ai->ai_addr, rpc_ai->ai_addrlen))
		return INIT_LOCAL_SOCKET_BIND_FAILURE;

	// start listening
	listen(rpc_sock_fd, RPC_BACKLOG);

	// Connect to binder
    char* binder_address = getenv(BINDER_ADDRESS_S);
    char* binder_port = getenv(BINDER_PORT_S);
    
    // Validates that the address and port is set
    if(binder_address == NULL)
        return INIT_BINDER_ADDRESS_NOT_FOUND;
    else if(binder_port == NULL)
        return INIT_BINDER_PORT_NOT_FOUND;

    binder_socket_fd = socket_connect(binder_address, binder_port);
    if(binder_socket_fd < 0)
        return INIT_BINDER_SOCKET_OPEN_FAILURE;
	return SUCCESS;
}

// Called by client
int rpcCall(char* name, int* argTypes, void** args)
{
    // Connect to binder
    // Get Binder's address & port
    if(binder_socket_fd <= 0){
        char* binder_address = getenv(BINDER_ADDRESS_S);
        char* binder_port = getenv(BINDER_PORT_S);

        // Validates that the address and port is set
        if(binder_address == NULL)
            return CALL_BINDER_ADDRESS_NOT_FOUND;
        else if(binder_port == NULL)
            return CALL_BINDER_PORT_NOT_FOUND;

        binder_socket_fd = socket_connect(binder_address, binder_port);
    }

	// Make the msg
	int arg_types_length = 0;
	while(argTypes[arg_types_length])
		arg_types_length++;

    // length, LOC_REQUEST, name, argTypes
	int msg_length = sizeof(int) + sizeof(MessageType) + DEFAULT_CHAR_ARR_SIZE + arg_types_length * sizeof(int);

	// Sent the length & type
	send(binder_socket_fd, &msg_length, sizeof(int), 0);

    int msg_type = LOC_REQUEST;
	send(binder_socket_fd, &msg_type, sizeof(MessageType), 0);

	// Send the msg
	send(binder_socket_fd, name, DEFAULT_CHAR_ARR_SIZE, 0);

	send(binder_socket_fd, argTypes, arg_types_length * sizeof(int), 0);

	// Recieve the response type
    // Response format:
    // length, LOC_SUCCESS, server_identifier, port
    int recv_length;
    recv(binder_socket_fd, &recv_length, sizeof(int), 0);

	int res_type;
	recv(binder_socket_fd, &res_type, sizeof(MessageType), 0);

	char server_address[DEFAULT_CHAR_ARR_SIZE];
	unsigned short server_port;
	if(res_type == LOC_SUCCESS)
	{
		// successful response, get the address and port
		recv(binder_socket_fd, server_address, DEFAULT_CHAR_ARR_SIZE, 0);
		recv(binder_socket_fd, &server_port, UNSIGNED_SHORT_SIZE, 0);
	} else if (res_type == LOC_FAILURE)
	{
		// If failure, get the reason code and return it
		int reason;
		recv(binder_socket_fd, &reason, sizeof(int), 0);
		return reason;
	} else
	{
		return UNKNOW_MSG_TYPE_RESPONSE;
	}

	// sent to server and to EXECUTE
	// Connection
    std::stringstream ss;
    ss << server_port;
    std::string str_port = ss.str();
    char char_port[str_port.size()];
    strcpy(char_port, str_port.c_str());
	int server_fd = socket_connect(server_address, char_port);

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

    // length EXECUTE, sizeofarg, name, argTypes, args
	msg_length = sizeof(int) + sizeof(MessageType) + sizeof(int) + DEFAULT_CHAR_ARR_SIZE + arg_types_length * sizeof(int) + arg_size;

	// Send the message to server
	// Message type and length
	send(server_fd, &msg_length, sizeof(int), 0);
    msg_type = EXECUTE;
	send(server_fd, &msg_type, sizeof(MessageType), 0);
    
    send(server_fd, &arg_types_length, sizeof(int), 0);

	// function name and signiture
	send(server_fd, name, DEFAULT_CHAR_ARR_SIZE, 0);
	send(server_fd, argTypes, arg_types_length * sizeof(int), 0);

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
    int recv_msg_arg_length;
	recv(server_fd, &recv_msg_length, sizeof(int), 0);
	recv(server_fd, &recv_msg_type, sizeof(MessageType), 0);

	if(recv_msg_type == EXECUTE_SUCCESS)
	{
        recv(server_fd, &recv_msg_arg_length, sizeof(int), 0);
		char buffer[recv_msg_length - 3*sizeof(int)];
		recv(server_fd, buffer, recv_msg_length - 3 * sizeof(int), MSG_WAITALL);
        
        int arg_size_tot = msg_length - 3*sizeof(int) - DEFAULT_CHAR_ARR_SIZE;
        int *client_args = (int*) malloc(arg_size_tot);
        memcpy(client_args, buffer + DEFAULT_CHAR_ARR_SIZE, arg_size_tot);
        
        int *arg_types = new int[arg_types_length * 4];
        memcpy(arg_types, client_args, arg_types_length * 4);
        
        void* args_index = client_args + arg_types_length;
        
        for (int index = 0; index < arg_types_length; index++)
        {
            //see what type/len of arg we're dealing with
            int arg_type = get_arg_type(&arg_types[index]);
            int arg_type_size = size_of_type(arg_type);
            int arr_size = get_arg_length(&arg_types[index]);
            
            void* holder = (void*) malloc(arr_size * arg_type_size);
            args[index] = holder;
            for (int i = 0; i < arr_size; i++)
            {
                void* temp = (char*) holder + i * arg_type_size;
                memcpy(temp, args_index, arg_type_size);
                args_index = (void*) ((char*) args_index + arg_type_size);
            }
        }
        close(server_fd);
        
        
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
    int func_name_length = (int)std::string(name).size();
	int arg_types_length = 0;
	while(argTypes[arg_types_length])
		arg_types_length++;

    // int(size) + messagetype(type) + DEFAULT_CHAR_ARR_SIZE + unsigned short + DEFAULT_CHAR_ARR_SIZE + int_arr(arg_types)
    int msg_length =  sizeof(int) + sizeof(MessageType) + DEFAULT_CHAR_ARR_SIZE*2 + sizeof(unsigned short) + arg_types_length * sizeof(int);

    send(binder_socket_fd, &msg_length, sizeof(int), 0);

    int msg_type = REGISTER;
    send(binder_socket_fd, &msg_type, sizeof(MessageType), 0);

    // Sending serverid, port, function_name, arg_type
    send(binder_socket_fd, host_name, DEFAULT_CHAR_ARR_SIZE, 0);
    send(binder_socket_fd, &host_port, sizeof(unsigned short), 0);

    send(binder_socket_fd, name, DEFAULT_CHAR_ARR_SIZE, 0);
    send(binder_socket_fd, argTypes, arg_types_length * sizeof(int), 0);

    int response_length;
    recv(binder_socket_fd, &response_length, sizeof(int), 0);

    // The first recieve is the REGISTER_SUCCESS / REGISTER_FAILURE
    int response_status;
    recv(binder_socket_fd, &response_status, sizeof(MessageType), 0);

	// The second recieve is the additional status code
    int response_code;
    recv(binder_socket_fd, &response_code, sizeof(int), 0);

	if(response_status == REGISTER_SUCCESS)
	{
		// If the binder register is successful, add the skeleton to map
        FuncSignature* key = new FuncSignature(std::string(name), argTypes, arg_types_length);
		server_functions[key] = f;
		return response_code;
	} else if(response_status == REGISTER_FAILURE)
		return response_code;

	return REGISTER_BINDER_RET_UNRECON_TYPE;
}

int rpcExecute()
{
    fd_set master;      // Master file descriptor
    fd_set read_fds;    // Temp file descriptor
    int fdmax;          // Max number of file descirptors

    struct sockaddr_storage remoteaddr; // connector's address information
    socklen_t addrlen;                  // Address length

    // clear the master and temp sets
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    // add the listener to the master set
    FD_SET(rpc_sock_fd, &master);

    // keep track of the biggest file descriptor
    fdmax = rpc_sock_fd;

    pthread_t terminate_listener;

    // listen for binder termination
    terminate = false;
    pthread_create(&terminate_listener, NULL, wait_terminate, NULL);


    // Server main loop
    while(!terminate) {
        int newfd = accept(rpc_sock_fd, (struct sockaddr *)&remoteaddr, &addrlen);
        pthread_t client_thread;
        pthread_create(&client_thread, NULL, client_request_handler, (void*) &newfd);
        
    }
    
    while(thread_count < 0){}

    close(binder_socket_fd);
    return SUCCESS;
}

int rpcTerminate()
{
	// call binder to inform servcers to terminate
	// if no binder is created, then return with warning
	if(binder_socket_fd <= 0)
		return TERMINATE_BINDER_DID_NOT_INITIATE;

	// Sent terminate request to binder
    int len = 8;
    int msg_t = TERMINATE;
    send(binder_socket_fd, &len, sizeof(int), 0);
    send(binder_socket_fd, &msg_t, sizeof(MessageType), 0);

	// Close binder socket
	//close(binder_socket_fd);
	return SUCCESS;
}
