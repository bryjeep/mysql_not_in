/* Copyright (C) 2002 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*
** The functions name, type and shared library is saved in the new system
** table 'func'.  To be able to create new functions one must have write
** privilege for the database 'mysql'.	If one starts MySQL with
** --skip-grant, then UDF initialization will also be skipped.
**
** Syntax for the new commands are:
** create function <function_name> returns {string|real|integer}
**		  soname <name_of_shared_library>
** drop function <function_name>
**
** Each defined function may have a xxxx_init function and a xxxx_deinit
** function.  The init function should alloc memory for the function
** and tell the main function about the max length of the result
** (for string functions), number of decimals (for double functions) and
** if the result may be a null value.
**
** If a function sets the 'error' argument to 1 the function will not be
** called anymore and mysqld will return NULL for all calls to this copy
** of the function.
**
** All strings arguments to functions are given as string pointer + length
** to allow handling of binary data.
** Remember that all functions must be thread safe. This means that one is not
** allowed to alloc any global or static variables that changes!
** If one needs memory one should alloc this in the init function and free
** this on the __deinit function.
**
** Note that the init and __deinit functions are only called once per
** SQL statement while the value function may be called many times
**
** A dynamicly loadable file should be compiled shared.
** (something like: gcc -shared -o my_func.so myfunc.cc).
** You can easily get all switches right by doing:
** cd sql ; make udf_example.o
** Take the compile line that make writes, remove the '-c' near the end of
** the line and add -shared -o udf_example.so to the end of the compile line.
** The resulting library (udf_example.so) should be copied to some dir
** searched by ld. (/usr/lib ?)
** If you are using gcc, then you should be able to create the udf_example.so
** by simply doing 'make udf_example.so'.
**
** After the library is made one must notify mysqld about the new
** functions with the commands:
**
** CREATE AGGREGATE FUNCTION not_in RETURNS REAL SONAME "not_in.so";
**
** After this the functions will work exactly like native MySQL functions.
** Functions should be created only once.
**
** The functions can be deleted by:
**
** DROP FUNCTION not_in;
**
** The CREATE FUNCTION and DROP FUNCTION update the func@mysql table. All
** Active function will be reloaded on every restart of server
** (if --skip-grant-tables is not given)
**
** If you ge problems with undefined symbols when loading the shared
** library, you should verify that mysqld is compiled with the -rdynamic
** option.
**
** If you can't get AGGREGATES to work, check that you have the column
** 'type' in the mysql.func table.  If not, run 'mysql_upgrade'.
**
*/

#ifdef STANDARD
	/* STANDARD is defined, don't use any mysql functions */
	#include <stdlib.h>
	#include <stdio.h>
	#include <string.h>
	
	#ifdef __WIN__
		typedef unsigned __int64 ulonglong;	/* Microsofts 64 bit types */
		typedef __int64 longlong;
	#else
		typedef unsigned long long ulonglong;
		typedef long long longlong;
	#endif /*__WIN__*/
#else
	#include <my_global.h>
	#include <my_sys.h>
	
	#if defined(MYSQL_SERVER)
		#include <m_string.h>		/* To get strmov() */
	#else
		/* when compiled as standalone */
		#include <string.h>
		#define strmov(a,b) stpcpy(a,b)
		#define bzero(a,b) memset(a,0,b)
	#endif
#endif


#include <mysql.h>
#include <ctype.h>

#ifdef HAVE_DLOPEN

/* These must be right or mysqld will not find the symbol! */

my_bool not_in_init( UDF_INIT* initid, UDF_ARGS* args, char* message );
void not_in_deinit( UDF_INIT* initid );
void not_in_reset( UDF_INIT* initid, UDF_ARGS* args, char* is_null, char *error );
void not_in_clear( UDF_INIT* initid, char* is_null, char *error );
void not_in_add( UDF_INIT* initid, UDF_ARGS* args, char* is_null, char *error );
double not_in( UDF_INIT* initid, UDF_ARGS* args, char* is_null, char *error );

/*************************************************************************
** Example of init function
** Arguments:
** initid	Points to a structure that the init function should fill.
**		This argument is given to all other functions.
**	my_bool maybe_null	1 if function can return NULL
**				Default value is 1 if any of the arguments
**				is declared maybe_null.
**	unsigned int decimals	Number of decimals.
**				Default value is max decimals in any of the
**				arguments.
**	unsigned int max_length  Length of string result.
**				The default value for integer functions is 21
**				The default value for real functions is 13+
**				default number of decimals.
**				The default value for string functions is
**				the longest string argument.
**	char *ptr;		A pointer that the function can use.
**
** args		Points to a structure which contains:
**	unsigned int arg_count		Number of arguments
**	enum Item_result *arg_type	Types for each argument.
**					Types are STRING_RESULT, REAL_RESULT
**					and INT_RESULT.
**	char **args			Pointer to constant arguments.
**					Contains 0 for not constant argument.
**	unsigned long *lengths;		max string length for each argument
**	char *maybe_null		Information of which arguments
**					may be NULL
**
** message	Error message that should be passed to the user on fail.
**		The message buffer is MYSQL_ERRMSG_SIZE big, but one should
**		try to keep the error message less than 80 bytes long!
**
** This function should return 1 if something goes wrong. In this case
** message should contain something usefull!
**************************************************************************/

