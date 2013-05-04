/* header file for prxs: proxy server */
#ifndef _PRXS_
#define _PRXS_

#include	<stdio.h>
#include	<stdlib.h>
#include	<strings.h>
#include	<string.h>
#include	<netdb.h>
#include	<errno.h>
#include	<unistd.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/param.h>
#include	<signal.h>
#include	<time.h>
#include	"socklib.h"
#include	"flexstr.h"


#define	PORTNUM	8080
#define	BACKLOG 5
#define	CONFIG_FILE	"prxs.conf"
#define	VERSION		"1"

#define	MAX_RQ_LEN	4096
#define	LINELEN		1024
#define	PARAM_LEN	128
#define	VALUE_LEN	512

// indices of substrings within HTTP request line
#define RL_METHOD 0
#define RL_REQUEST_URI 1
#define RL_HTTP_VERSION 2

// indices of substrings within HTTP request URI
#define RU_PROTOCOL 0
#define RU_HOST 1
#define RU_PATH 2

#define	oops(m,x)	{ perror(m); exit(x); }



#define SERVER_NAME	"prxs"

/* can change to 0 if do not want to send server in header for security */
#define SEND_SERVER	1


/*
 * prototypes
 */

/* taken from ws.c */
int	startup(int, char *a[], char [], int *);
void	read_til_crnl(FILE *);
void	process_rq( char *, FILE *);
void	bad_request(FILE *);
void	cannot_do(FILE *fp);
void	do_404(char *item, FILE *fp);
void	do_cat(char *f, FILE *fpsock);
void	do_exec( char *prog, FILE *fp);
void	do_ls(char *dir, FILE *fp);
int	ends_in_cgi(char *f);
char 	*file_type(char *f);
void	header( FILE *fp, int code, char *msg, char *content_type );
int	isadir(char *f);
char	*modify_argument(char *arg, int len);
int	not_exist(char *f);

void	handle_call(int);
int	read_request(FILE *, char *, int);
char	*readline(char *, int, FILE *);

char *newstr(char *s, int l);
FLEXLIST *splitline(char *line);

/* added for assignment */
char * rfc822_time(time_t thetime);
int build_errors( FLEXLIST *err_list );
char *get_err_msg( FLEXLIST *err, char *name );

#endif
