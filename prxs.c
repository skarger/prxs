#include    <stdio.h>
#include    <stdlib.h>
#include    <strings.h>
#include    <string.h>
#include    <netdb.h>
#include    <errno.h>
#include    <unistd.h>
#include    <sys/types.h>
#include    <sys/stat.h>
#include    <sys/param.h>
#include    <signal.h>
#include    <time.h>
#include    <pthread.h>
#include    "socklib.h"
#include    "flexstr.h"
#include	"util.h"
#include    "prxs.h"

// RFC 2616
// Request-Line   = Method SP Request-URI SP HTTP-Version CRLF
#define	is_request_delim(x) ((x)==' ')

/*
 * prxs.c - proxy server
 * 
 * usage: prxs [ -c configfilenmame ]
 *
 * features: supports the GET command only
 *           runs in the current directory
 *           forks a new child to handle each request
 *
 * compile: cc prxs.c socklib.c -o prxs
 *
 * Adapted from simple web server code provided by Harvard Extension School
 * Course: CSCI-E215, Instructor: Bruce Molay
 */



char myhost[MAXHOSTNAMELEN];
int myport;



char *full_hostname();

/* global table of error messages */
//FLEXLIST err_list; 


int
main(int ac, char *av[])
{
    int sock, fd;



//    if ( build_errors( &err_list ) != 0 )
//        oops("could not build error message list\n", 1 );

    /* set up */
    sock = startup(ac, av, myhost, &myport);

    /* sign on */
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


    int fd = *((int *) argument);
    FILE *fpin, *fpout;

    /*
     * MAX_MSG_LEN is large, on the order of 1 MB
     * therefore we must malloc the buffer for it in order to put it on the heap.
     * conversely if we said char request[MAX_MSG_LEN] it might cause a stack overflow
     */
    char *request = emalloc(MAX_MSG_LEN * sizeof(char));

    /* buffer socket and talk with client */
    fpin  = fdopen(fd, "r");
    fpout = fdopen(fd, "w");
    if ( fpin == NULL || fpout == NULL )
        exit(1);

    FLEXLIST *request_info = prepare_request(fd, request, MAX_MSG_LEN);

    // connect to server
    // send request
    free(request);
    fl_free(request_info);

    // recv response
    // send to client (fd)

    process_rq(request, fpout);
    fflush(fpout);        /* send data to client    */
    close(fd);


    return NULL;
}

/*
 * read the http request into rq not to exceed rqlen
 * also return a FLEXLIST with components that will be used to connect to desired server
 */
FLEXLIST *prepare_request(int sockfd, char rq[], int rqlen)
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

/* like fgets, but truncates at len but reads until \n */
char *readline(char *buf, int len, FILE *fp)
{
        int     space = len - 2;
        char    *cp = buf;
        int     c;

        while( ( c = getc(fp) ) != '\n' && c != EOF ){
                if ( space-- > 0 )
                        *cp++ = c;
        }
        if ( c == '\n' )
                *cp++ = c;
        *cp = '\0';
        return ( c == EOF && cp == buf ? NULL : buf );
}


/**
 **	splitline ( parse a line into an array of strings )
 **/

FLEXLIST *splitline(char *line)
/*
 * purpose: split a line into list of space separated tokens
 * returns: a flexlist with copies of the tokens
 *          or NULL if line is NULL.
 *          (If no tokens on the line, then the array returned by splitline
 *           contains only the terminating NULL.)
 *  action: traverse the array, locate strings, make copies
 *    note: strtok() could work, but we may want to add quotes later
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
    token = strtok(NULL, search);
    if (token != NULL) {
        free(host);
        host = newstr(token, strlen(token));
    }
    return host;
}

char *extract_port(char *request_uri) {
    // copy to avoid destroying the input string
    char *copy = newstr(request_uri, strlen(request_uri));
	char *token;
    char *search = ":/";
    // if there is a port it will come after the second colon (the first is in http://)
    // it will have a / following but that will be removed by strtok
    token = strtok(copy, search);
    token = strtok(NULL, search);
    token = strtok(NULL, search);
    char *port;
    if (token != NULL) {
        port = newstr(token, strlen(token));
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
    


/* ------------------------------------------------------ *
   process_rq( char *rq, FILE *fpout)
   do what the request asks for and write reply to fp
   rq is HTTP command:  GET /foo/bar.html HTTP/1.0
   ------------------------------------------------------ */

void process_rq(char *rq, FILE *fp)
{
    char *cmd = emalloc(MAX_MSG_LEN * sizeof(char));
    char *arg = emalloc(MAX_MSG_LEN * sizeof(char));
    char    *item, *modify_argument();

    if ( sscanf(rq, "%s%s", cmd, arg) != 2 ){
        bad_request(fp);
        return;
    }

    item = modify_argument(arg, MAX_MSG_LEN);
    if ( strcmp(cmd,"GET") != 0 )
        cannot_do(fp);
    else if ( not_exist( item ) )
        do_404(item, fp );
    else if ( isadir( item ) )
        do_ls( item, fp );
    else if ( ends_in_cgi( item ) )
        do_exec( item, fp );
    else
        do_cat( item, fp );

    free (cmd);
    free(arg);
}

