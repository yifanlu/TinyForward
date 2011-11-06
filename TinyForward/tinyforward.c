//
//  tinyforward.c
//  TinyForward
//
//  Copyright (C) 2011  Yifan Lu
//  
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//  
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//  
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "tinyforward.h"

Socket *last_socket;
fd_set master_set, read_set, write_set;

int compare_host_addr(struct sockaddr_in *name1, struct sockaddr_in *name2)
{
    if(name1->sin_addr.s_addr != name2->sin_addr.s_addr)
        return -1;
    if(name1->sin_port != name2->sin_port)
        return -1;
    if(name1->sin_family != name2->sin_family)
        return -1;
    return 0;
}

int get_host_addr (struct sockaddr_in *name, const char *hostName, uint16_t port)
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

int get_host_from_headers(char *headers, char **host_name, int *port)
{
    static size_t prefix_length = strlen("Host: ");
    const char *host_header;
    size_t length;
    char *port_str;
    
    host_header = strcasestr(headers, "Host: ");
    if(host_header == NULL)
        return -1;
    length = strcspn(host_header, "\r\n");
    *host_name = (char *)malloc(length - prefix_length + 1); // room for null terminator
    **host_name = '\0'; // so strncat will start at beginning
    strncat(*host_name, host_header + prefix_length, length - prefix_length);
    
    *port = 80;
    if(strchr(*host_name, ':') != NULL)
    {
        strtok (*host_name, ":"); // cut string at colon
        port_str = strtok (NULL, ":"); // get port number
        *port = atoi (port_str); // convert ASCII sting to number
    }
    
    return 0;
}

/*
int get_header_pos(char *buffer, regmatch_t *match)
{
    static regex_t regex; // no need to free it
    static int compiled = 0;
    regmatch_t pmatch[2];
    
    // host: "^Host: [A-Za-z0-9\\-\\.]+(:\\d+)?$"
    if(compiled == 0)
    {
        if(regcomp(&regex, "^Host: [A-Za-z0-9\\-\\.]+(:\\d+)?$", REG_EXTENDED|REG_NOSUB) != 0)
        {
            fprintf(stderr, "Cannot compile regex for comparsion!\n");
            return -1;
        }
        compiled = 1;
    }
    
    if(regexec(&regex, buffer, 1, pmatch, 0) != 0)
    {
        return -1;
    }
    
    *match = pmatch[1];
    
    return 1;
}
 */

long is_request_complete(char *buffer, long buffer_size)
{
    static size_t prefix_length = strlen("Content-length: ");
    const char *content_length_header;
    char *data;
    size_t length;
    size_t content_length;
    long data_length;
    
    content_length_header = strcasestr(buffer, "Content-Length: ");
    if(content_length_header != NULL)
    {
        length = strcspn(content_length_header + prefix_length, "\r\n");
        if(length == strlen(content_length_header + prefix_length)) // returned entire string, no match
        {
            return -1;
        }
        char length_str[length+1];
        memcpy(length_str, content_length_header + prefix_length, length);
        length_str[length] = '\0'; // null terminate
        content_length = atoi(length_str);
    }
    else
    {
        content_length = 0;
    }
    
    data = strstr(buffer, "\r\n\r\n");
    if(data == NULL)
        return -1;
    if(content_length == 0)
    {
        return 0;
    }
    else
    {
        data_length = buffer_size - ((data+4) - buffer); // how much data is read
        if(data_length >= content_length)
        {
            return content_length;
        }
        else
        {
            return -1;
        }
    }
    
    return 0;
}

Socket *add_socket(int socket_id, SocketType type)
{
    static int open = 0;
    fprintf(stderr, "Open sockets: %d\n", ++open);
    Socket *new_socket;
    new_socket = (Socket*)malloc(sizeof(Socket));
    new_socket->type = type;
    new_socket->id = socket_id;
    new_socket->buffer = (char*)malloc(BUFFER_SIZE);
    //new_socket->buffer[0] = '\0'; // strlen == 0
    new_socket->buffer_size = 0;
    new_socket->connected_socket = NULL;
    new_socket->next_socket = NULL;
    
    //Add it to the linked list
    new_socket->prevous_socket = last_socket;
    last_socket->next_socket = new_socket;
    last_socket = new_socket;
    
    return new_socket;
}

