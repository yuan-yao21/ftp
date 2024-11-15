# ifndef IPGET_H
# define IPGET_H


# include <ifaddrs.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include <regex.h>

# define NI_MAXHOST         1025
# define NI_NUMERICHOST     1


int get_server_ip(char* ip_buffer, size_t ip_buffer_len, int is_pasv_local);

# endif