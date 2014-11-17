/*  */
#include <stdlib.h>
#include <string.h>
#include <ctpublic.h>

/* patch by Charles
 *   Current stable freetds (0.64) and even current release candidate freetds (0.82rc4) 
 *   don't have either CS_ESYNTAX, or CS_TRUNCATED. */
#ifdef FREETDS
#ifndef CS_CONV_ERR
#define CS_CONV_ERR		-100
#endif
#ifndef CS_ESYNTAX
#define CS_ESYNTAX		(CS_CONV_ERR - 5)
#endif
#ifndef CS_TRUNCATED
#define CS_TRUNCATED		(CS_CONV_ERR - 13)
#endif
#endif  /* End of FREETDS */

/* Set latest CS_VERSION_XXX, thanks to Will Sobel */
#if defined (CS_CURRENT_VERSION)
#define SYB_CTLIB_VERSION CS_CURRENT_VERSION
#elif defined (CS_VERSION_125)
#define SYB_CTLIB_VERSION CS_VERSION_125
#elif defined (CS_VERSION_120)
#define SYB_CTLIB_VERSION CS_VERSION_120
#elif defined(CS_VERSION_110)
#define SYB_CTLIB_VERSION CS_VERSION_110
#else
#define SYB_CTLIB_VERSION CS_VERSION_100
#endif

/* initial allocate row buffer size */
#define SYB_INITIAL_ROWSIZE	128

#ifndef MAX
#define MAX(X,Y)	(((X) > (Y)) ? (X) : (Y))
#endif

typedef struct {
  int is_async;			/* ASYNC IO? */
  CS_INT timeout;		/* CS_TIMEOUT value when ASYNC IO */
} SYB_IO_INFO;

typedef struct {
  CS_CONTEXT *val;
  int ct_init_flag;		/* ct_init flag (NOT USE) */
  SYB_IO_INFO ioinfo;		/* IO information */
#ifdef USE_MEMPOOL
  CS_VOID *mempool ;		
#endif
} SYB_CONTEXT_DATA;

typedef struct {
  CS_CONNECTION *val;
  int is_connect;		/* ct_connect flag */
  SYB_IO_INFO ioinfo;		/* IO information */
} SYB_CONNECTION_DATA;

/* Using ct_fetch's return value */
typedef struct  {
  /* row data format */
  CS_DATAFMT datafmt;

  int is_bind;	  /* is bined ? */

#define RUBY_NUM_TYPE 1
#define RUBY_STR_TYPE 2
#define RUBY_FLOAT_TYPE 3
#ifndef BIND_TIMESTAMP_AS_CHAR
#define RUBY_TIMESTAMP_TYPE 20
#endif
  int ruby_type;

  CS_SMALLINT	indicator;
  CS_INT	valuelen;
  /* CS_CHAR_TYPE buffer */
  CS_CHAR		*svalue;
  /* CS_INT_TYPE buffer */
  CS_INT		ivalue;
  /* CS_FLOAT_TYPE buffer */
  CS_FLOAT		fvalue;
#ifndef BIND_TIMESTAMP_AS_CHAR
  /* TIMESTAMP buffer */
  CS_BYTE		timestamp[CS_TS_SIZE];
#endif
} SYB_COLUMN_DATA;

typedef struct {
  CS_COMMAND *val;
#define SYBSTATUS_IDLE 0
#define SYBSTATUS_COMMAND_OPENED 1
#define SYBSTATUS_CURSOR_DECLARED 2
  int status;		
  SYB_COLUMN_DATA *colbuf;
  int len_colbuf; /* length of colbuf */

  CS_CONNECTION *conn;	/* pointer of parent handle */
  SYB_IO_INFO ioinfo;		/* IO information */
} SYB_COMMAND_DATA;

typedef struct {
  CS_IODESC body;
} SYB_IODESC_DATA;

/* 
   CS_USERDATA structure that allocates CS_CONNECTION object.
   (Getting argument when sybase's callback convert to ruby's callback)
*/
typedef struct {
  VALUE ctxobj;
  VALUE conobj;
} SYB_CALLBACK_USERDATA ;

#ifdef SYBCT_MAIN
VALUE cSybConstant;
VALUE cSybContext;
VALUE cSybConnection;
VALUE cSybCommand;
VALUE cSybIODesc;
#else
extern VALUE cSybConstant;
extern VALUE cSybContext;
extern VALUE cSybConnection;
extern VALUE cSybCommand;
extern VALUE cSybIODesc;
#endif
