//
//  tinyforward.c
//  TinyForward
//
//  Copyright (C) 2012  Yifan Lu
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

connection_t *last_connection;
fd_set master_set, read_set, write_set;

int compare_host_addr(struct sockaddr_in *name1, struct sockaddr_in *name2){
    if(name1->sin_addr.s_addr != name2->sin_addr.s_addr)
        return -1;
    if(name1->sin_port != name2->sin_port)
        return -1;
    if(name1->sin_family != name2->sin_family)
        return -1;
    return 0;
}

int get_host_addr(struct sockaddr_in *name, const char *hostName, uint16_t port){
    struct hostent *hostinfo;
    
    name->sin_family = AF_INET;
    name->sin_port = htons (port);
    hostinfo = gethostbyname (hostName);
    if (hostinfo == NULL){
        return -1;
    }
    name->sin_addr = *(struct in_addr *) hostinfo->h_addr;
    return 0;
}

int get_host_from_headers(char *headers, char **host_name, unsigned short *port){
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
    if(strchr(*host_name, ':') != NULL){
        strtok (*host_name, ":"); // cut string at colon
        port_str = strtok (NULL, ":"); // get port number
        *port = atoi (port_str); // convert ASCII sting to number
    }
    
    return 0;
}

long is_request_complete(char *buffer, long buffer_size){
    static size_t prefix_length = strlen("Content-Length: ");
    const char *content_length_header;
    char *data;
    size_t length;
    size_t content_length;
    long data_length;
    
    content_length_header = strcasestr(buffer, "Content-Length: ");
    if(content_length_header != NULL){
        length = strcspn(content_length_header + prefix_length, "\r\n");
        if(length == strlen(content_length_header + prefix_length)){ // returned entire string, no match
            return -1;
        }
        char length_str[length+1];
        memcpy(length_str, content_length_header + prefix_length, length);
        length_str[length] = '\0'; // null terminate
        content_length = atoi(length_str);
    }else{
        content_length = 0;
    }
    
    data = strstr(buffer, "\r\n\r\n");
    if(data == NULL)
        return -1;
    if(content_length == 0){
        return 0;
    }
    else{
        data_length = buffer_size - ((data+4) - buffer); // how much data is read
        if(data_length >= content_length){
            return content_length;
        }else{
            return -1;
        }
    }
    
    return 0;
}

connection_t *add_connection(int socket){
    connection_t *new_connection = malloc(sizeof(connection_t));
    memset(new_connection, 0, sizeof(connection_t)); // zero out everything
    new_connection->client_socket = socket;
    new_connection->previous_connection = last_connection;
    if(last_connection != NULL){
        last_connection->next_connection = new_connection;
    }
    last_connection = new_connection;
    
    return new_connection;
}

void remove_connection(connection_t *connection){
    // remove from linked list
    if(connection->previous_connection != NULL){
        connection->previous_connection->next_connection = connection->next_connection;
    }
    if(connection->next_connection != NULL){
        connection->next_connection->previous_connection = connection->previous_connection;
    }
    if(last_connection == connection){
        last_connection = connection->previous_connection;
    }
    
    free(connection->request_buffer);
    free(connection->response_buffer);
    free(connection);
}


connection_t *accept_client(int listener){
    int new_client;
    new_client = accept(listener, NULL, NULL);
    if(new_client < 0){
        fprintf(stderr, "Error accepting new connection: socket error %d\n", errno);
        return NULL;
    }
    fcntl(new_client, F_SETFL, O_NONBLOCK); // non-blocking read
    
    fprintf(stdout, "New connection.\n");
    FD_SET(new_client, &master_set);
    
    return add_connection(new_client);
}

void close_connection(int socket){
    close(socket); // close connection
    FD_CLR(socket, &master_set);
    FD_CLR(socket, &read_set);
    FD_CLR(socket, &write_set);
}

int connect_server(connection_t *connection){
    char *host_name;
    unsigned short port;
    struct sockaddr_in name;
    int server_sock;
    
    if(get_host_from_headers((char*)connection->request_buffer, &host_name, &port) < 0){
        fprintf(stderr, "%s\n", "Cannot get hostname from headers.");
        return -1;
    }
    
    fprintf(stdout, "Connecting to %s on port %d.\n", host_name, port);
    
    if(get_host_addr(&name, host_name, port) < 0){
        fprintf(stderr, "Cannot get host address from host name: %s and port: %d.\n", host_name, port);
        return -1;
    }
    
    free(host_name); // no longer needed
    
    if(connection->server_socket > 0){
        if(compare_host_addr(&name, &connection->server_addr) == 0){ // We are reusing this socket
            return 0;
        }else{ // seems like another socket will be created, destroy the old one
            close_connection(connection->server_socket);
            connection->server_socket = 0;
        }
    }
    
    server_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(server_sock < 0){
        fprintf(stderr, "Cannot create client socket.\n");
        return -1;
    }
    if (connect(server_sock, (struct sockaddr *)&name, sizeof(struct sockaddr)) < 0){
        fprintf(stderr, "Cannot connect to server.\n");
        return -1;
    }
    connection->server_socket = server_sock;
    
    //Set name
    connection->server_addr = name;
    
    // set read
    FD_SET(connection->server_socket, &master_set);
    
    return 0;
}

int is_http_request(unsigned char *data, unsigned long len){
    
}

int handle_request(connection_t *connection){
    if(!(connection->request_size > 0)){
        return -1;
    }
    // check if it's http
    // check for transparent
    // get host and port
    // check for upstream
    // open server connection
    // modify request if necessary
}


