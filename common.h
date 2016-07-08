#ifndef COMMON_H
#define COMMON_H

// RPC const values
#define RPC_BACKLOG 5

// Const string define
#define BINDER_ADDRESS_S "BINDER_ADDRESS"
#define BINDER_PORT_S "BINDER_PORT"

// Return values
// Success
#define SUCCESS 0

// Failure
#define ERROR_BINDER_ADDRESS_NOT_FOUND -1
#define ERROR_BINDER_PORT_NOT_FOUND -2

// RPC Binder communication
#define TERMINATION_MSG "Terminate_Binder_Sig"
