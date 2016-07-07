#ifndef COMMON_H
#define COMMON_H

// RPC const values
#define RPC_BACKLOG 5

// Const string define
#define BINDER_ADDRESS_S "BINDER_ADDRESS"
#define BINDER_PORT_S "BINDER_PORT"

enum MessageType {
    TERMINATE = 0,
    REGISTER = 1,
    REGISTER_SUCCESS = 2,
    REGISTER_FAILURE = 3,
    LOC_REQUEST = 4,
    LOC_REQUEST_SUCCESS = 5,
    LOC_REQUEST_FAILURE = 6,
    EXECUTE = 7,
    EXECUTE_SUCCESS = 8,
    EXECUTE_FAILURE = 9
};

enum ReasonCode {
    SUCCESS = 0,
    FUNCTION_NOT_FOUND = 1,
    FUNCTION_OVERRIDDEN = 2,
};

// Return values
// Success
#define SUCCESS 0

// Failure
#define ERROR_BINDER_ADDRESS_NOT_FOUND -1
#define ERROR_BINDER_PORT_NOT_FOUND -2

#endif