ssize_t read_socket(int socket, unsigned char **p_buffer, unsigned long *p_size){
    unsigned char *buffer = *p_buffer;
    unsigned long size = *p_size;
    ssize_t count;
    
    buffer = realloc(buffer, size + MAX_BUFFER_SIZE); // resize buffer in case the last set was not written yet
    
    count = recv(socket, buffer + size, MAX_BUFFER_SIZE, 0);
    if(count > 0){
        size += count;
    }
    
    *p_buffer = buffer;
    *p_size = size;
    
    return count;
}

ssize_t write_socket(int socket, unsigned char **p_buffer, unsigned long *p_size){
    unsigned char *buffer = *p_buffer;
    unsigned long size = *p_size;
    ssize_t count;
    
    count = send(socket, buffer, size, 0);
    size = 0;
    
    *p_buffer = buffer;
    *p_size = size;
    
    return count;
}

int create_listener_socket(const char *host, uint16_t port){
    int sock;
    struct sockaddr_in name;
    
    if((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0){ // make the socket
        perror("create");
        exit(EXIT_FAILURE);
    }
    
    // define the socket parameters
    name.sin_family = AF_INET;
    name.sin_port = htons(port);
    if(inet_pton(AF_INET, host, &name.sin_addr.s_addr) < 1){ // convert host string to IP type
        perror ("parse");
        close (sock);
        exit (EXIT_FAILURE);
    }
    if(bind(sock, (struct sockaddr *) &name, sizeof (name)) < 0){ // bind the socket
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    return sock;
}

int main (int argc, const char * argv[]){
    connection_t *current;
    int listener_socket;
    
    listener_socket = create_listener_socket(HOST, atoi(PORT));
    last_connection = NULL;
    
    if(listen(listener_socket, 1) < 0){ // start listening
        perror("listen");
        close(listener_socket);
        exit(EXIT_FAILURE);
    }
    
    fprintf(stdout, "Started listening on %s port %s\n", HOST, PORT);
    
    FD_ZERO(&master_set);
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    
    FD_SET(listener_socket, &master_set);
    
    do{
        read_set = master_set;
        
        if (select(FD_SETSIZE, &read_set, &write_set, NULL, NULL) < 0){
            perror("select");
            close(listener_socket);
            exit(EXIT_FAILURE);
        }
        
        // check listener socket
        if (FD_ISSET(listener_socket, &read_set)){
            if(accept_client(listener_socket) == NULL){
                // TODO: Error handling
            }
        }
        
        ssize_t count;
        // first loop for client
        for (current = last_connection; current != NULL; current = current->previous_connection){
            if (FD_ISSET(current->client_socket, &read_set)){ // request to be read
                if(current->server_socket > 0 && current->request_size > 0){ // TODO: find if this slows loading down
                    // woah! too fast. don't read the next request until we parse this one
                }else{
                    FD_CLR(current->client_socket, &read_set);
                    for(;;){
                        count = read_socket(current->client_socket, &current->request_buffer, &current->request_size);
                        if(count == 0 || // closed connection
                          (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))){ // persistant connection
                            break;
                        }
                    }
                    /*
                    if(count > 0){ // read something
                        if(is_request_complete((char*)current->request_buffer, current->request_size) >= 0){
                            // TODO: Here, we modify the request headers.
                            
                            if(connect_server(current) != 0){ // error creating connection
                                send(current->client_socket, error_return, strlen(error_return), 0); // send error to client
                                close_connection(current->client_socket);
                                fprintf(stderr, "%s\n", "Error creating connection to server.");
                                remove_connection(current);
                                break;
                            }else{
                                FD_SET(current->server_socket, &write_set); // ready to write to server
                            }
                        }
                     */
                    if(handle_request(current) < 0){ // interpret request
                        // close connection
                        close_connection(current->client_socket);
                        close_connection(current->server_socket);
                        remove_connection(current);
                        break;
                    }
                }
            }
            if (FD_ISSET(current->client_socket, &write_set)){ // response to be written
                FD_CLR(current->client_socket, &write_set);
                count = write_socket(current->client_socket, &current->response_buffer, &current->response_size);
                if(count < current->response_size){ // error sending to client
                    // close connection
                    close_connection(current->client_socket);
                    close_connection(current->server_socket);
                    fprintf(stderr, "%s\n", "Error sending response to client.");
                    remove_connection(current);
                    break;
                }
            }
        //}
        // second loop for the server
        //for (current = last_connection; current != NULL; current = current->previous_connection){
            if (FD_ISSET(current->server_socket, &read_set)){ // response to be read
                FD_CLR(current->server_socket, &read_set);
                count = read_socket(current->server_socket, &current->response_buffer, &current->response_size);
                if(count <= 0){ // done reading
                    close_connection(current->server_socket);
                    current->server_socket = 0;
                }
                FD_SET(current->client_socket, &write_set); // ready to write to client
            }
            if (FD_ISSET(current->server_socket, &write_set)){ // request to be written
                FD_CLR(current->server_socket, &write_set);
                count = write_socket(current->server_socket, &current->request_buffer, &current->request_size);
                if(count < current->response_size){ // error sending to server
                    close_connection(current->server_socket);
                    current->server_socket = 0;
                    send(current->client_socket, error_return, strlen(error_return), 0); // send error to client
                    fprintf(stderr, "%s\n", "Error sending request to server.");
                }
            }
        }
    }while(1);
    
    close(listener_socket);
    return 0;
}
