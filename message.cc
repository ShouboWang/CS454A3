#include <cstring>
#include <sys/socket.h>
#include "message.h"
#include "rpc.h"

/*
*	Sends the message to socket fd with package size of 64 bytes each
* returns SUCCESS is execution did not encounter error
* else return the error status code
*/
int sendMessage(int socket_fd, unsigned int data_len, MessageType msg_type, char msg_data[])
{
    // Format of the data is
    // Send the length
    send(socket_fd, &data_len, sizeof(int), 0);
    send(socket_fd, &msg_type, sizeof(MessageType), 0);
    send(socket_fd, msg_data, data_len - sizeof(int) - sizeof(MessageType), 0);
    return SUCCESS;
}

int receiveMessage(int socket_fd, int expect_len, char buf[])
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