void remove_socket(Socket *socket)
{
    static int close = 0;
    fprintf(stderr, "Closed sockets: %d\n", ++close);
    // remove from linked list
    socket->prevous_socket->next_socket = socket->next_socket;
    if(socket->next_socket != NULL)
        socket->next_socket->prevous_socket = socket->prevous_socket;
    if(last_socket == socket)
        last_socket = socket->prevous_socket;
    free(socket->buffer);
    free(socket);
}


Socket *accept_connection(Socket *listener)
{
    int new_client;
    new_client = accept (listener->id, NULL, NULL);
    if (new_client < 0)
    {
        fprintf(stderr, "Error: %d", errno);
        perror ("accept");
        exit (EXIT_FAILURE);
    }
    
    fprintf (stderr, "New connection.");
    FD_SET (new_client, &master_set);
    
    return add_socket(new_client, ClientSocket);
}

Socket *close_connection(Socket *socket) // returns the next socket
{
    Socket *next;
    fprintf (stderr, "Closing socket.");
    close (socket->id); // close connection
    next = socket->next_socket; // get the next socket
    FD_CLR (socket->id, &master_set);
    FD_CLR (socket->id, &read_set);
    FD_CLR (socket->id, &write_set);
    if(socket->connected_socket != NULL)
    {
        if(next == socket->connected_socket)
            next = socket->connected_socket->next_socket;
        socket->connected_socket->connected_socket = NULL;
        close_connection(socket->connected_socket);
    }
    remove_socket(socket);
    return next;
}

Socket *connect_to_server(struct sockaddr_in *name)
{
    int server_id;
    
    server_id = socket (PF_INET, SOCK_STREAM, 0);
    if(server_id < 0)
    {
        fprintf(stderr, "Cannot create client socket.");
        return NULL;
    }
    if (connect (server_id, (struct sockaddr *) name, sizeof (struct sockaddr)) < 0)
    {
        fprintf(stderr, "Cannot connect to server.");
        return NULL;
    }
    
    return add_socket(server_id, ServerSocket);
}

int create_connection(Socket *client)
{
    char *host_name;
    int port;
    struct sockaddr_in name;
    Socket *server;
    
    if(get_host_from_headers(client->buffer, &host_name, &port) < 0)
    {
        fprintf(stderr, "Cannot get hostname from headers.\n");
        return -1;
    }
    
    if(get_host_addr(&name, host_name, port) < 0)
    {
        fprintf(stderr, "Cannot get host address from host name: %s and port: %d.", host_name, port);
        return -1;
    }
    
    free(host_name); // no longer needed
    
    if(client->connected_socket != NULL)
    {
        if(compare_host_addr(&name, &client->connected_socket->name) == 0) // We are reusing this socket
        {
            return 0;
        }
        else
        {
            client->connected_socket->connected_socket = NULL;
            close_connection(client->connected_socket);
            client->connected_socket = NULL;
        }
    }
    
    if((server = connect_to_server(&name)) == NULL)
    {
        send(client->id, error_return, strlen(error_return), 0); // send response to client
        return -1;
    }
    //Associate with client
    client->connected_socket = server;
    server->connected_socket = client;
    
    //Set name
    server->name = name;
    
    //Allow socket to be read
    FD_SET (server->id, &master_set);
    
    return 0;
}

int reconnect(Socket *sock)
{
    int new_id;
    int old_id;
    
    old_id = sock->id;
    close(sock->id);
    if((new_id = socket (PF_INET, SOCK_STREAM, 0)) < 0)
    {
        return -1;
    }
    
    sock->id = new_id;
    
    if(connect (new_id, (struct sockaddr *) &sock->name, sizeof (struct sockaddr)) < 0)
    {
        fprintf(stderr, "Cannot reconnect!\n");
        return -1;
    }
    
    if(FD_ISSET(old_id, &master_set))
    {
        FD_CLR(old_id, &master_set);
        FD_SET(new_id, &master_set);
    }
    if(FD_ISSET(old_id, &read_set))
    {
        FD_CLR(old_id, &read_set);
        FD_SET(new_id, &read_set);
    }
    if(FD_ISSET(old_id, &write_set))
    {
        FD_CLR(old_id, &write_set);
        FD_SET(new_id, &write_set);
    }
    
    return 0;
}

