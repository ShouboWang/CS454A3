#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

int sendMessage(int socket_fd, unsigned int msg_len, enum MessageType msg_type, char msg_data[]);
int receiveMessage(int socket_fd, int expect_len, char buf[]);

#endif
