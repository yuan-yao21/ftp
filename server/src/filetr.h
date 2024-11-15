# ifndef FILETR_H
# define FILETR_H


# include <netinet/in.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <arpa/inet.h>
# include <sys/socket.h>
# include <errno.h>
# include <time.h>
# include <netdb.h>
# include <ifaddrs.h>
# include <sys/stat.h>
# include <pthread.h>

# define SEND_RESPONSE(socket, msg) send(socket, msg, strlen(msg), 0)

# define CMD_RETREIVE 1
# define CMD_STORE 2

struct transfer_params {
    int data_socket;
    int client_socket;
    FILE *file;
    char transfer_type;
    int transfer_error;
    int file_command;
};

void *transfer_file(void *args);

# endif