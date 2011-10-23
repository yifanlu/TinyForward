#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define HOST    "0.0.0.0"
#define PORT    5560
#define BUFFER_SIZE  10

int connectedClients, connectedServers;

typedef enum {
    ClientSocket,
    ServerSocket,
    ListenerSocket
} SocketType;

typedef struct ProxySocket Socket;

struct ProxySocket {
    Socket *prevousSocket;
    SocketType type;
    int id;
    struct ProxySocket *destinationSocket;
    char *bytesRead;
    ssize_t readSize;
    Socket *nextSocket;
};

Socket *lastSocket;
fd_set readSet, tempReadSet, writeSet;

Socket *addSocket(int socketId, SocketType type)
{
    static int open = 0;
    fprintf(stderr, "Open sockets: %d", ++open);
    Socket *newSocket;
    newSocket = (Socket*)malloc(sizeof(Socket));
    newSocket->destinationSocket = NULL;
    newSocket->bytesRead = NULL;
    newSocket->readSize = 0;
    newSocket->nextSocket = NULL;
    newSocket->type = type;
    newSocket->id = socketId;
    
    //Add it to the linked list
    newSocket->prevousSocket = lastSocket;
    lastSocket->nextSocket = newSocket;
    lastSocket = newSocket;
    
    return newSocket;
}

void removeSocket(Socket *socket)
{
    static int close = 0;
    fprintf(stderr, "Closed sockets: %d", ++close);
    // remove from linked list
    socket->prevousSocket->nextSocket = socket->nextSocket;
    if(socket->nextSocket != NULL)
        socket->nextSocket->prevousSocket = socket->prevousSocket;
    if(lastSocket == socket)
        lastSocket = socket->prevousSocket;
    free(socket->bytesRead);
    free(socket);
}

int getHostAddr (struct sockaddr_in *name, const char *hostName, uint16_t port)
{
    struct hostent *hostinfo;
    
    name->sin_family = AF_INET;
    name->sin_port = htons (port);
    hostinfo = gethostbyname (hostName);
    if (hostinfo == NULL)
    {
        return -1;
    }
    name->sin_addr = *(struct in_addr *) hostinfo->h_addr;
    return 0;
}

int
read_from_client (Socket *client)
{
    char *buffer = (char*)malloc(BUFFER_SIZE);
    ssize_t nbytes;
    
    nbytes = recv(client->id, buffer, BUFFER_SIZE, 0);
    fprintf(stderr, "Bytes: %ld", nbytes);
    if (nbytes < 0)
    {
        free(buffer);
        return -1;
        /* Read error. */
        //perror ("read");
        //exit (EXIT_FAILURE);
    }
    else if (nbytes == 0)
    {
        free(buffer);
        return 0;
    }
    else
    {
        /* Data read. */
        client->bytesRead = realloc(client->bytesRead, (client->bytesRead ? strlen(client->bytesRead) : 0) + strlen(buffer) + sizeof(char));
        strncat(client->bytesRead, buffer, nbytes);
        if(nbytes < BUFFER_SIZE){
            fprintf (stderr, "Server: whole message: '%s'\n", client->bytesRead);
        }
        //fprintf (stderr, "Server: got message: '%s'\n", buffer);
        free(buffer);
        return 0;
    }
}

int getHostFromHeaders(char *headers, char **hostName, int *port)
{
    static size_t prefixLength = strlen("Host: ");
    const char *hostHeader;
    size_t length;
    char *portStr;
    
    hostHeader = strstr(headers, "Host: ");
    if(hostHeader == NULL)
        return -1;
    length = strcspn(hostHeader, "\r\n");
    *hostName = (char *)malloc(length - prefixLength + 1); // room for null terminator
    **hostName = '\0'; // so strncat will start at beginning
    strncat(*hostName, hostHeader + prefixLength, length - prefixLength);
    
    *port = 80;
    if(strchr(*hostName, ':') != NULL)
    {
        strtok (*hostName, ":"); // cut string at colon
        portStr = strtok (NULL, ":"); // get port number
        *port = atoi (portStr); // convert ASCII sting to number
    }
    
    return 0;
}

