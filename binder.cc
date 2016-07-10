#include <errno.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <vector>
#include "common.h"
#include "rpc.h"
#include "binder.h"
#include "message.h"


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

struct ServerLoc {
    std::string serverId;
    unsigned short port;
    int socketFd;
    ServerLoc(std::string serverId, unsigned short port, int socketFd) : serverId(serverId), port(port), socketFd(socketFd) {}
};

bool operator == (const ServerLoc &l, const ServerLoc &r) {
    return l.serverId == r.serverId && l.port == r.port && l.socketFd == r.socketFd;
}

bool terminating;
std::map <FuncSignature*, std::vector<ServerLoc *> > funcDict;
std::vector<ServerLoc *> serverQueue;

void registerServer(std::string serverId, unsigned short port, int socketFd) {
    ServerLoc *location = new ServerLoc(serverId, port, socketFd);
    for (int i = 0; i < serverQueue.size(); i++) {
        if (*(serverQueue[i]) == *location) {
            // server is registered and in queue
            return;
        }
    }
    // push the server to round robin queue
    serverQueue.push_back(location);
}

ReasonCode registerFunc(std::string name, int* argTypes, int argSize, std::string serverId, unsigned short port, int socketFd) {
    bool found = false;
    ServerLoc *location = new ServerLoc(serverId, port, socketFd);
    FuncSignature *func = new FuncSignature(name, argTypes, argSize);

    // Look up function in the dictionary
    for (std::map<FuncSignature *, std::vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it ++) {
        if (*func == *(it->first)) {
            found = true;
            for (int i = 0; i < it->second.size(); i++) {
                if (*(it->second[i]) == *location) {
                    // override function
                    return FUNCTION_OVERRIDDEN;
                }
            }
            it->second.push_back(location);
        }
    }

    if (!found) {
        // adds function to the dictioonary
        funcDict[func].push_back(location);
    }

    // register server to queue if not already
    registerServer(serverId, port, socketFd);
    return REQUEST_SUCCESS;
}


void handleRegisterRequest(int clientSocketFd, int msgLength) {
    char buffer[msgLength];
    ReasonCode reason;
    char responseMsg [INT_SIZE];
    // reads message to buffer
    int status = receiveMessage(clientSocketFd, msgLength, buffer);
    if (status == RECEIVE_ERROR) {
        // corrupt message
        reason = MESSAGE_CORRUPTED;
        memcpy(responseMsg, &reason, INT_SIZE);
        sendMessage(clientSocketFd, 3 * INT_SIZE, REGISTER_FAILURE, responseMsg);
        return;
    }

    char server[DEFAULT_CHAR_ARR_SIZE];
    unsigned short port;
    char funcName[DEFAULT_CHAR_ARR_SIZE];
    int argSize = ((msgLength - 2 * DEFAULT_CHAR_ARR_SIZE - UNSIGNED_SHORT_SIZE)/ INT_SIZE);
    int *argTypes = new int[argSize];

    // reads server and function info
    memcpy(server, buffer, DEFAULT_CHAR_ARR_SIZE);
    memcpy(&port, buffer + DEFAULT_CHAR_ARR_SIZE, UNSIGNED_SHORT_SIZE);
    memcpy(funcName, buffer + DEFAULT_CHAR_ARR_SIZE + UNSIGNED_SHORT_SIZE, DEFAULT_CHAR_ARR_SIZE);
    memcpy(argTypes, buffer + 2 * DEFAULT_CHAR_ARR_SIZE + UNSIGNED_SHORT_SIZE, argSize * INT_SIZE);
    
    std::string name(funcName);
    std::string serverId(server);
    
    reason = registerFunc(name, argTypes, argSize, serverId, port, clientSocketFd);
    memcpy(responseMsg, &reason, INT_SIZE);
    sendMessage(clientSocketFd, 3 * INT_SIZE, REGISTER_SUCCESS, responseMsg);
}

