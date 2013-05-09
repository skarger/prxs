#include    <stdio.h>
#include    <stdlib.h>
#include    <strings.h>
#include    <string.h>
#include    <netdb.h>
#include    <errno.h>
#include    <fcntl.h>
#include    <unistd.h>
#include    <sys/types.h>
#include    <sys/socket.h>
#include    <sys/stat.h>
#include    <sys/param.h>
#include    <sys/select.h>
#include    <sys/time.h>
#include    <signal.h>
#include    <time.h>
#include    <pthread.h>
#include    "socklib.h"
#include    "flexstr.h"
#include	"util.h"
#include    "prxs.h"


/*
 * prxs.c - proxy server
 * 
 * usage: prxs [ -c configfilenmame ]
 *
 * Adapted from simple web server code provided by Harvard Extension School
 * Specifically socklib.c, flexstr.c, and code to start server with config file
 * Course: CSCI-E215, Instructor: Bruce Molay
 */

char myhost[MAXHOSTNAMELEN];
int myport;


int
main(int ac, char *av[])
{
    int sock, fd;

    /* set up */
    sock = startup(ac, av, myhost, &myport);

    printf("%s%s started. host=%s port=%d\n",
        SERVER_NAME, VERSION, myhost, myport);

    /* main loop here */
    while(1)
    {
        fd = accept( sock, NULL, NULL );
        if ( fd == -1 ) {
            perror("accept");
        } else {
            handle_call(fd);
        }
    }
    return 0;
    /* never end */
}

/*
 * handle_call(fd) - serve the request arriving on fd
 */
void handle_call(int fd)
{
    int rc;
    static pthread_t threads[NUM_THREADS];    
    static int thread_args[NUM_THREADS];
    static int thread_count = 0;
    thread_args[thread_count] = fd;
    rc = pthread_create(&threads[thread_count], NULL,
                        serve_request, (void *) &thread_args[thread_count]);
    thread_count++;
    if (rc != 0) {
        close(fd);
        fatal("handle_call: pthread_create failed", "", -1);
    }

    /* if we've run out of threads wait for all threads to complete and resume */
    if (thread_count >= NUM_THREADS) {
        int i;
        for (i = 0; i < NUM_THREADS; i++) {
            rc = pthread_join(threads[i], NULL);
            if (rc != 0) {
                fatal("handle_call: pthread_join failed", "", -1);
            }
        }
        thread_count = 0;
    }
}

void *serve_request(void *argument) {
    int clientfd = *((int *) argument);

    /*
     * MAX_MSG_LEN is large, on the order of 1 MB
     * therefore we must malloc the buffer for it in order to put it on the heap.
     * conversely if we said char buffer[MAX_MSG_LEN] it might cause a stack overflow
     */
    char *buffer = emalloc(MAX_MSG_LEN * sizeof(char));
    int send_rqlen;

    // re-write the request into format suitable for destination server
    // and extract some elements of the request needed to connect
    FLEXLIST *request_info = prepare_request(clientfd, buffer, MAX_MSG_LEN, &send_rqlen);

    if (request_info == NULL || send_rqlen == 0) {
        close(clientfd);
        return NULL;
    }

    // send request
    int serverfd = connect_to_server(fl_getlist(request_info)[REQ_HOST],
                                     fl_getlist(request_info)[REQ_PORT]);
    if (serverfd < 0) {
        close(clientfd);
        return NULL;
    }
    fl_free(request_info);
    int num_sent = send(serverfd, buffer, send_rqlen, 0);
    if (num_sent < 0) {
        fatal("serve_request: failure sending to server","",1);
    }

    // two-way dialog until either client or server closes connection
    relay_data(clientfd, serverfd, buffer);

    free(buffer);
    if (fcntl(clientfd, F_GETFD) != -1) {
        close(clientfd);
    }
    if (fcntl(serverfd, F_GETFD) != -1) {
        close(serverfd);
    }
    
    return NULL;
}

