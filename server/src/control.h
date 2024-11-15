# ifndef CONTROL_H
# define CONTROL_H


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

# define DEFAULT_PORT       21
# define DEFAULT_ROOT       "/tmp"
# define BUFFER_SIZE        8192
# define PATH_MAX           1024


# define SEND_RESPONSE(socket, msg) send(socket, msg, strlen(msg), 0)

int control_init(in_port_t server_port, const char* root_dir, int is_pasv_local);

# endif