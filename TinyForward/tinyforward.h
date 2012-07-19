//
//  tinyforward.h
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

#ifndef TinyForward_tinyforward_h
#define TinyForward_tinyforward_h

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define HOST    "0.0.0.0"
#define PORT    "5560"
#define MAX_BUFFER_SIZE  10240

typedef struct connection connection_t;

struct connection {
    connection_t *previous_connection;
    int client_socket;
    int server_socket;
    struct sockaddr_in server_addr;
    unsigned char *request_buffer;
    unsigned long request_size;
    unsigned char *response_buffer;
    unsigned long response_size;
    connection_t *next_connection;
};

const char *error_return = "HTTP/1.1 500 Proxy Error\r\n\r\nProxy cannot process request. Error connecting to server.";

/* Helper functions */
int compare_host_addr(struct sockaddr_in *name1, struct sockaddr_in *name2);
int get_host_addr(struct sockaddr_in *name, const char *hostName, uint16_t port);
int get_host_from_headers(char *headers, char **host_name, unsigned short *port);
long is_request_complete(char *buffer, long buffer_size);

/* Linked list functions */
connection_t *add_connection(int socket);
void remove_connection(connection_t *connection);

/* Connecting clients */
connection_t *accept_client(int listener);
void close_connection(int socket);

/* Connecting servers */
int connect_server(connection_t *connection);

/* Sockets IO */
ssize_t read_socket(int socket, unsigned char **p_buffer, unsigned long *p_size);
ssize_t write_socket(int socket, unsigned char **p_buffer, unsigned long *p_size);

/* Listening */
int create_listener_socket(const char *host, uint16_t port);

#endif