int read_socket(Socket *socket)
{
    char *buffer;
    ssize_t count;
    
    socket->buffer = realloc(socket->buffer, (socket->buffer_size + BUFFER_SIZE) * sizeof(char)); // resize buffer in case the last set was not read yet
    buffer = socket->buffer + socket->buffer_size;
    
    count = recv(socket->id, buffer, BUFFER_SIZE - 1, 0); // make room for null terminator
    buffer[count] = '\0'; // null terminate
    socket->buffer_size += count;
    
    if (count < 0) // error
    {
        return -1;
    }
    else if (count == 0) // read no bytes == socket closed from other side
    {
        return -1;
    }
    else
    {   
        if(socket->type == ClientSocket)
        {
            // check for Content-Length field and/or \r\n\r\n
            if(is_request_complete(socket->buffer, socket->buffer_size) >= 0)
            {
                if(create_connection(socket) != 0)
                {
                    return -1;
                }
            }
            else
            {
                return 0;
            }
        }
        
        assert(socket->connected_socket != NULL);
        FD_SET(socket->connected_socket->id, &write_set); // Can write to connected socket
    }
    
    return 0;
}

int write_socket(Socket *socket)
{
    assert(socket->connected_socket != NULL); // Shouldn't be null or we won't be here
    ssize_t count;
    
    count = send(socket->id, socket->connected_socket->buffer, socket->connected_socket->buffer_size, 0);
    //socket->connected_socket->buffer[0] = '\0'; // Reset buffer, strlen == 0
    socket->connected_socket->buffer_size = 0;
    
    if(count < 0) // error
    {
        return -1;
    }
    
    return 0;
}

int create_listener_socket (const char *host, uint16_t port)
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

int main (int argc, const char * argv[])
{
    Socket *listener;
    Socket *current;
    
    listener = (Socket*)malloc(sizeof(Socket));
    listener->type = ListenerSocket;
    listener->id = create_listener_socket (HOST, PORT);
    listener->buffer = NULL;
    listener->buffer_size = 0;
    listener->connected_socket = NULL;
    listener->prevous_socket = NULL;
    listener->next_socket = NULL;
    
    last_socket = listener;
    
    if (listen (listener->id, 1) < 0) // start listening
    {
        perror ("listen");
        close (listener->id);
        exit (EXIT_FAILURE);
    }
    
    FD_ZERO (&master_set);
    FD_ZERO (&read_set);
    FD_ZERO (&write_set);
    
    FD_SET (listener->id, &master_set);
    
    do
    {
        read_set = master_set;
        
        if (select (FD_SETSIZE, &read_set, &write_set, NULL, NULL) < 0)
        {
            perror ("select");
            close (listener->id);
            exit (EXIT_FAILURE);
        }
        
        for (current = listener; current != NULL; current = current->next_socket)
        {
            if (current->id > -1 && FD_ISSET (current->id, &read_set)) // the socket can be read
            {
                FD_CLR(current->id, &read_set);
                if(current->type == ListenerSocket)
                {
                    accept_connection(listener);
                }
                else
                {
                    if (read_socket (current) < 0)
                    {
                        if(current->type == ClientSocket)
                        {
                            Socket temp;
                            temp.id = -1;
                            temp.next_socket = close_connection(current); // so the loop can continue
                            current = &temp; // so we can still manage the next socket
                        }
                        else if(current->type == ServerSocket && current->buffer_size == 0)
                        {
                            Socket temp;
                            temp.id = -1;
                            assert(current->connected_socket != NULL);
                            current->connected_socket->connected_socket = NULL;
                            current->connected_socket = NULL;
                            temp.next_socket = close_connection(current); // so the loop can continue
                            current = &temp; // so we can still manage the next socket
                            /*
                            if(reconnect(current) < 0)
                            {
                            }
                            else
                            {
                                fprintf(stderr, "Reconnected to server.\n");
                            }
                             */
                        }
                    }
                }
            }
            if (current->id > -1 && FD_ISSET (current->id, &write_set)) // the socket can be written
            {
                FD_CLR(current->id, &write_set);
                if (write_socket (current) < 0)
                {
                    if(current->type == ClientSocket)
                    {
                        Socket temp;
                        temp.id = -1;
                        temp.next_socket = close_connection(current); // so the loop can continue
                        current = &temp; // so we can still manage the next socket
                    }
                    else if(current->type == ServerSocket)
                    {
                        if(reconnect(current) < 0)
                        {
                        }
                        else
                        {
                            fprintf(stderr, "Reconnected to server.\n");
                        }
                    }
                }
            }
        }
    }while(1);
    
    free(listener);
    return 0;
}
