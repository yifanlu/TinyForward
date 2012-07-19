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

// stolen from tinyproxy
int opensock (const char *host, int port)
{
    int sockfd, n;
    struct addrinfo hints, *res, *ressave;
    char portstr[6];
    
    assert (host != NULL);
    assert (port > 0);
    
    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    snprintf (portstr, sizeof (portstr), "%d", port);
    
    n = getaddrinfo (host, portstr, &hints, &res);
    if (n != 0) {
        fprintf(stderr, "opensock: Could not retrieve info for %s", host);
        return -1;
    }
    
    ressave = res;
    do {
        sockfd =
        socket (res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd < 0)
            continue;       /* ignore this one */
        
        if (connect (sockfd, res->ai_addr, res->ai_addrlen) == 0)
            break;  /* success */
        
        close (sockfd);
    } while ((res = res->ai_next) != NULL);
    
    freeaddrinfo (ressave);
    if (res == NULL) {
        fprintf(stderr, "opensock: Could not establish a connection to %s", host);
        return -1;
    }
    
    return sockfd;
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

void remove_connection(connection_t *conn){
    // remove from linked list
    if(conn->previous_connection != NULL){
        conn->previous_connection->next_connection = conn->next_connection;
    }
    if(conn->next_connection != NULL){
        conn->next_connection->previous_connection = conn->previous_connection;
    }
    if(last_connection == conn){
        last_connection = conn->previous_connection;
    }
    
    free(conn->request_buffer);
    free(conn->response_buffer);
    free(conn);
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

int is_http_request(unsigned char *data, unsigned long len){
    // there isn't a fool-proof way (that I know) to see if a TCP packet is HTTP
    // so I'll just have to make some guesses and hope for no false positives
    
    // Min size of a valid HTTP request should be theoretically:
    // "X HTTP/1.1\r\n\r\n"
    if(len < 14){
        return 0;
    }
    // Check if the first byte is alphabetical
    if(!(data[0] >= 'A' && data[0] <= 'Z')){
        return 0;
    }
    // check for trailing CRLF
    if(!(data[len-4] == '\r' && data[len-3] == '\n' && data[len-2] == '\r' && data[len-1] == '\n')){
        return 0;
    }
    // Look for HTTP tag before newline
    if((data = (unsigned char*)strstr((const char*)data, "HTTP/")) != NULL){
        // look for newline
        if(!(data[8] == '\r' && data[9] == '\n')){
            return 0;
        }
    }else{
        return 0;
    }
    // if after all that, the packet still seems to be HTTP, we'll say so
    return 1;
}

int get_host_port(unsigned char *request, unsigned long len, char** host, int *port){
    char *copy = strdup((char*)request);
    char *start;
    char *temp;
    
    if((start = strchr(copy, ' ')) == NULL){
        goto error;
    }
    if((temp = strchr(start, ' ')) == NULL){
        goto error;
    }
    start++; // strip leading space
    temp[0] = '\0'; // strip trailing space
    
    // strip protocol if exists
    temp = strstr(start, "://");
    if(temp != NULL){
        start = temp+3;
    }
    
    // strip username/password
    temp = strchr(start, '@');
    start = temp + 1;
    
    // strip slash
    temp = strchr(start, '/');
    if(temp != NULL){
        temp[0] = '\0';
    }
    
    temp = strchr(start, ']');
    if(temp != NULL){ // IPv6
        temp[0] = '\0'; // strip ]
        if(temp[1] == ':'){ // ipv6 AND alt. port? wtf
            *port = atoi(temp+2);
            temp[1] = '\0'; // strip :
        }else{
            *port = 80; // normal port
        }
        temp = strchr(start, '[');
        if(temp == NULL){ // malformed request
            goto error;
        }else{
            start = temp+1; // strip [
        }
    }else{ // IPv4
        temp = strchr(start, ':');
        if(temp != NULL){ // port number
            *port = atoi(temp+1);
            temp[0] = '\0'; // strip :
        }else{
            *port = 80;
        }
        
    }
    if(strlen(temp) > 0){ // we still have a host string
        *host = strdup(start);
    }else{ // need to be transparent proxy
        goto error;
    }
    
    free(copy);
    return 0;
error:
    free(copy);
    return -1;
}

int handle_request(connection_t *conn){
    char *host;
    char *temp;
    int port;
    
    if(!(conn->request_size > 0)){
        fprintf(stderr, "Request is invalid\n");
        return -1;
    }
    if(is_http_request(conn->request_buffer, conn->request_size)){
        // if HTTP upstream... else
        // if HTTP connect... else
        if(get_host_port(conn->request_buffer, conn->request_size, &host, &port) < 0){ // can't get info
            // TODO: Error handling
        }
        // else get host/port from transparent proxy
        // copy request from queue to buffer
        temp = strstr((char*)conn->request_buffer, "\r\n\r\n");
        if(temp == NULL){ // invalid request
            // check if we are piplining, if so, current_request_size < request_size
            conn->current_request_size = ((int)temp - (int)conn->request_buffer + 4);
            conn->current_request_buffer = realloc(conn->current_request_buffer, conn->current_request_size);
            memcpy(conn->current_request_buffer, conn->request_buffer, conn->current_request_size);
            // remove request from queue
            memmove(conn->request_buffer, conn->request_buffer + conn->current_request_size, conn->request_size - conn->current_request_size);
            conn->request_size -= conn->current_request_size;
        }
    }else{ // create CONNECT request
        // get host/port from transparent
        conn->current_request_buffer = conn->request_buffer;
        conn->current_request_size = conn->request_size;
    }
    // modify request if necessary
    
    if(conn->server_socket > 0){
        if(strcmp(host, conn->request.host) == 0){ // We are reusing this socket
            goto done;
        }else{ // seems like another socket will be created, destroy the old one
            close_connection(conn->server_socket);
            conn->server_socket = 0;
            // save server details
            free(conn->request.host);
            conn->request.host = host;
            conn->request.port = port;
        }
    }
    conn->server_socket = opensock(host, port);
    if(conn->server_socket < 0){
        fprintf(stderr, "Cannot connect to server.\n");
        goto error;
    }
done:
    FD_SET(conn->server_socket, &write_set); // ready to write to server
    return 0;
error:
    free(host);
    return -1;
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
                count = write_socket(current->server_socket, &current->current_request_buffer, &current->current_request_size);
                // TODO: handle next request
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