/*
** Syntax for the new aggregate commands are:
** create aggregate function <function_name> returns {string|real|integer}
**		  soname <name_of_shared_library>
**
** Syntax for not_in: not_in( value, reference )
**	with value=anything, reference=anything
*/

struct not_in_data
{
	ulonglong referenceCount;
	char ** references;
	ulonglong * referenceLengths;

	ulonglong valueCount;
	char ** values;
	ulonglong * valueLengths;
};


/*
** Average Cost Aggregate Function.
*/
my_bool
not_in_init( UDF_INIT* initid, UDF_ARGS* args, char* message )
{
	struct not_in_data*	data;
	
	if (args->arg_count != 2)
	{
		strcpy(
			message,
			"wrong number of arguments: NOT_IN() requires two arguments"
		);
		return 1;
	}
	
	/* Set type of argument for MySQL to coerce */
	
	args->arg_type[0] = STRING_RESULT;
	args->arg_type[1] = STRING_RESULT;
	
	initid->maybe_null	= 1;		/* The result may be null */
	initid->max_length	= args->attribute_lengths[0];	/*Max Length is length of value attribute*/
	
	/* Allocate the main data structure */
	if (!(data = (struct not_in_data*) malloc(sizeof(struct not_in_data))))
	{
		strmov(message,"Couldn't allocate memory");
		return 1;
	}
	
	/* Allocate the arrays */
	if (!(data->references = (char **) malloc(sizeof(char *))))
	{
		/*free the memory so far as it doesn't call deinit*/
		free(data);
		strmov(message,"Couldn't allocate memory");
		return 1;
	}

	if (!(data->referenceLengths = (ulonglong *) malloc(sizeof(ulonglong))))
	{
		/*free the memory so far as it doesn't call deinit*/
		free(data->references);
		free(data);
		strmov(message,"Couldn't allocate memory");
		return 1;
	}
	
	if (!(data->values = (char **) malloc(sizeof(char *))))
	{
		/*free the memory so far as it doesn't call deinit*/
		free(data->references);
		free(data->referenceLengths);
		free(data);
		strmov(message,"Couldn't allocate memory");
		return 1;
	}

	if (!(data->valueLengths = (ulonglong *) malloc(sizeof(ulonglong))))
	{
		/*free the memory so far as it doesn't call deinit*/
		free(data->references);
		free(data->referenceLengths);
		free(data->values);
		free(data);
		strmov(message,"Couldn't allocate memory");
		return 1;
	}
	
	data->referenceCount = 0;
	data->valueCount = 0;
		
	initid->ptr = (char*)data;
	
	return 0;
}


/****************************************************************************
** Deinit function. This should free all resources allocated by
** this function.
** Arguments:
** initid	Return value from xxxx_init
****************************************************************************/


void
not_in_deinit( UDF_INIT* initid )
{
	struct not_in_data* data = (struct not_in_data*)initid->ptr;
	
	/*Free all the strings that make up the string arrays*/
	for(int i=0; i < data->referenceCount; i++){
		free(data->references[i]);
	}
	for(int i=0; i < data->valueCount; i++){
		free(data->values[i]);
	}
		
	/*Free the arrays themself*/
	free(data->references);
	free(data->referenceLengths);
	free(data->values);
	free(data->valueLengths);
			
	/*Free the datastructure itself*/
	free(initid->ptr);
}

/* This is only for MySQL 4.0 compability */
void
not_in_reset(UDF_INIT* initid, UDF_ARGS* args, char* is_null, char* message)
{
	not_in_clear(initid, is_null, message);
	not_in_add(initid, args, is_null, message);
}

/* This is needed to get things to work in MySQL 4.1.1 and above */

void
not_in_clear(UDF_INIT* initid, char* is_null __attribute__((unused)),
              char* message __attribute__((unused)))
{
	struct not_in_data* data = (struct not_in_data*)initid->ptr;

	for(int i=0; i < data->referenceCount; i++){
		free(data->references[i]);
	}
	
	for(int i=0; i < data->valueCount; i++){
		free(data->values[i]);
	}
	
	data->referenceCount = 0;
	data->valueCount = 0;
	
	/*SHOULD WE REALLOC HERE ALSO ?*/
}

