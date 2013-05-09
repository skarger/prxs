/* header file for prxs: proxy server */
#ifndef _PRXS_
#define _PRXS_

#define	CONFIG_FILE	"prxs.conf"
#define SERVER_NAME	"prxs"
#define	VERSION		"1"
#define	PORTNUM	8080

// for config file
#define	PARAM_LEN	128
#define	VALUE_LEN	512

// allow a queue of BACKLOG connections
// as connections arrive they will be handed off to threads for service
// capping the NUM_THREADS in play at 10 times the backlog size
// TODO: experiment with these values 
#define	BACKLOG 5
#define NUM_THREADS 50

// allow HTTP requests and responses up to 1 MB
#define	MAX_MSG_LEN	1048576
#define	LINELEN		4096


// indices of substrings within HTTP request line
// RFC 2616: Request-Line   = Method SP Request-URI SP HTTP-Version CRLF
#define RL_METHOD 0
#define RL_REQUEST_URI 1
#define RL_HTTP_VERSION 2

// indices of substrings within HTTP request URI
#define RU_PROTOCOL 0
#define RU_HOST 1
#define RU_PORT 2
#define RU_PATH 3

// indices of parsed components of HTTP request line returned by prepare_request
#define REQ_METHOD 0
#define REQ_HTTP_VERSION 1
#define REQ_PROTOCOL 2
#define REQ_HOST 3
#define REQ_PORT 4
#define REQ_PATH 5

#define	oops(m,x)	{ perror(m); exit(x); }


/*
 * prototypes
 */


int	startup(int, char *a[], char [], int *);
char *full_hostname();
void handle_call(int);
void *serve_request(void *argument);
FLEXLIST *prepare_request(int sockfd, char rq[], int rqlen, int *final_rqlen);
FLEXLIST *parse_request_uri(char *);
char *extract_protocol(char *request_uri);
char *extract_host(char *request_uri);
char *extract_port(char *request_uri);
char *extract_path(char *request_uri);
int read_til_crnl(char *, int);

char *newstr(char *s, int l);
FLEXLIST *splitline(char *line);

int connect_to_host(FLEXLIST *request_info);
void relay_data(int clientfd, int serverfd, char *buffer);
int talk(int clientfd, int serverfd, fd_set *rdfsp, char *buffer);

#endif
