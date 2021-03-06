#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <string.h>

// RPC const values
#define RPC_BACKLOG 5
#define INT_BYTE_PADDING 4
#define SOCKET_DATA_CHUNK_SIZE 16
#define DEFAULT_CHAR_ARR_SIZE 128

// Const string define
#define BINDER_ADDRESS_S "BINDER_ADDRESS"
#define BINDER_PORT_S "BINDER_PORT"

// Const char define
#define TERMINATING_CHAR '\0'


#define INT_SIZE sizeof(int)
#define UNSIGNED_SHORT_SIZE sizeof(unsigned short)

enum MessageType {
    TERMINATE = 0,
    REGISTER = 1,
    REGISTER_SUCCESS = 2,
    REGISTER_FAILURE = 3,
    LOC_REQUEST = 4,
    LOC_SUCCESS = 5,
    LOC_FAILURE = 6,
    EXECUTE = 7,
    EXECUTE_SUCCESS = 8,
    EXECUTE_FAILURE = 9
};

enum ReasonCode {
    REQUEST_SUCCESS = 0,
    FUNCTION_NOT_FOUND = 1,
    FUNCTION_OVERRIDDEN = 2,
    MESSAGE_CORRUPTED = 3
};

// Return values
// Success
#define SUCCESS 0

// Failure
#define INIT_BINDER_ADDRESS_NOT_FOUND -1
#define INIT_BINDER_PORT_NOT_FOUND -2
#define INIT_BINDER_SOCKET_OPEN_FAILURE -3
#define INIT_BINDER_SOCKET_BIND_FAILURE -4
#define INIT_LOCAL_SOCKET_OPEN_FAILURE -5
#define INIT_LOCAL_SOCKET_BIND_FAILURE -6

#define REGISTER_BINDER_DID_NOT_INITIATE -7
#define REGISTER_BINDER_RET_UNRECON_TYPE -8

#define EXECUTE_ADDRINFO_ERROR -9
#define EXECUTE_SOCKET_OPEN_FAILURE -10
#define EXECUTE_SOCKET_BIND_FAILURE -11
#define EXECUTE_SELECTION_FAILURE -12


#define CALL_BINDER_ADDRESS_NOT_FOUND -31
#define CALL_BINDER_PORT_NOT_FOUND -32
#define CALL_BINDER_FUNCTION_NOT_FOUND -33

#define RECEIVE_ERROR -404

#define SOCKET_OPEN_FAILURE -41
#define SOCKET_BIND_FAILURE -42

#define UNKNOW_MSG_TYPE_RESPONSE -100

// Warning
#define TERMINATE_BINDER_DID_NOT_INITIATE 1
#define REGISTER_BINDER_FUNCTION_OVERRIDEN 2


#endif