ServerLoc *lookupAvailableServer(std::string name, int *argTypes, int argSize) {
    ServerLoc *selectedServer = NULL;
    FuncSignature *func = new FuncSignature(name, argTypes, argSize);

    for (std::map<FuncSignature *, std::vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it ++) {
        if (*func == *(it->first)) {
            std::vector<ServerLoc *> availServers = it->second;
            // Look up server queue in round robin fashion
            for (int i = 0; i < serverQueue.size(); i++) {
                ServerLoc * server = serverQueue.front();
                for (int j = 0; j < availServers.size(); j++) {
                    if (*server == *(availServers[j])) {
                        // found the first available server
                        selectedServer = server;
                        break;
                    }
                }
                // move the server to the back of the queue if cannot service the function
                rotate(serverQueue.begin(), serverQueue.end()-1, serverQueue.end());
            }


        }
    }
    return selectedServer;

}

void handleLocationRequest(int clientSocketFd, int msgLength) {
    char buffer[msgLength];
    // read message to buffer
    int status = receiveMessage(clientSocketFd, msgLength, buffer);
    if (status == RECEIVE_ERROR) {
        // corrupt message
        char responseMsg [INT_SIZE];
        ReasonCode reason = MESSAGE_CORRUPTED;
        memcpy(responseMsg, &reason, INT_SIZE);
        sendMessage(clientSocketFd, 3 * INT_SIZE, LOC_FAILURE, responseMsg);
        return;
    }

    char funcName[DEFAULT_CHAR_ARR_SIZE];
    int argSize = ((msgLength - DEFAULT_CHAR_ARR_SIZE) / INT_SIZE);
    int *argTypes = new int[argSize];

    // reads function name and args
    memcpy(funcName, buffer, DEFAULT_CHAR_ARR_SIZE);
    memcpy(argTypes, buffer + DEFAULT_CHAR_ARR_SIZE, argSize * INT_SIZE);

    std::string name(funcName);

    ServerLoc * availServer = lookupAvailableServer(name, argTypes, argSize);

    if (!availServer) {
        // function not found, return failure
        char responseMsg [INT_SIZE];
        ReasonCode reason = FUNCTION_NOT_FOUND;
        memcpy(responseMsg, &reason, INT_SIZE);
        sendMessage(clientSocketFd, 3 * INT_SIZE, LOC_FAILURE, responseMsg);
    } else {
        // return server info if found
        char responseMsg [DEFAULT_CHAR_ARR_SIZE + INT_SIZE];
        memcpy(responseMsg, availServer->serverId.c_str(), DEFAULT_CHAR_ARR_SIZE);
        memcpy(responseMsg + DEFAULT_CHAR_ARR_SIZE, &(availServer->port), UNSIGNED_SHORT_SIZE);
        sendMessage(clientSocketFd, 2 * INT_SIZE + DEFAULT_CHAR_ARR_SIZE + UNSIGNED_SHORT_SIZE, LOC_SUCCESS, responseMsg);
    }
}

void handleTerminateRequest() {
    // terminate and clean up
    for (int i = 0; i < serverQueue.size(); i++) {
        sendMessage(serverQueue[i]->socketFd, 2 * INT_SIZE, TERMINATE, NULL);
    }
    terminating = true;
}

void removeServer(int closingSocketFd) {
    for (int i = 0; i < serverQueue.size(); i++) {
        if (serverQueue[i]->socketFd == closingSocketFd) {
            delete serverQueue[i];
            serverQueue.erase(serverQueue.begin() + i);
        }
    }
    
    for (std::map<FuncSignature *, std::vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it++) {
        for (std::vector<ServerLoc *>::iterator it2 = it->second.begin(); it2 != it->second.end();) {
            if (closingSocketFd == (*it2)->socketFd) {
                delete *it2;
                it2 = it->second.erase(it2);
                break;
            } else {
                it2++;
            }
        }
    }

}