/*
 * modify_argument
 *  purpose: many roles
 *        security - remove all ".." components in paths
 *        cleaning - if arg is "/" convert to "."
 *  returns: pointer to modified string
 *     args: array containing arg and length of that array
 */

char *
modify_argument(char *arg, int len)
{
    char    *nexttoken;
    char    *copy = malloc(len);

    if ( copy == NULL )
        oops("memory error", 1);

    /* remove all ".." components from path */
    /* by tokenizing on "/" and rebuilding */
    /* the string without the ".." items    */

    *copy = '\0';

    nexttoken = strtok(arg, "/");
    while( nexttoken != NULL )
    {
        if ( strcmp(nexttoken,"..") != 0 )
        {
            if ( *copy )
                strcat(copy, "/");
            strcat(copy, nexttoken);
        }
        nexttoken = strtok(NULL, "/");
    }
    strcpy(arg, copy);
    free(copy);

    /* the array is now cleaned up */
    /* handle a special case       */

    if ( strcmp(arg,"") == 0 )
        strcpy(arg, ".");
    return arg;
}
/* ------------------------------------------------------ *
   the reply header thing: all functions need one
   if content_type is NULL then don't send content type
   ------------------------------------------------------ */

void
header( FILE *fp, int code, char *msg, char *content_type )
{
    fprintf(fp, "HTTP/1.0 %d %s\r\n", code, msg);
    if ( content_type )
        fprintf(fp, "Content-type: %s\r\n", content_type );

    if ( SEND_SERVER )
        fprintf(fp,"Server: %s/%s\r\n", SERVER_NAME, VERSION );
}

/* ------------------------------------------------------ *
   simple functions first:
    bad_request(fp)     bad request syntax
        cannot_do(fp)       unimplemented HTTP command
    and do_404(item,fp)     no such object
   ------------------------------------------------------ */

void
bad_request(FILE *fp)
{
    header(fp, 400, "Bad Request", "text/plain");
    fprintf(fp, "\r\n");

//    fprintf(fp, get_err_msg( &err_list, "bad_request" ) );
    //fprintf(fp, "I cannot understand your request\r\n");
}

void
cannot_do(FILE *fp)
{
    header(fp, 501, "Not Implemented", "text/plain");
    fprintf(fp, "\r\n");

    fprintf(fp, "That command is not yet implemented\r\n");
}

void
do_404(char *item, FILE *fp)
{
    header(fp, 404, "Not Found", "text/plain");
    fprintf(fp, "\r\n");

//    fprintf(fp, get_err_msg( &err_list, "do_404" ) );
/*
    fprintf(fp, "The item you requested: %s\r\nis not found\r\n", 
            item);
*/
}

/* ------------------------------------------------------ *
   the directory listing section
   isadir() uses stat, not_exist() uses stat
   do_ls runs ls. It should not
   ------------------------------------------------------ */

int
isadir(char *f)
{
    struct stat info;
    return ( stat(f, &info) != -1 && S_ISDIR(info.st_mode) );
}

int
not_exist(char *f)
{
    struct stat info;

    return( stat(f,&info) == -1 && errno == ENOENT );
}

/*
 * lists the directory named by 'dir' 
 * sends the listing to the stream at fp
 */
void
do_ls(char *dir, FILE *fp)
{
    int    fd;    /* file descriptor of stream */

    header(fp, 200, "OK", "text/plain");
    fprintf(fp,"\r\n");
    fflush(fp);

    fd = fileno(fp);
    dup2(fd,1);
    dup2(fd,2);
    execlp("/bin/ls","ls","-l",dir,NULL);
    perror(dir);
}

/* ------------------------------------------------------ *
   the cgi stuff.  function to check extension and
   one to run the program.
   ------------------------------------------------------ */

char *
file_type(char *f)
/* returns 'extension' of file */
{
    char    *cp;
    if ( (cp = strrchr(f, '.' )) != NULL )
        return cp+1;
    return "";
}

int
ends_in_cgi(char *f)
{
    return ( strcmp( file_type(f), "cgi" ) == 0 );
}

void
do_exec( char *prog, FILE *fp)
{
    int    fd = fileno(fp);

    header(fp, 200, "OK", NULL);
    fflush(fp);

    dup2(fd, 1);
    dup2(fd, 2);
    execl(prog,prog,NULL);
    perror(prog);
}
/* ------------------------------------------------------ *
   do_cat(filename,fp)
   sends back contents after a header
   ------------------------------------------------------ */

void
do_cat(char *f, FILE *fpsock)
{
    char    *extension = file_type(f);
    char    *content = "text/plain";
    FILE    *fpfile;
    int    c;

    if ( strcmp(extension,"html") == 0 )
        content = "text/html";
    else if ( strcmp(extension, "gif") == 0 )
        content = "image/gif";
    else if ( strcmp(extension, "jpg") == 0 )
        content = "image/jpeg";
    else if ( strcmp(extension, "jpeg") == 0 )
        content = "image/jpeg";

    fpfile = fopen( f , "r");
    if ( fpfile != NULL )
    {
        header( fpsock, 200, "OK", content );
        fprintf(fpsock, "\r\n");
        while( (c = getc(fpfile) ) != EOF )
            putc(c, fpsock);
        fclose(fpfile);
    }
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

/*
connect_to_host() {
    struct addrinfo *result;
    int s;
    s = getaddrinfo(hname, NULL, NULL, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }
}
*/