void relay_data(int clientfd, int serverfd, char *buffer) {
    fd_set rfds;
    struct timeval tv;
    int retval, num_received, num_sent, maxfd, rc;

    FD_ZERO(&rfds);
    FD_SET(clientfd, &rfds);
    FD_SET(serverfd, &rfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    maxfd = (serverfd > clientfd ? serverfd : clientfd);

    while ( fcntl(clientfd, F_GETFD) != -1 && fcntl(serverfd, F_GETFD) != -1 ) {
        retval = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (retval == -1) {
            fatal("relay data","select failure", 1);
        } else if (retval == 0) {
            /* no data after timeout seconds */
            num_received = 0;
            num_sent = 0;
        } else {
            rc = talk(clientfd, serverfd, &rfds, buffer);
            if (rc < 0)
                return;
        }
    }
}

int talk(int clientfd, int serverfd, fd_set *rfdsp, char *buffer) {
    int num_received, num_sent;
    if (FD_ISSET(clientfd, rfdsp)) {
        num_received = recv(clientfd, buffer, MAX_MSG_LEN, MSG_DONTWAIT);
        if (num_received < 0) {
            fatal("talk","recv failure from client", 1);
        } else if (num_received == 0) {
            // nothing to send and clientfd dead so no more communication possible
            return -1;
        } else {
            // avoid error if serverfd is closed, otherwise die on error
            if (fcntl(serverfd, F_GETFD) != -1) {
                num_sent = send(serverfd, buffer, num_received, 0);
                if (num_sent < 0) {
                    fatal("talk","send failure to server", 1);
                }
            }
        }
    }

    if (FD_ISSET(serverfd, rfdsp)) {
        num_received = recv(serverfd, buffer, MAX_MSG_LEN, MSG_DONTWAIT);
        if (num_received < 0) {
            fatal("talk","recv failure from server", 1);
        } else if (num_received == 0) {
            // nothing to send and serverfd dead so no more communication possible
            return -1;
        } else {
            // avoid error if clientfd is closed, otherwise die on error
            if (fcntl(clientfd, F_GETFD) != -1) {
                num_sent = send(clientfd, buffer, num_received, 0);
                if (num_sent < 0) {
                    fatal("talk","send failure to client", 1);
                }
            }
        }
    }

    return 0;
}


/*
 * read the http request into rq not to exceed rqlen
 * also return a FLEXLIST with components that will be used to connect to desired server
 */
FLEXLIST *prepare_request(int sockfd, char rq[], int rqlen, int *final_rqlen)
{
    int num_bytes;
    char *cp;
    char peek_buf[LINELEN];
    char discard_buf[LINELEN];

    num_bytes = recv(sockfd, peek_buf, LINELEN, MSG_PEEK | MSG_DONTWAIT);
    int read_til_crnl(char *, int);
    // (LINELEN) - 1 to leave room for a null byte at the end
    int start_line_length = read_til_crnl(peek_buf, (LINELEN) - 1);
    // truncate what we peeked at to the actual end of the first line
    peek_buf[start_line_length] = '\0';
    FLEXLIST *request_line = splitline(peek_buf);
    if (request_line == NULL || fl_getcount(request_line) != 3) {
        *final_rqlen = 0;
        return NULL;
    }

    // discard the original start line
    num_bytes = recv(sockfd, discard_buf, start_line_length, MSG_DONTWAIT);
    if ((num_bytes) < 0)
        perror("recv");

    // parse the start line and put the re-written version on the request
    FLEXLIST *parsed_request = parse_request_uri(fl_getlist(request_line)[RL_REQUEST_URI]);
    void write_request_line(char *buf, char *method, char *path, char* http_version);

    // temporarily treat request as a string, i.e. give it a terminating null byte
    // and rely on that to compute length
    // we cannot assume the final version has a '\0' though
    write_request_line( rq, fl_getlist(request_line)[RL_METHOD],
                        fl_getlist(parsed_request)[RU_PATH],
                        fl_getlist(request_line)[RL_HTTP_VERSION] );

    // set pointer where to start writing the rest of the request
    // this will overwrite the current terminating null byte
    int new_length = strlen(rq);
    cp = rq + new_length;

    // add on the rest of the request
    num_bytes = recv(sockfd, cp, (MAX_MSG_LEN) - new_length, MSG_DONTWAIT);
    if ((num_bytes) < 0)
        perror("recv");

    // tell the caller how long the final request is
    *final_rqlen = new_length + num_bytes;

    // finally return a new FLEXLIST with the fully parsed pieces of the start line
    FLEXLIST *strings = emalloc(sizeof(FLEXLIST));;
	fl_init(strings,0);

    // copy the strings so we can free the original lists in this function
    char *m = fl_getlist(request_line)[RL_METHOD];
    char *v = fl_getlist(request_line)[RL_HTTP_VERSION];
    char *proto = fl_getlist(parsed_request)[RU_PROTOCOL];
    char *h = fl_getlist(parsed_request)[RU_HOST];
    char *port = fl_getlist(parsed_request)[RU_PORT];
    char *path = fl_getlist(parsed_request)[RU_PATH];

    fl_append(strings, newstr(m, strlen(m)));
    fl_append(strings, newstr(v, strlen(v)));
    fl_append(strings, newstr(proto, strlen(proto)));
    fl_append(strings, newstr(h, strlen(h)));
    fl_append(strings, newstr(port, strlen(port)));
    fl_append(strings, newstr(path, strlen(path)));

    fl_free(request_line);
    fl_free(parsed_request);
    return strings;    
}


void write_request_line(char *buf, char *method, char *path, char* http_version) {
    // make sure the request buffer starts with a null byte for strcat
    *buf = '\0';
    char *SP = " ";
    strcat(buf, method);
    strcat(buf, SP);
    strcat(buf, path);
    strcat(buf, SP);
    strcat(buf, http_version);
}

int read_til_crnl(char *buf, int len)
{
    // starting from the beginning of buf search for CRLF.
    // if found return the length of the sub-string including that 2-char sequence.
    // if not found return -1
    char cur, prev;
    char *cp = buf;
    cur = *cp;
    int i = 0;
    while (i < len) {
        prev = cur;
        cur = *cp++;
        i++;        
        if (prev == '\r' && cur == '\n') {
            return i;
        }
    }
    return -1;
}



FLEXLIST *splitline(char *line)
/*
 * purpose: split a line into list of space separated tokens
 * returns: a flexlist with copies of the tokens
 *          or NULL if line is NULL.
 *          (If no tokens on the line, then the array returned by splitline
 *           contains only the terminating NULL.)
 *  action: traverse the array, locate strings, make copies
 */
{
	if ( line == NULL )
		return NULL;

	char *token;
    char *search = " "; 
	FLEXLIST *strings = emalloc(sizeof(FLEXLIST));    
	fl_init(strings,0);

    token = strtok(line, search);
    while (token != NULL) {
    	fl_append(strings, newstr(token, strlen(token)));
        token = strtok(NULL, search);
    }

	return strings;
}

FLEXLIST *parse_request_uri(char *request_uri) {
	if ( request_uri == NULL ) {
        fatal("parse_request_uri: null request passed", "", 1);
    }

	FLEXLIST *strings = emalloc(sizeof(FLEXLIST));
	fl_init(strings,0);

    fl_append(strings, extract_protocol(request_uri));
    fl_append(strings, extract_host(request_uri));
    fl_append(strings, extract_port(request_uri));
    fl_append(strings, extract_path(request_uri));

    return strings;
}

char *extract_protocol(char *request_uri) {
    // copy to avoid destroying the input string
    char *copy = newstr(request_uri, strlen(request_uri));
	char *token;
    char *search = ":";
    // store the protocol. should be http or https
    token = strtok(copy, search);
    if (token == NULL) {
        fatal("parse_request_uri: missing protocol in request", request_uri, 1);
    }
    char *protocol = newstr(token, strlen(token));
    return protocol;
}

char *extract_host(char *request_uri) {
    // copy to avoid destroying the input string
    char *copy = newstr(request_uri, strlen(request_uri));
	char *token;
    char *search = ":/";
    // the host should be preceded by http(s)://
    token = strtok(copy, search);
    token = strtok(NULL, search);
    if (token == NULL) {
        fatal("parse_request_uri: nothing after protocol in request", request_uri, 1);
    }

    // now pointing at the beginning of the host. remove the path and port (if present)
    char *host = newstr(token, strlen(token));
    char *idx;
    if ( (idx = strstr(host, ":")) != NULL || (idx = strstr(host, "/"))) {
        *idx = '\0';
    }
    return host;
}

char *extract_port(char *request_uri) {
    // copy to avoid destroying the input string
    char *copy = newstr(request_uri, strlen(request_uri));
	char *token;
    char *search = ":";
    // if there is a port it will come after the second colon (the first is in http:)
    // it will have a / following
    token = strtok(copy, search);
    token = strtok(NULL, search);
    token = strtok(NULL, search);
    char *port;
    if (token != NULL) {
        port = newstr(token, strlen(token));
        char *idx;
        if ((idx = strstr(port, "/")) != NULL ) {
            *idx = '\0';
        }
    } else {
        port = newstr("80", 2);
    }
    return port;
}

char *extract_path(char *request_uri) {
    // copy to avoid destroying the input string
    char *copy = newstr(request_uri, strlen(request_uri));
	char *token;
    char *search = "/";
    // if there is a path it will come after the third slash (the first and second are in http://)
    token = strtok(copy, search);
    token = strtok(NULL, search);
    token = strtok(NULL, search);

    FLEXSTR *path = emalloc(sizeof(FLEXSTR));;
    fs_init(path, 0);
    // we will re-add the / at the beginning whether we find a path or not
    fs_addch(path, '/');
    if (token != NULL) {
        fs_addstr(path, newstr(token, strlen(token)));
    }
    return fs_getstr(path);
}


/*
 * purpose: constructor for strings
 * returns: a string, never NULL
 */
char *newstr(char *s, int l)
{
	char *rv = emalloc(l+1);

	rv[l] = '\0';
	strncpy(rv, s, l);
	return rv;
}

/*
 * initialization function
 *  1. process command line args
 *      handles -c configfile
 *  2. open config file
 *      read port, backlog
 *  3. open a socket on port
 *  4. get the hostname
 *  5. return the socket
 *
 *  returns: socket as the return value
 *         the host by writing it into host[]
 *         the port by writing it into *portnump
 */
int startup(int ac, char *av[],char host[], int *portnump)
{
    int sock;
    int portnum = PORTNUM, backlog = BACKLOG;
    char *configfile = CONFIG_FILE;
    int pos;
    void process_config_file(char *, int *, int *);

    for(pos=1;pos<ac;pos++){
        if ( strcmp(av[pos],"-c") == 0 ){
            if ( ++pos < ac )
                configfile = av[pos];
            else
                fatal("missing arg for -c", NULL, 1);
        }
    }
    process_config_file(configfile, &portnum, &backlog);
            
    sock = make_server_socket( portnum, backlog );
    if ( sock == -1 ) 
        oops("making socket",2);
    strcpy(host, full_hostname());
    *portnump = portnum;
    return sock;
}


/*
 * opens file or dies
 * reads file for lines with the format
 *   port ###
 *   backlog ###
 * return the portnum and backlog by loading *portnump and *backlogp
 */
void process_config_file(char *conf_file, int *portnump, int *backlogp)
{
    FILE *fp;
    char param[PARAM_LEN];
    char value[VALUE_LEN];
    int port, backlog;
    int read_param(FILE *, char *, int, char *, int );

    /* open the file */
    if ( (fp = fopen(conf_file,"r")) == NULL )
        fatal("Cannot open config file %s", conf_file, 1);

    /* extract the settings */
    while( read_param(fp, param, PARAM_LEN, value, VALUE_LEN) != EOF )
    {
        if ( strcasecmp(param,"backlog") == 0 )
            backlog = atoi(value);
        if ( strcasecmp(param,"port") == 0 )
            port = atoi(value);
    }
    fclose(fp);

    /* act on the settings */
    *backlogp = backlog;
    *portnump = port;
    return;
}

/*
 * read_param:
 *   purpose -- read next parameter setting line from fp
 *   details -- a param-setting line looks like name value
 *        for example:  port 4444
 *     extra -- skip over lines that start with # and those
 *        that do not contain two strings
 *   returns -- EOF at eof and 1 on good data
 *
 */
int read_param(FILE *fp, char *name, int nlen, char* value, int vlen)
{
    char line[LINELEN];
    int c;
    char fmt[100] ;

    sprintf(fmt, "%%%ds%%%ds", nlen, vlen);

    /* read in next line and if the line is too long, read until \n */
    while( fgets(line, LINELEN, fp) != NULL )
    {
        if ( line[strlen(line)-1] != '\n' )
            while( (c = getc(fp)) != '\n' && c != EOF )
                ;
        if ( sscanf(line, fmt, name, value ) == 2 && *name != '#' )
            return 1;
    }
    return EOF;
}

char *
full_hostname()
/*
 * returns full hostname for current machine
 */
{
    char hname[MAXHOSTNAMELEN];
    static char fullname[MAXHOSTNAMELEN];
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
    freeaddrinfo(result);
    return fullname;
}
