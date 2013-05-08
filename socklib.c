#include    <stdio.h>
#include    <strings.h>
#include    <sys/types.h>
#include    <sys/socket.h>
#include    <sys/select.h>
#include    <sys/time.h>
#include    <netinet/in.h>
#include    <netdb.h>
#include    <unistd.h>
#include    <string.h>
#include    "util.h"

/*
 *    socklib.c
 *
 *    This file contains functions used lots when writing internet
 *    client/server programs.  The two main functions here are:
 *
 *    make_server_socket( portnum )    returns a server socket
 *                    or -1 if error
 *
 *    connect_to_server(char *hostname, int portnum)
 *                    returns a connected socket
 *                    or -1 if error
 *
 *    history: 2010-04-16 replaced bcopy/bzero with memcpy/memset
 *    history: 2005-05-09 added SO_REUSEADDR to make_server_socket
 */ 

int
make_server_socket( int portnum, int backlog )
{
    struct  sockaddr_in   saddr;   /* build our address here */
    int    sock_id;           /* line id, file desc     */
    int    on = 1;               /* for sockopt         */

    /*
     *      step 1: build our network address
     *               domain is internet, hostname is any address
     *               of local host, port is some number
     */

    memset(&saddr, 0, sizeof(saddr));          /* 0. zero all members   */

    saddr.sin_family = AF_INET;           /* 1. set addr family    */
    saddr.sin_addr.s_addr = htonl(INADDR_ANY); /* 2. and IP addr        */
    saddr.sin_port = htons(portnum);       /* 3. and the port         */

    /*
     *      step 2: get socket, set option, then then bind address
     */

    sock_id = socket( PF_INET, SOCK_STREAM, 0 );    /* get a socket */
    if ( sock_id == -1 ) return -1;
    if ( setsockopt(sock_id,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)) == -1 )
        return -1;
    if ( bind(sock_id,(struct sockaddr*)&saddr, sizeof(saddr)) ==  -1 )
           return -1;

    /*
     *      step 3: tell kernel we want to listen for calls
     */
    if ( listen(sock_id, backlog) != 0 ) return -1;
    return sock_id;
}


int connect_to_server( char *hostname, char *portnum )
{
    struct addrinfo *result, *rp,  hints;
    int serverfd = -1, rc;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;       /* IPv4 */
    hints.ai_socktype = SOCK_STREAM; /* sequenced, reliable, connection-based i.e. TCP */
    hints.ai_flags = AI_CANONNAME;   /* return hostname */
    hints.ai_protocol = 0;           /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(hostname, portnum, &hints, &result);
    if (rc != 0) {
        printf("getaddrinfo: %s\n", gai_strerror(rc));
        fatal("connect_to_server", "", 1);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        serverfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (serverfd == -1)
            continue;

       if (connect(serverfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;                  /* Success */
        }

        close(serverfd);
    }
    freeaddrinfo(result);
    return serverfd;
}