int readyForServer(Socket *clientSocket)
{
    fprintf (stderr, "Header dump: '%s'\n", clientSocket->bytesRead);
    if(clientSocket->destinationSocket == NULL)
    {
        struct sockaddr_in name;
        char *hostName;
        int port;
        int server;
        Socket *serverSocket;
        
        if(getHostFromHeaders(clientSocket->bytesRead, &hostName, &port) < 0)
        {
            fprintf(stderr, "Cannot get hostname from headers.");
            return -1;
        }
        
        server = socket (PF_INET, SOCK_STREAM, 0);
        if(server < 0)
        {
            fprintf(stderr, "Cannot create client socket.");
            return -1;
        }
        if(getHostAddr(&name, hostName, port) < 0)
        {
            fprintf(stderr, "Cannot get host address from host name: %s and port: %d.", hostName, port);
            return -1;
        }
        if (connect (server, (struct sockaddr *) &name, sizeof (name)) < 0)
        {
            fprintf(stderr, "Cannot connect to server: %s on port: %d.", hostName, port);
            return -1;
        }
        
        serverSocket = addSocket(server, ServerSocket);
        
        //Associate with client
        clientSocket->destinationSocket = serverSocket;
        serverSocket->destinationSocket = clientSocket;
        
        //Allow socket to be read
        FD_SET (serverSocket->id, &readSet);
        
        free(hostName);
    }
    
    // Socket is ready to be written to
    clientSocket->readSize = strlen(clientSocket->bytesRead); // This allows checking to see if socket is ready
    FD_SET(clientSocket->destinationSocket->id, &writeSet);
    return 0;
}

int readClient (Socket *client)
{
    // parse input
    // if done with parseing
    // set bytesRead
    // if no connection, connect to server
    // add server to read set
    // else continue
    char *buffer = (char*)malloc(BUFFER_SIZE);
    ssize_t count;
    
    count = recv(client->id, buffer, BUFFER_SIZE, 0);
    if (count < 0) // error
    {
        // set server socket ready to write
        // if bytesRead != null
        free(buffer);
        return -1;
    }
    else if (count == 0) // read no bytes
    {
        free(buffer);
        //if(client->readSize == 0 && client->bytesRead != NULL){
        //    return readyForServer(client);
        //}
        return -1;
    }
    else
    {
        // resize the bytesRead array, but if it's null just alloc. Don't forget to make room for the trailing null byte
        client->bytesRead = realloc(client->bytesRead, (client->bytesRead ? strlen(client->bytesRead) : 0) + strlen(buffer) + sizeof(char));
        strncat(client->bytesRead, buffer, count); // use count to make sure garbage data isn't copied
        free(buffer);
        if(count < BUFFER_SIZE){
            // set server socket ready to write
            return readyForServer(client);
        }
        //fprintf (stderr, "Server: got message: '%s'\n", buffer);
        return 0;
    }
    
    return 0;
}

int readServer (Socket *server)
{
    // read data to buffer
    // associate buffer with client
    // add client to write set
    // if end, remove self from read set
    if(server->destinationSocket == NULL)
        return -1;
    
    if(server->bytesRead == NULL)
        server->bytesRead = malloc(BUFFER_SIZE);
    
    server->readSize = recv(server->id, server->bytesRead, BUFFER_SIZE, 0);
    if (server->readSize <= 0)
    {
        // set server socket ready to write
        // if bytesRead != null
        return -1;
    }
    else
    {
        FD_SET(server->destinationSocket->id, &writeSet);
    }
    
    return 0;
}

int writeSocketFromDestination (Socket *socket)
{
    // write buffer to socket
    // remove self from write set
    size_t bytesWritten;
    if(socket->destinationSocket == NULL)
    {
        return -1;
    }
    else if(socket->destinationSocket->bytesRead == NULL)
    {
        return 0;
    }
    bytesWritten = send(socket->id, socket->destinationSocket->bytesRead, socket->destinationSocket->readSize, 0);
    socket->readSize = 0;
    FD_CLR(socket->id, &writeSet);
    return 0;
}

int createListenerSocket (const char *host, uint16_t port)
{
    int sock;
    struct sockaddr_in name;
    
    if ((sock = socket (PF_INET, SOCK_STREAM, 0)) < 0) // make the socket
    {
        perror ("create");
        exit (EXIT_FAILURE);
    }
    
    // define the socket parameters
    name.sin_family = AF_INET;
    name.sin_port = htons (port);
    if(inet_pton (AF_INET, host, &name.sin_addr.s_addr) < 1) // convert host string to IP type
    {
        perror ("parse");
        close (sock);
        exit (EXIT_FAILURE);
    }
    if (bind (sock, (struct sockaddr *) &name, sizeof (name)) < 0) // bind the socket
    {
        perror ("bind");
        close (sock);
        exit (EXIT_FAILURE);
    }
    
    return sock;
}

