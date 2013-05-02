#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <sys/param.h>


char *
full_hostname()
/*
 * returns full hostname for current machine
 */
{
    char    hname[MAXHOSTNAMELEN];
    static  char fullname[MAXHOSTNAMELEN];
    struct addrinfo hints;
    struct addrinfo *result;
    int s;

    if ( gethostname(hname,MAXHOSTNAMELEN) == -1 )    /* get rel name    */
    {
        perror("gethostname");
        exit(1);
    }

    /* get info about host    */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;    /* IPv4 */
    hints.ai_socktype = SOCK_STREAM; /* sequenced, reliable, connection-based i.e. TCP */
    hints.ai_flags = AI_PASSIVE | AI_CANONNAME;    /* For wildcard IP address | return hostname*/
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    s = getaddrinfo(hname, NULL, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    strcpy( fullname, result->ai_canonname );
    return fullname;
}

int main(int ac, char **av) {
    char    fmt[100] ;
    int nlen = 7, vlen = 31;
    sprintf(fmt, "%%%ds%%%ds", nlen, vlen);
    printf("%s\n", fmt);
}