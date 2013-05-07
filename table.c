#include	<string.h>
#include	"prxs.h"
#include	"flexstr.h"

/* table data structures used in wsng */


/* error messages */

/*
 * purpose: populate a flexlist with all defined text error messages
 * input: err_list - pointer to flexlist to populate
 * returns: 0 if went OK, 1 if error
 */
int build_errors( FLEXLIST *err_list )
{

	fl_init( err_list, 0 );

	if ( fl_append( err_list, 
	"bad_request=I cannot understand your request") != 0 ) return 1;

	if ( fl_append( err_list, 
	"cannot_do=That command is not yet implemented") != 0 ) return 1;

	if ( fl_append( err_list, 
	"do_404=The item you requested: is not found") != 0 ) return 1;

	return 0;
}

/*
 * purpose: retrieve error message given name
 * input: err - ptr to flexlist with errors stored
 * 	name - string to search for
 * returns: pointer to error message string, or NULL if not found
 */	
char *get_err_msg( FLEXLIST *err, char *name )
{
	int cnt = fl_getcount( err );
	char **errors = fl_getlist( err );

	int i;
	for( i = 0; i < cnt; i++ )
		if ( strncmp( errors[i], name, strlen(name) ) == 0 )
			return ( strchr(errors[i],'=') + 1 );

	return NULL;
}

