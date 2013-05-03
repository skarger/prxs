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
        fd = accept( sock, NULL, NULL );    /* take a call  */
        if ( fd == -1 )
            perror("accept");
        else
            handle_call(fd);        /* handle call    */
    }
    return 0;
    /* never end */
}

/*
 * handle_call(fd) - serve the request arriving on fd
 * summary: fork, then get request, then process request
 *    rets: child exits with 1 for error, 0 for ok
 *    note: closes fd in parent
 */
void handle_call(int fd)
{
    int    pid = fork();
    FILE    *fpin, *fpout;
    char    request[MAX_RQ_LEN];


    /* child: buffer socket and talk with client */
    if ( pid == 0 )
    {
        fpin  = fdopen(fd, "r");
        fpout = fdopen(fd, "w");
        if ( fpin == NULL || fpout == NULL )
            exit(1);

        if ( read_request(fpin, request, MAX_RQ_LEN) == -1 )
            exit(1);
        printf("got a call: request = %s", request);
        process_rq(request, fpout);
        fflush(fpout);        /* send data to client    */
        exit(0);        /* child is done    */
                    /* exit closes files    */
    }
    /* error and parent execute this code */
    if ( pid == -1 )
        perror("fork");
    close(fd);
}

/*
 * read the http request into rq not to exceed rqlen
 * return -1 for error, 0 for success
 */
int read_request(FILE *fp, char rq[], int rqlen)
{
    /* null means EOF or error. Either way there is no request */
    if ( readline(rq, rqlen, fp) == NULL )
        return -1;
    FLEXLIST *request_line = splitline(newstr(rq, rqlen));
    printf("nused: %d\n", fl_getcount(request_line));
    if (fl_getcount(request_line) != 3) {
        fatal("malformed request", rq, 1);
    }
    // process request line
    read_til_crnl(fp);
//    void read_til_crnl2(FILE *fp, int len, char rq[]);
//    read_til_crnl2(fp, MAX_RQ_LEN, rq);
    return 0;
}

void read_til_crnl(FILE *fp)
{
        char    buf[MAX_RQ_LEN];
        while( readline(buf,MAX_RQ_LEN,fp) != NULL 
            && strcmp(buf,"\r\n") != 0 )
                ;
}

void read_til_crnl2(FILE *fp, int len, char rq[])
{
        int     space = len - 3;
        char    *cp = rq;
        int     n = getc(fp), c = n, p = c;

        while( ( n = getc(fp) ) != EOF ){
                if ( space-- > 0 ) {
                    if ( n == '\n' && c == '\r' && p == '\n' ) {
                        *cp++ = '\0';
                        return;
                    } else {
                        *cp++ = c;
                        p = c; c = n;
                    }                        
                }
        }
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
    char    cmd[MAX_RQ_LEN], arg[MAX_RQ_LEN];
    char    *item, *modify_argument();

    if ( sscanf(rq, "%s%s", cmd, arg) != 2 ){
        bad_request(fp);
        return;
    }

    item = modify_argument(arg, MAX_RQ_LEN);
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
    return fullname;
}


