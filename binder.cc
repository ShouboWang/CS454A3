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
    serverQueue.push_back(location);
}

int registerFunc(std::string name, int* argTypes, int argSize, std::string serverId, unsigned short port, int socketFd) {
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
                    return 1;
                }
            }
            std::cout << "size before: " << it->second.size() << std::endl;
            it->second.push_back(location);
            std::cout << "size: " << it->second.size() << std::endl;
        }
    }

    if (!found) {
        funcDict[func].push_back(location);
    }

    // register server to queue
    registerServer(serverId, port, socketFd);
    return 0;
}


void handleRegisterRequest(int clientSocketFd, int msgLength) {
    char buffer[msgLength];
    ReasonCode reason;
    char responseMsg [INT_SIZE];
    int status = receiveMessage(clientSocketFd, msgLength, buffer);
    if (status == RECEIVE_ERROR) {
        // corrupt message
        reason = MESSAGE_CORRUPTED;
        memcpy(responseMsg, &reason, INT_SIZE);
        sendMessage(clientSocketFd, 3 * INT_SIZE, REGISTER_SUCCESS, responseMsg);
        return;
    }

    char server[CHAR_ARR_SIZE];
    unsigned short port;
    char funcName[CHAR_ARR_SIZE];
    int argSize = ((msgLength - 2 * CHAR_ARR_SIZE - UNSIGNED_SHORT_SIZE)/ INT_SIZE);
    int argTypes [argSize];

    memcpy(server, buffer, CHAR_ARR_SIZE);
    memcpy(&port, buffer + CHAR_ARR_SIZE, UNSIGNED_SHORT_SIZE);
    memcpy(funcName, buffer + CHAR_ARR_SIZE + UNSIGNED_SHORT_SIZE, CHAR_ARR_SIZE);
    memcpy(argTypes, buffer + 2 * CHAR_ARR_SIZE + UNSIGNED_SHORT_SIZE, argSize * INT_SIZE);
    
    std::cout << "server: " << server << std::endl;
    std::cout << "port: " << port << std::endl;
    std::cout << "funcName: " << funcName << std::endl;
    std::cout << "argSize: " << argSize << std::endl;
    for(int i = 0; i < argSize; i++)
    {
        std::cout << "argTypes: " << argTypes[i] << std::endl;
    }

    std::string name(funcName);
    std::string serverId(server);
    
    std::cout << "done" << std::endl;

    status = registerFunc(name, argTypes, argSize, serverId, port, clientSocketFd);
    std::cout << "done1" << std::endl;
    if (status == 1) {
        reason = FUNCTION_OVERRIDDEN;
    } else {
        reason = REQUEST_SUCCESS;
    }
    memcpy(responseMsg, &reason, INT_SIZE);
    std::cout << "reason: " << reason << std::endl;
    sendMessage(clientSocketFd, 3 * INT_SIZE, REGISTER_SUCCESS, responseMsg);
    std::cout << "done4" << std::endl;
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
                rotate(serverQueue.begin(), serverQueue.end()-1, serverQueue.end());
            }


        }
    }
    return selectedServer;

}

void handleLocationRequest(int clientSocketFd, int msgLength) {
    char buffer[msgLength];
    int status = receiveMessage(clientSocketFd, msgLength, buffer);
    if (status == RECEIVE_ERROR) {
        // corrupt message
        char responseMsg [INT_SIZE];
        ReasonCode reason = MESSAGE_CORRUPTED;
        memcpy(responseMsg, &reason, INT_SIZE);
        sendMessage(clientSocketFd, 3 * INT_SIZE, LOC_FAILURE, responseMsg);
        return;
    }

    char funcName[CHAR_ARR_SIZE];
    int argSize = ((msgLength - 2 * CHAR_ARR_SIZE) / INT_SIZE) - 1;
    int argTypes [argSize];

    memcpy(funcName, buffer, CHAR_ARR_SIZE);
    memcpy(argTypes, buffer + CHAR_ARR_SIZE, argSize);

    std::string name(funcName);

    ServerLoc * availServer;
    availServer = lookupAvailableServer(name, argTypes, argSize);

    if (!availServer) {
        // function not available
        char responseMsg [INT_SIZE];
        ReasonCode reason = FUNCTION_NOT_FOUND;
        memcpy(responseMsg, &reason, INT_SIZE);
        sendMessage(clientSocketFd, 3 * INT_SIZE, LOC_FAILURE, responseMsg);
    } else {
        char responseMsg [CHAR_ARR_SIZE + INT_SIZE];
        memcpy(responseMsg, availServer->serverId.c_str(), CHAR_ARR_SIZE);
        memcpy(responseMsg + CHAR_ARR_SIZE, &(availServer->port), INT_SIZE);
        sendMessage(clientSocketFd, 2 * INT_SIZE + CHAR_ARR_SIZE, LOC_SUCCESS, responseMsg);
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
            std::cout << "deleting server from queue" << std::endl;
            delete serverQueue[i];
            std::cout << "deleted server from queue" << std::endl;
            serverQueue.erase(serverQueue.begin() + i);
        }
    }
    
    std::cout << "queue size: " << serverQueue.size() << std::endl;
    for (std::map<FuncSignature *, std::vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it++) {
        std::cout << "function: " << it->first->name << std::endl;
        std::vector<ServerLoc *> servers = it->second;
        for (std::vector<ServerLoc *>::iterator it2 = servers.begin(); it2 != servers.end(); it2++) {
            std::cout << "server host: " << (*it2)->serverId << "port: " << (*it2)->port << std::endl;
            if (closingSocketFd == (*it2)->socketFd) {
                // TODO: check this fker out
                std::cout << "delete this fucker" << std::endl;
            }
        }
        std::cout << "vector size: " << servers.size() << std::endl;
    }
    
    for (std::map<FuncSignature *, std::vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it++) {
        for (std::vector<ServerLoc *>::iterator it2 = it->second.begin(); it2 != it->second.end();) {
            if (closingSocketFd == (*it2)->socketFd) {
                // TODO: check this fker out
                std::cout << "deleting from map" << std::endl;
                delete *it2;
                std::cout << "deleted from map" << std::endl;
                it2 = it->second.erase(it2);
                break;
            } else {
                it2++;
            }
        }
        std::cout << "vector size: " << it->second.size() << std::endl;
    }

}

void cleanup() {
    std::cout << "cleaning" << std::endl;
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
        handleRegisterRequest(clientSocketFd, msgLength - 2*INT_BYTE_PADDING);
    } else if (msgType == LOC_REQUEST) {
        handleLocationRequest(clientSocketFd, msgLength - 2*INT_BYTE_PADDING);
    } else if (msgType == TERMINATE) {
        handleTerminateRequest();
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

    //bzero((char*) &svrAddr, sizeof(svrAddr));
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
                    std::cout << "found new connection" << std::endl;
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
