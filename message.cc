#include <cstring>
#include <sys/socket.h>
#include "message.h"
#include "rpc.h"

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
			if(sent_len < 0)
                return sent_len;

			// If 0 is returned then the sent is terminated
			if(sent_len == 0)
				break;

			have_sent += sent_len;
		}

		return SUCCESS;
}


int recieveMessage(int socket_fd, int expect_len, char buf[])
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
