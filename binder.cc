#include <errno.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <unistd.h>
#include <map>
#include <vector>
#include "common.h"
#include "rpc.h"
#include "binder.h"
#include "message.h"

using namespace std;


struct FuncSignature {
    string name;
    int* argTypes;
    int argSize;
    FuncSignature(string name, int* argTypes, int argSize) : name(name), argTypes(argTypes), argSize(argSize) {}
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

struct ServerLoc {
    string serverId;
    int port;
    ServerLoc(string serverId, int port) : serverId(serverId), port(port) {}
};

bool operator == (const ServerLoc &l, const ServerLoc &r) {
    return l.serverId == r.serverId && l.port == r.port;
}

map <FuncSignature*, vector<ServerLoc *> > funcDict;
vector<ServerLoc *> serverQueue;

void registerServer(ServerLoc* location) {
    for (int i = 0; i < serverQueue.size(); i++) {
        if (*(serverQueue[i]) == *location) {
            // server is registered and in queue
            return;
        }
    }
    serverQueue.push_back(location);
}

int registerFunc(string name, int* argTypes, int argSize, string serverId, int port) {
    bool found = false;
    ServerLoc *location = new ServerLoc(serverId, port);
    FuncSignature *func = new FuncSignature(name, argTypes, argSize);

    // Look up function in the dictionary
    for (map<FuncSignature *, vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it ++) {
        if (*func == *(it->first)) {
            found = true;
            for (int i = 0; i < it->second.size(); i++) {
                if (*(it->second[i]) == *location) {
                    // override function
                    return 1;
                }
            }
            it->second.push_back(location);
        }
    }

    if (!found) {
        funcDict[func].push_back(location);
    }

    // register server to queue
    registerServer(location);
    return 0;
}


void handleRegisterRequest(int clientSocketFd, int msgLength) {
}

void handleLocationRequest(int clientSocketFd, int msgLength) {
}

void handleTerminateRequest() {
}

void handleRequest(int clientSocketFd, fd_set *masterFds) {
    int msgLength;
    int bytes = read(clientSocketFd, &msgLength, 4);
    if (bytes == 0) {
        close(clientSocketFd);
        FD_CLR(clientSocketFd, masterFds);
        return;
    }
    MessageType msgType;
    bytes = read(clientSocketFd, &msgType, 4);
    if (bytes == 0) {
        close(clientSocketFd);
        FD_CLR(clientSocketFd, masterFds);
        return;
    }

    if (msgType == REGISTER) {
        handleRegisterRequest(clientSocketFd, msgLength);
    } else if (msgType == LOC_REQUEST) {
        handleLocationRequest(clientSocketFd, msgLength);
    } else if (msgType == TERMINATE) {
        handleTerminateRequest();
    }
    /*
    char buffer [buffer_size];
    bytes = read(i, &buffer[0], buffer_size);
    if (bytes == 0) {
        close(i);
        FD_CLR(i, &masterFds);
        continue;
    }
    capitalize(buffer, buffer_size);
    char res [buffer_size + 4];
    memcpy(&res[0], &buffer_size, 4);
    memcpy(&res[4], &buffer[0], buffer_size);
    write(i, res, buffer_size + 4);
    */
}

int main() {
    int socketFd, fdmax;
    struct sockaddr_in svrAddr, clntAddr;
    fd_set masterFds;
    fd_set readFds;


    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        cerr << "Error: cannot open a socket" << endl;
        return 1;
    }

    bzero((char*) &svrAddr, sizeof(svrAddr));

    svrAddr.sin_family = AF_INET;
    svrAddr.sin_addr.s_addr = htons(INADDR_ANY);
    svrAddr.sin_port = htons(0);

    if (bind(socketFd, (struct sockaddr *)&svrAddr, sizeof(svrAddr)) < 0) {
        cerr << "Error: cannot bind socket" << endl;
        return 1;
    }

    if (listen(socketFd, 5) < 0) {
        cerr << "Error: cannot listen to socket" << endl;
        return 1;
    }


    socklen_t size = sizeof(svrAddr);
    char server_name [128];
    getsockname(socketFd, (struct sockaddr *)&svrAddr, &size);
    gethostname(server_name, 128);
    
    cout << "BINDER_ADDRESS " << server_name << endl;
    cout << "BINDER_PORT " << ntohs(svrAddr.sin_port) << endl;

    FD_ZERO(&masterFds);
    FD_SET(socketFd, &masterFds);
    fdmax = socketFd;

    for(;;) {
        readFds = masterFds;

        if (select(fdmax+1, &readFds, NULL, NULL, NULL) < 0) {
            cerr << "Error: fail to select" << endl;
            return 1;
        }

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &readFds)) {
                if (i == socketFd) {
                    socklen_t size = sizeof(clntAddr);
                    int newFd = accept(socketFd, (struct sockaddr *)&clntAddr, &size);
                    if (newFd < 0) {
                        cerr << "Error: cannot establish new connection" << endl;
                        return 1;
                    }

                    FD_SET(newFd, &masterFds);

                    if (newFd > fdmax) fdmax = newFd;

                } else {
                    int clientSocketFd = i;
                    handleRequest(clientSocketFd, &masterFds);
                    /*
                    int buffer_size;
                    int bytes = read(i, &buffer_size, 4);
                    if (bytes == 0) {
                        close(i);
                        FD_CLR(i, &masterFds);
                        continue;
                    }
                    char buffer [buffer_size];
                    bytes = read(i, &buffer[0], buffer_size);
                    if (bytes == 0) {
                        close(i);
                        FD_CLR(i, &masterFds);
                        continue;
                    }
                    capitalize(buffer, buffer_size);
                    char res [buffer_size + 4];
                    memcpy(&res[0], &buffer_size, 4);
                    memcpy(&res[4], &buffer[0], buffer_size);
                    write(i, res, buffer_size + 4);
                    */
                }
            }
        }
    }


}

