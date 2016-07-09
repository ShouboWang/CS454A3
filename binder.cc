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
#include <algorithm>
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
    int socketFd;
    ServerLoc(string serverId, int port, int socketFd) : serverId(serverId), port(port), socketFd(socketFd) {}
};

bool operator == (const ServerLoc &l, const ServerLoc &r) {
    return l.serverId == r.serverId && l.port == r.port && l.socketFd == r.socketFd;
}

bool terminating;
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

int registerFunc(string name, int* argTypes, int argSize, string serverId, int port, int socketFd) {
    bool found = false;
    ServerLoc *location = new ServerLoc(serverId, port, socketFd);
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
    int port;
    char funcName[CHAR_ARR_SIZE];
    int argSize = ((msgLength - 2 * CHAR_ARR_SIZE) / INT_SIZE) - 1;
    int argTypes [argSize];

    memcpy(server, buffer, CHAR_ARR_SIZE);
    memcpy(&port, buffer + CHAR_ARR_SIZE, INT_SIZE);
    memcpy(funcName, buffer + CHAR_ARR_SIZE + INT_SIZE, CHAR_ARR_SIZE);
    memcpy(argTypes, buffer + 2 * CHAR_ARR_SIZE + INT_SIZE, argSize);

    string name(funcName);
    string serverId(server);

    status = registerFunc(name, argTypes, argSize, serverId, port, clientSocketFd);
    if (status == 1) {
        reason = FUNCTION_OVERRIDDEN;
    } else {
        reason = REQUEST_SUCCESS;
    }
    memcpy(responseMsg, &reason, INT_SIZE);
    sendMessage(clientSocketFd, 3 * INT_SIZE, REGISTER_SUCCESS, responseMsg);
}

ServerLoc *lookupAvailableServer(string name, int *argTypes, int argSize) {
    ServerLoc *selectedServer = NULL;
    FuncSignature *func = new FuncSignature(name, argTypes, argSize);

    for (map<FuncSignature *, vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it ++) {
        if (*func == *(it->first)) {
            vector<ServerLoc *> availServers = it->second;

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

    string name(funcName);

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
            delete serverQueue[i];
            serverQueue.erase(serverQueue.begin() + i);
        }
    }
    for (map<FuncSignature *, vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it++) {
        vector<ServerLoc *> servers = it->second;
        for (vector<ServerLoc *>::iterator it2 = servers.begin(); it2 != servers.end(); it2++) {
            if (closingSocketFd == (*it2)->socketFd) {
                delete *it2;
                servers.erase(it2);
                break;
            }
        }
    }
}

void cleanup() {
    for (int i = 0; i < serverQueue.size(); i++) {
        delete serverQueue[i];
    }
    serverQueue.clear();

    for (map<FuncSignature *, vector<ServerLoc *> >::iterator it = funcDict.begin(); it != funcDict.end(); it ++) {
        delete it->first;
        vector<ServerLoc *> v = it->second;
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
                }
            }
        }
    }


}