int main (void)
{
    Socket *listenerSocket;
    Socket *currentSocket;
    //int socket;
    int newClient;
    //struct sockaddr_in client;
    //socklen_t size;
    
    // create listener socket
    listenerSocket = (Socket*)malloc(sizeof(Socket));
    listenerSocket->destinationSocket = NULL;
    listenerSocket->bytesRead = NULL;
    listenerSocket->nextSocket = NULL;
    listenerSocket->prevousSocket = NULL;
    listenerSocket->type = ListenerSocket;
    listenerSocket->id = createListenerSocket (HOST, PORT);
    
    if (listen (listenerSocket->id, 1) < 0) // start listening
    {
        perror ("listen");
        close (listenerSocket->id);
        exit (EXIT_FAILURE);
    }
    
    lastSocket = listenerSocket; // the last socket is the first one
    
    // initialize the sets
    FD_ZERO (&readSet);
    //FD_ZERO (&tempReadSet);
    FD_ZERO (&writeSet);
    FD_SET (listenerSocket->id, &readSet);

    
    do
    {
        tempReadSet = readSet; // copy the set because select() destroys our input
        if (select (FD_SETSIZE, &tempReadSet, &writeSet, NULL, NULL) < 0)
        {
            perror ("select");
            close (listenerSocket->id);
            exit (EXIT_FAILURE);
        }
        
        /* Service all the sockets with input pending. */
        assert(listenerSocket != NULL);
        for (currentSocket = listenerSocket; currentSocket != NULL; currentSocket = currentSocket->nextSocket)
        {
            if (FD_ISSET (currentSocket->id, &tempReadSet)) // the socket can be read
            {
                FD_CLR(currentSocket->id, &tempReadSet);
                if (currentSocket->type == ListenerSocket)
                {
                    // get connection request
                    //size = sizeof (client);
                    newClient = accept (listenerSocket->id, NULL, NULL);
                                  //(struct sockaddr *) &client,
                                  ///&size);
                    if (newClient < 0)
                    {
                        fprintf(stderr, "Error: %d", errno);
                        perror ("accept");
                        exit (EXIT_FAILURE);
                    }
                    
                    addSocket(newClient, ClientSocket);
                    
                    fprintf (stderr, "New connection.");
                    FD_SET (newClient, &readSet);
                    break;
                    // TODO: bug! If this break isn't here, program will infinite loop when too many clients try to connect at once
                }
                else if (currentSocket->type == ClientSocket)
                {
                    if (readClient (currentSocket) < 0)
                    {
                        fprintf (stderr, "Closing client socket.");
                        close (currentSocket->id); // close connection
                        FD_CLR (currentSocket->id, &readSet);
                        FD_CLR (currentSocket->id, &writeSet);
                        if(currentSocket->destinationSocket != NULL && currentSocket->destinationSocket->type == ServerSocket)
                        {
                            FD_CLR (currentSocket->destinationSocket->id, &writeSet);
                            currentSocket->destinationSocket->destinationSocket = NULL;
                        }
                        removeSocket(currentSocket);
                        currentSocket = NULL;
                        break; // it's easier to lose some speed than try to free the pointer elsewhere
                    }
                }
                else if (currentSocket->type == ServerSocket)
                {
                    if (readServer (currentSocket) < 0)
                    {
                        fprintf (stderr, "Closing server socket.");
                        close (currentSocket->id); // close connection
                        FD_CLR (currentSocket->id, &readSet);
                        FD_CLR (currentSocket->id, &writeSet);
                        if(currentSocket->destinationSocket != NULL && currentSocket->destinationSocket->type == ClientSocket)
                        {
                            FD_CLR (currentSocket->destinationSocket->id, &writeSet);
                            currentSocket->destinationSocket->destinationSocket = NULL;
                        }
                        removeSocket(currentSocket);
                        currentSocket = NULL;
                        break; // it's easier to lose some speed than try to free the pointer elsewhere
                    }
                }
            }
            if (FD_ISSET (currentSocket->id, &writeSet)) // the socket can be written
            {
                if (writeSocketFromDestination (currentSocket) < 0)
                {
                    
                }
            }
            assert(currentSocket->nextSocket != currentSocket);
        }
    }while(1);
    
    free(listenerSocket);
}