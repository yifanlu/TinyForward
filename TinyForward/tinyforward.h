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
#define PORT    "5555"
#define MAX_BUFFER_SIZE  10240
#define DEBUG_READ
#define DEBUG_WRITE

typedef struct connection connection_t;

typedef struct request {
    char *host;
    int port;
    int is_http;
} request_t;

struct connection {
    connection_t *previous_connection;
    int client_socket;
    int server_socket;
    request_t request;
    unsigned char *request_buffer;
    unsigned long request_size;
    unsigned char *current_request_buffer;
    unsigned long current_request_size;
    unsigned char *response_buffer;
    unsigned long response_size;
    connection_t *next_connection;
};

/* Helper functions */
int opensock (const char *host, int port);
int is_http_request(unsigned char *data, unsigned long len);
int get_host_port(unsigned char *request, unsigned long len, char** host, int *port);

/* Linked list functions */
connection_t *add_connection(int socket);
void remove_connection(connection_t *conn);

/* Connecting clients */
connection_t *accept_client(int listener);
void close_connection(int socket);

/* Connecting servers */
int handle_request(connection_t *conn);

/* Sockets IO */
ssize_t read_socket(int socket, unsigned char **p_buffer, unsigned long *p_size);
ssize_t write_socket(int socket, unsigned char **p_buffer, unsigned long *p_size);

/* Listening */
int create_listener_socket(const char *host, uint16_t port);

#endif