/***************************************************************************
** UNKNOWN FORMAT RIGH NOW
***************************************************************************/
void
not_in_add(UDF_INIT* initid, UDF_ARGS* args,
            char* is_null __attribute__((unused)),
            char* message __attribute__((unused)))
{
	struct not_in_data* data	= (struct not_in_data*)initid->ptr;
	
	my_bool referencesHaveValue = 0;
	my_bool referencesHaveReference = 0;

	my_bool valuesHaveValue = 0;
	my_bool valuesHaveReference = 0;
			
	/*
	**loop through every element of reference array in this group
	**if we already have the value arg and reference arg in the reference list then break out early
	*/
	for(int i=0; i < data->referenceCount && !(referencesHaveValue && referencesHaveReference); i++){
		if(args->lengths[0] == data->referenceLengths[i] && memcmp(args->args[0],data->references[i],args->lengths[0]) == 0){
			/*DON'T ADD VALUE*/
			referencesHaveValue = 1;
		}
		if(args->lengths[1] == data->referenceLengths[i] && memcmp(args->args[1],data->references[i],args->lengths[1]) == 0){
			/*DON'T ADD REFERENCE*/
			referencesHaveReference = 1;
		}
	}
	
	/*
	**loop through every element of value array in this group
	**if we already have the value arg and reference arg in the value list then break out early
	*/
	for(int i=0; i < data->valueCount && !(valuesHaveValue && valuesHaveReference); i++){
		if(args->lengths[0] == data->valueLengths[i] && memcmp(args->args[0],data->values[i],args->lengths[0]) == 0){
			/*DON'T ADD VALUE*/
			valuesHaveValue = 1;
		}
		if(args->lengths[1] == data->valueLengths[i] && memcmp(args->args[1],data->values[i],args->lengths[1]) == 0){
			/*REMOVE VALUE*/
			/* this is done by moving last value to current spot */
			free(data->values[i]);
			data->valueCount--;
			data->valueLengths[i] = data->valueLengths[data->valueCount];
			data->values[i] = data->values[data->valueCount];
						
			/*SHOULD WE REALLOC HERE ALSO ?*/
		}
	}
	
	/*
	**If the references don't already have the value arg
	**and the value is not equal to the new reference arg
	*/
	if	(	!referencesHaveValue && 
			!valuesHaveValue &&
			!( /*TEST TO SEE IF ARGS ARE SAME*/
				args->lengths[0] == args->lengths[1] && /*Same Lengths*/
				args->args[0] && /*Not Null*/
				args->args[1] && /*Not Null*/
				memcmp(args->args[0],args->args[1],args->lengths[1]) == 0/*Same bytes*/
			) 
		){
		char * newValue = NULL;
		ulonglong newValueLength = 0;

		if (!(newValue = (char*) malloc(args->lengths[0])))
		{
			strmov(message,"Couldn't allocate string");
			return;
		}
		newValueLength = args->lengths[0];
		memcpy(newValue,args->args[0],newValueLength);
		
		/*add new value to array*/
		data->valueCount++;
		if (!(data->values = (char **) realloc(data->values, data->valueCount * sizeof(char *))))
		{
			strmov(message,"Couldn't reallocate memory");
			return;
		}
		if (!(data->valueLengths = (ulonglong *) realloc(data->valueLengths, data->valueCount * sizeof(ulonglong))))
		{
			strmov(message,"Couldn't reallocate memory");
			return;
		}
		data->values[data->valueCount-1]=newValue;
		data->valueLengths[data->valueCount-1]=newValueLength;		
	}
	
	if(!referencesHaveReference && args->args[1]){		
		char * newReference = NULL;
		ulonglong newReferenceLength = 0;
		
		if (!(newReference = (char*) malloc(args->lengths[1])))
		{
			strmov(message,"Couldn't allocate string");
			return;
		}
		newReferenceLength = args->lengths[1];
		memcpy(newReference,args->args[1],newReferenceLength);
		
		/*add new reference to array*/
		data->referenceCount++;
		if (!(data->references = (char **) realloc(data->references, data->referenceCount * sizeof(char *))))
		{
			strmov(message,"Couldn't reallocate memory");
			return;
		}
		if (!(data->referenceLengths = (ulonglong *) realloc(data->referenceLengths, data->referenceCount * sizeof(ulonglong))))
		{
			strmov(message,"Couldn't reallocate memory");
			return;
		}
		data->references[data->referenceCount-1]=newReference;
		data->referenceLengths[data->referenceCount-1]=newReferenceLength;	
	}
}

/***************************************************************************
** UDF string function.
** Arguments:
** initid	Structure filled by xxx_init
** args		The same structure as to xxx_init. This structure
**		contains values for all parameters.
**		Note that the functions MUST check and convert all
**		to the type it wants!  Null values are represented by
**		a NULL pointer
** result	Possible buffer to save result. At least 255 byte long.
** length	Pointer to length of the above buffer.	In this the function
**		should save the result length
** is_null	If the result is null, one should store 1 here.
** error	If something goes fatally wrong one should store 1 here.
**
** This function should return a pointer to the result string.
** Normally this is 'result' but may also be an alloced string.
***************************************************************************/

char *
not_in( UDF_INIT *initid, UDF_ARGS *args __attribute__((unused)),
		char *result, unsigned long *length,
		char *is_null, char *error __attribute__((unused)))
{
	struct not_in_data* data = (struct not_in_data*)initid->ptr;
	
	/*
	SHOULD I USE THE RESULT BUFFER?
	*length = (unsigned long) 0;
	*result = data->currentValue;
	*/
	
	return result;
}

#endif /* HAVE_DLOPEN */