void cleanup() {
    // clean up database and queue
    for (int i = 0; i < serverQueue.size(); i++) {
        delete serverQueue[i];
    }
    serverQueue.clear();

    for (std::map<FuncSignature *, std::vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it ++) {
        delete it->first;
        std::vector<ServerLoc *> v = it->second;
        for (std::vector<ServerLoc *>::iterator v_it = v.begin() ; v_it != v.end(); v_it++) {
            delete *v_it;
        }
        v.clear();
    }
    funcDict.clear();
}


void handleRequest(int clientSocketFd, fd_set *masterFds) {
    // read message length
    int msgLength;
    int bytes = read(clientSocketFd, &msgLength, 4);
    if (bytes <= 0) {
        close(clientSocketFd);
        FD_CLR(clientSocketFd, masterFds);
        removeServer(clientSocketFd);
        if (serverQueue.size() == 0 && terminating) {
            cleanup();
            exit(0);
        }
        return;
    }

    // read message type
    MessageType msgType;
    bytes = read(clientSocketFd, &msgType, 4);
    if (bytes <= 0) {
        close(clientSocketFd);
        FD_CLR(clientSocketFd, masterFds);
        removeServer(clientSocketFd);
        if (serverQueue.size() == 0 && terminating) {
            cleanup();
            exit(0);
        }
        return;
    }

    if (msgType == REGISTER) {
        handleRegisterRequest(clientSocketFd, msgLength - 2 * INT_SIZE);
    } else if (msgType == LOC_REQUEST) {
        handleLocationRequest(clientSocketFd, msgLength - 2 * INT_SIZE);
    } else if (msgType == TERMINATE) {
        handleTerminateRequest();
    } else {
    }
}

int main() {
    terminating = false;
    int socketFd, fdmax;
    struct sockaddr_in svrAddr, clntAddr;
    fd_set masterFds;
    fd_set readFds;


    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        std::cerr << "Error: cannot open a socket" << std::endl;
        return 1;
    }

    memset((char*) &svrAddr, 0, sizeof(svrAddr));

    svrAddr.sin_family = AF_INET;
    svrAddr.sin_addr.s_addr = htons(INADDR_ANY);
    svrAddr.sin_port = htons(0);

    if (bind(socketFd, (struct sockaddr *)&svrAddr, sizeof(svrAddr)) < 0) {
        std::cerr << "Error: cannot bind socket" << std::endl;
        return 1;
    }

    if (listen(socketFd, 5) < 0) {
        std::cerr << "Error: cannot listen to socket" << std::endl;
        return 1;
    }


    socklen_t size = sizeof(svrAddr);
    char server_name [128];
    getsockname(socketFd, (struct sockaddr *)&svrAddr, &size);
    gethostname(server_name, 128);

    std::cout << "BINDER_ADDRESS " << server_name << std::endl;
    std::cout << "BINDER_PORT " << ntohs(svrAddr.sin_port) << std::endl;

    FD_ZERO(&masterFds);
    FD_SET(socketFd, &masterFds);
    fdmax = socketFd;

    for(;;) {
        readFds = masterFds;

        if (select(fdmax+1, &readFds, NULL, NULL, NULL) < 0) {
            std::cerr << "Error: fail to select" << std::endl;
            return 1;
        }

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &readFds)) {
                if (i == socketFd) {
                    socklen_t size = sizeof(clntAddr);
                    int newFd = accept(socketFd, (struct sockaddr *)&clntAddr, &size);
                    if (newFd < 0) {
                        std::cerr << "Error: cannot establish new connection" << std::endl;
                        return 1;
                    }

                    FD_SET(newFd, &masterFds);

                    if (newFd > fdmax) fdmax = newFd;

                } else {
                    int clientSocketFd = i;
                    handleRequest(clientSocketFd, &masterFds);
                }
            }
        }
    }


}
