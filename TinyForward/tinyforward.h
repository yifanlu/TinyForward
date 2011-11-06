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

#ifndef TinyForward_tinyforward_h
#define TinyForward_tinyforward_h

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define HOST    "0.0.0.0"
#define PORT    5565
#define BUFFER_SIZE  1024

typedef enum {
    ClientSocket,
    ServerSocket,
    ListenerSocket
} SocketType;

typedef struct ProxySocket Socket;

struct ProxySocket {
    Socket *prevous_socket;
    SocketType type;
    int id;
    struct sockaddr_in name;
    char *buffer;
    long buffer_size;
    Socket *connected_socket;
    Socket *next_socket;
};

const char *error_return = "HTTP/1.1 500 Proxy Error\r\n\r\nProxy cannot process request.";

/* Helper functions */
int compare_host_addr(struct sockaddr_in *, struct sockaddr_in *);
int get_host_addr (struct sockaddr_in *, const char *, uint16_t);
int get_host_from_headers(char *, char **, int *);
//int get_header_pos(char *, regmatch_t *);
long is_request_complete(char *);

/* Linked list functions */
Socket *add_socket(int, SocketType);
void remove_socket(Socket *);

/* Connecting clients */
Socket *accept_connection(Socket *);
Socket *close_connection(Socket *);

/* Connecting servers */
Socket *connect_to_server(struct sockaddr_in *);
int create_connection(Socket *);
int reconnect(Socket *);

/* Sockets IO */
int read_socket(Socket *);
int write_socket(Socket *);

/* Listening */
int create_listener_socket (const char *, uint16_t);

#endif
