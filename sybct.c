/*
  sybct.c

  Ruby module for SyBase.
  (Require SyBase Open Client library)

  Author: Tetsuhumi Takaishi

  Copyright (C) 1999-2001  Tetsuhumi Takaishi

  */

#include <stdio.h>	/* debug */

#include "ruby.h"
#ifdef HAS_THREAD_CRITICAL
#include "rubysig.h"
#endif

#define SYBCT_MAIN
#include "sybconf.h"
#include "sybct.h"

#ifndef SYB_DEBUG
#define IOWAIT(rc,fg,cn,oid) if( (fg)->is_async ) { \
 if( (rc) == CS_PENDING ) \
    (rc) = io_wait( (cn), (oid), (fg)->timeout ); \
 else if ( (rc) == CS_BUSY ) { \
    (rc) = CS_FAIL; \
  } \
 }

#else
#define IOWAIT(rc,fg,cn,oid) if( (fg)->is_async ) { \
 if( (rc) == CS_PENDING ) \
    (rc) = io_wait( (cn), (oid), (fg)->timeout ); \
 else if ( (rc) == CS_BUSY ) { \
    fprintf(stderr,"CS_BUSY in IOWAIT\n"); \
    (rc) = CS_FAIL; \
  } \
 }

#endif

#ifndef CS_BUSY  /* freetds */
#undef IOWAIT
#define IOWAIT(rc,fg,cn,oid) if( (fg)->is_async ) { \
 if( (rc) == CS_PENDING ) \
    (rc) = io_wait( (cn), (oid), (fg)->timeout ); \
 }
#endif


/* connection DEAD check for SYB_COMMAND_DATA */
#define ISALIVE( x ) is_alive_con( (x)->conn )

#ifdef HAS_THREAD_CRITICAL
#define GUARD { DEFER_INTS; ++rb_thread_critical; }
/* #define UNGUARD { ALLOW_INTS; --rb_thread_critical; } */
#define UNGUARD { ENABLE_INTS; --rb_thread_critical; }
#else
#define GUARD
#define UNGUARD
#endif

/* For ryby1.1 => ruby1.3 */
/* #define Fail(x) rb_raise(rb_eRuntimeError, (x)) */

#ifdef USE_STR2CSTR
#define MY_STR2CSTR STR2CSTR
#define MY_RB_STR2CSTR rb_str2cstr
#else
/* #define MY_STR2CSTR StringValuePtr */
#define MY_STR2CSTR StringValueCStr
static char *my_str2cstr(str, len)
     VALUE str;
     long *len;
{
  StringValue(str);
  if (len) *len = RSTRING_LEN(str); /* str.to_str */
  return RSTRING_PTR(str);
}
#define MY_RB_STR2CSTR my_str2cstr
#endif


static CS_RETCODE  syb_clientmsg_cb(), syb_servermsg_cb();
static CS_RETCODE cmd_or_cursor_cancel() ;

/*
   Start of ruby interface 
   */


#ifdef SYB_DEBUG_NONE
/* MURIYARI callback wo kidousuru kansuu */
static int debug_cbraise(cmd)
     CS_COMMAND *cmd;
{
  CS_INT tm = 0;
  ct_cmd_props(cmd, CS_SET, CS_TIMEOUT, &tm, CS_UNUSED,NULL);
  return 0;
}
#endif

static void *mymalloc(size)
  size_t size;
{
  void *p;
  if( !( p = (void *)malloc( size ) ) ){
#ifdef SYB_DEBUG
    fprintf(stderr,"malloc failed\n");
#endif
    rb_raise(rb_eRuntimeError,"malloc failed");
  }
  return(p);
}
static void myfree(ptr)
  void *ptr;
{
  if( ptr != NULL ){
#ifdef SYB_DEBUG
    *((char *)ptr) = '\0'; 
#endif
    free(ptr);
  }
#ifdef SYB_DEBUG
  fprintf(stderr,"MyFree\n");	/* debug */
#endif
}

/* #define free myfree  RDEBUG */

/* 
   Cut trailing space
   pin will be destroyd!
   */
static void strip_tail( pin )
     char *pin;
{
  char *pend;
  if( (pin == NULL) || ( *pin == '\0')  )return;
  pend = pin + strlen(pin) - 1;
  while( *pend == ' '){
    if( pend == pin ){
      *pin = '\0';
      return;
    }
    --pend;
  }
  *(pend + 1 ) = '\0';
}

#include <time.h>
/*
  raise client message callback for timeout 
 */
CS_RETCODE raise_timeout_cb( context, connection)
     CS_CONTEXT	*context;
     CS_CONNECTION	*connection;	
{
  CS_CLIENTMSG	errdata, *errmsg;
  int len;
  char *msg;

#ifdef SYB_DEBUG
  fprintf(stderr,"raise_timeout_cb, con=%ld, ctx=%ld",(long)context,
	  (long)connection);
#endif
  errmsg = &errdata;
  memset(errmsg, 0, sizeof(CS_CLIENTMSG));
  msg = "io_wait(): TimeOut";
  len = strlen(msg);
  if( len >= CS_MAX_MSG ) len = CS_MAX_MSG - 1 ;
  strncpy(errmsg->msgstring, msg, len);
  errmsg->msgstringlen = len;
#ifndef CS_FIRST_CHUNK
  errmsg->status = 0;
#else 
  errmsg->status = CS_FIRST_CHUNK|CS_LAST_CHUNK;
#endif
  /* severity */
  errmsg->msgnumber = (CS_SV_RETRY_FAIL << 8) & 0x0000ff00;
  return syb_clientmsg_cb(context, connection, errmsg);
}

/*
  IO wait function
  Hontounara compcmd to compid ga kitai siteita monoto onazi ka CHECK subeki.
  demo, douitu setuzoku no douzi yobidasiha nai to kateisi , ATOMAWASI.
 */
static CS_RETCODE io_wait(conn, opid, tmout )
  CS_CONNECTION	*conn;
  CS_INT opid;
  CS_INT tmout;
{
  CS_COMMAND	*compcmd;
  CS_INT compid;
  CS_RETCODE  compstatus;
  CS_RETCODE  retcode;
  CS_CONTEXT *context = NULL;
#define POLLING_INTERVAL 10  /* polling 100 milliseconds */
  time_t timeout = (time_t)0 ;

  /* get parent CS_CONTEXT handle */
  if( ct_con_props( conn, CS_GET, CS_PARENT_HANDLE,
		    &context, CS_UNUSED, NULL) != CS_SUCCEED ){
#ifdef SYB_DEBUG
    fprintf(stderr,"Fail get context in io_wait\n");
#endif
    context = NULL;
  }
  /* 
     Set timeout
  */
  if( tmout > 0 )
    timeout = time(NULL) + (time_t)tmout;
  
#ifdef SYB_DEBUG
  fprintf(stderr,"TIMEOUT to %ld\n", timeout); /* RDEBUG */
#endif 

  for( ; ; ){
#ifdef SYB_DEBUG
    fprintf(stderr, "+");  fflush(stderr);
#endif

    /* 
       Release Ruby-Context-Switch 
    */
    /* rb_thread_schedule(); */
#ifdef  HAS_THREAD_CRITICAL
    if (!rb_thread_critical) rb_thread_schedule();
#else
    rb_thread_schedule();
#endif

    retcode = ct_poll(NULL, conn, POLLING_INTERVAL, NULL, &compcmd,
		      &compid, &compstatus);
    if( (compid != opid) && (compid != 0 ) )
      continue;  /* Another polling  (Shorizumi no baii ,0 ga kuru tokiga aru. */

    if( retcode == CS_SUCCEED ) break;
    if ( (retcode != CS_TIMED_OUT) && (retcode != CS_INTERRUPT) ){
      /*
       It has returned with CS_QUIET !!!
         in case of ct_cancel  process being done already.
        But ct_cancel  returns CS_PENDING  ????
	(See f_cmd_delete's  ct_cancel )
      */
#ifdef SYB_DEBUG
      fprintf(stderr, "\nERROR: ct_poll unexpected retcode [%ld]\n",
	      retcode);
#endif
      break;
    }
#ifdef SYB_DEBUG
    if(retcode == CS_INTERRUPT )
      fprintf(stderr,"CS_INTERRUPT!\n");
#endif
    /* timeout check */
    if( (timeout > (time_t)0) && (timeout < time(NULL)) ){
      /* fprintf(stderr,"-- IO TIMEOUT --\n");  T debug */
      if( context == NULL ) return CS_FAIL;
      if( raise_timeout_cb(context,conn) != CS_SUCCEED )
	return CS_FAIL;
    }
  }
  
#ifdef SYB_DEBUG
  fprintf(stderr,
	      "\nEND ct_poll retcode=%ld, compid=%ld,compstatus=%ld\n",
	  retcode,compid, compstatus);
  fflush(stderr);
#endif  
  return compstatus;
}


/*
 Generate Property and Constants
 */
static void defconst_props(cls)
     VALUE cls;
{
  /* Sets CS_XXXX property constants */
  /* rb_define_const(cls, "CS_ALLOC_FUNC", INT2NUM(CS_ANSI_BINDS)); */
  rb_define_const(cls, "CS_ANSI_BINDS", INT2NUM(CS_ANSI_BINDS));
  rb_define_const(cls, "CS_APPNAME", INT2NUM(CS_APPNAME));
#ifdef CS_ASYNC_NOTIFS
  rb_define_const(cls, "CS_ASYNC_NOTIFS", INT2NUM(CS_ASYNC_NOTIFS));
#endif
  rb_define_const(cls, "CS_BULK_LOGIN", INT2NUM(CS_BULK_LOGIN));
  rb_define_const(cls, "CS_CHARSETCNV", INT2NUM(CS_CHARSETCNV));
  /* rb_define_const(cls, "CS_COMMBLOCK", INT2NUM(CS_COMMBLOCK)); */
  rb_define_const(cls, "CS_CON_STATUS", INT2NUM(CS_CON_STATUS));
  rb_define_const(cls, "CS_CUR_ID", INT2NUM(CS_CUR_ID));
  rb_define_const(cls, "CS_CUR_NAME", INT2NUM(CS_CUR_NAME));
  rb_define_const(cls, "CS_CUR_ROWCOUNT", INT2NUM(CS_CUR_ROWCOUNT));
  rb_define_const(cls, "CS_CUR_STATUS", INT2NUM(CS_CUR_STATUS));
  /* rb_define_const(cls, "CS_DIAG_TIMEOUT", INT2NUM(CS_DIAG_TIMEOUT)); */
  rb_define_const(cls, "CS_DISABLE_POLL", INT2NUM(CS_DISABLE_POLL));
  /* rb_define_const(cls, "CS_EED_CMD", INT2NUM(CS_EED_CMD)); */
#ifdef CS_ENDPOINT
  rb_define_const(cls, "CS_ENDPOINT", INT2NUM(CS_ENDPOINT));
#endif
  rb_define_const(cls, "CS_EXPOSE_FMTS", INT2NUM(CS_EXPOSE_FMTS));
  /* rb_define_const(cls, "CS_EXTRA_INF", INT2NUM(CS_EXTRA_INF)); */
  rb_define_const(cls, "CS_HIDDEN_KEYS", INT2NUM(CS_HIDDEN_KEYS));
  rb_define_const(cls, "CS_HOSTNAME", INT2NUM(CS_HOSTNAME));
  rb_define_const(cls, "CS_IFILE", INT2NUM(CS_IFILE));
  /* rb_define_const(cls, "CS_LOC_PROP", INT2NUM(CS_LOC_PROP)); */
  rb_define_const(cls, "CS_LOGIN_STATUS", INT2NUM(CS_LOGIN_STATUS));
  rb_define_const(cls, "CS_LOGIN_TIMEOUT", INT2NUM(CS_LOGIN_TIMEOUT));
  rb_define_const(cls, "CS_MAX_CONNECT", INT2NUM(CS_MAX_CONNECT));
  /* rb_define_const(cls, "CS_MEM_POOL", INT2NUM(CS_MEM_POOL)); */
  rb_define_const(cls, "CS_NETIO", INT2NUM(CS_NETIO));
#ifdef CS_NO_TRUNCATE
  rb_define_const(cls, "CS_NO_TRUNCATE", INT2NUM(CS_NO_TRUNCATE));
#endif
#ifdef CS_NOINTERRUPT
  rb_define_const(cls, "CS_NOINTERRUPT", INT2NUM(CS_NOINTERRUPT));
#endif
  rb_define_const(cls, "CS_PACKETSIZE", INT2NUM(CS_PACKETSIZE));
  /* rb_define_const(cls, "CS_PARENT_HANDLE", INT2NUM(CS_PARENT_HANDLE)); */
  rb_define_const(cls, "CS_PASSWORD", INT2NUM(CS_PASSWORD));
  rb_define_const(cls, "CS_SEC_APPDEFINED", INT2NUM(CS_SEC_APPDEFINED));
  rb_define_const(cls, "CS_SEC_CHALLENGE", INT2NUM(CS_SEC_CHALLENGE));
  rb_define_const(cls, "CS_SEC_ENCRYPTION", INT2NUM(CS_SEC_ENCRYPTION));
  rb_define_const(cls, "CS_SEC_NEGOTIATE", INT2NUM(CS_SEC_NEGOTIATE));
  rb_define_const(cls, "CS_SERVERNAME", INT2NUM(CS_SERVERNAME));
  rb_define_const(cls, "CS_TDS_VERSION", INT2NUM(CS_TDS_VERSION));
  rb_define_const(cls, "CS_TEXTLIMIT", INT2NUM(CS_TEXTLIMIT));
  rb_define_const(cls, "CS_TIMEOUT", INT2NUM(CS_TIMEOUT));
#ifdef CS_TRANSACTION_NAME
  rb_define_const(cls, "CS_TRANSACTION_NAME", INT2NUM(CS_TRANSACTION_NAME));
#endif
  rb_define_const(cls, "CS_USERDATA", INT2NUM(CS_USERDATA));
  rb_define_const(cls, "CS_USERNAME", INT2NUM(CS_USERNAME));
  /* rb_define_const(cls, "CS_USER_ALLOC", INT2NUM(CS_USER_ALLOC)); */
  /* rb_define_const(cls, "CS_USER_FREE", INT2NUM(CS_USER_FREE)); */
  rb_define_const(cls, "CS_VERSION", INT2NUM(CS_VERSION));
  rb_define_const(cls, "CS_VER_STRING", INT2NUM(CS_VER_STRING));
}

/* 
   define result_type constants ( see P 3-209) 
 */
static void defconst_restypes(cls)
     VALUE cls;
{
  /* define result_type constants ( see P 3-209) */
  rb_define_const(cls, "CS_CMD_SUCCEED", INT2NUM(CS_CMD_SUCCEED));
  rb_define_const(cls, "CS_CMD_DONE", INT2NUM(CS_CMD_DONE));
  rb_define_const(cls, "CS_CMD_FAIL", INT2NUM(CS_CMD_FAIL));
  rb_define_const(cls, "CS_ROW_RESULT", INT2NUM(CS_ROW_RESULT));
  rb_define_const(cls, "CS_CURSOR_RESULT", INT2NUM(CS_CURSOR_RESULT));
  rb_define_const(cls, "CS_PARAM_RESULT", INT2NUM(CS_PARAM_RESULT));
  rb_define_const(cls, "CS_STATUS_RESULT", INT2NUM(CS_STATUS_RESULT));
  rb_define_const(cls, "CS_COMPUTE_RESULT", INT2NUM(CS_COMPUTE_RESULT));
  rb_define_const(cls, "CS_COMPUTEFMT_RESULT", INT2NUM(CS_COMPUTEFMT_RESULT));
  rb_define_const(cls, "CS_ROWFMT_RESULT", INT2NUM(CS_ROWFMT_RESULT));
  rb_define_const(cls, "CS_MSG_RESULT", INT2NUM(CS_MSG_RESULT));
  rb_define_const(cls, "CS_DESCRIBE_RESULT", INT2NUM(CS_DESCRIBE_RESULT));
  /* define ct_cancel's type (See p3-27) */
  rb_define_const(cls, "CS_CANCEL_ALL", INT2NUM(CS_CANCEL_ALL));
  rb_define_const(cls, "CS_CANCEL_ATTN", INT2NUM(CS_CANCEL_ATTN));
  rb_define_const(cls, "CS_CANCEL_CURRENT", INT2NUM(CS_CANCEL_CURRENT));
}
/*
  Sets constants for the server option.
 */
static void defconst_options(cls)
     VALUE cls;
{
  rb_define_const(cls, "CS_OPT_ANSINULL", INT2NUM(CS_OPT_ANSINULL));
  rb_define_const(cls, "CS_OPT_ANSIPERM", INT2NUM(CS_OPT_ANSIPERM));
  rb_define_const(cls, "CS_OPT_ARITHABORT", INT2NUM(CS_OPT_ARITHABORT));
  rb_define_const(cls, "CS_OPT_ARITHIGNORE", INT2NUM(CS_OPT_ARITHIGNORE));
  rb_define_const(cls, "CS_OPT_AUTHOFF", INT2NUM(CS_OPT_AUTHOFF));
  rb_define_const(cls, "CS_OPT_AUTHON", INT2NUM(CS_OPT_AUTHON));
  rb_define_const(cls, "CS_OPT_CHAINXACTS", INT2NUM(CS_OPT_CHAINXACTS));
#ifdef CS_OPT_CHARSET
  rb_define_const(cls, "CS_OPT_CHARSET", INT2NUM(CS_OPT_CHARSET));
#endif
  rb_define_const(cls, "CS_OPT_CURCLOSEONXACT", INT2NUM(CS_OPT_CURCLOSEONXACT));
  rb_define_const(cls, "CS_OPT_CURREAD", INT2NUM(CS_OPT_CURREAD));
  rb_define_const(cls, "CS_OPT_CURWRITE", INT2NUM(CS_OPT_CURWRITE));
  rb_define_const(cls, "CS_OPT_DATEFIRST", INT2NUM(CS_OPT_DATEFIRST));
  rb_define_const(cls, "CS_OPT_DATEFORMAT", INT2NUM(CS_OPT_DATEFORMAT));
  rb_define_const(cls, "CS_OPT_FIPSFLAG", INT2NUM(CS_OPT_FIPSFLAG));
  rb_define_const(cls, "CS_OPT_FORCEPLAN", INT2NUM(CS_OPT_FORCEPLAN));
  rb_define_const(cls, "CS_OPT_FORMATONLY", INT2NUM(CS_OPT_FORMATONLY));
  rb_define_const(cls, "CS_OPT_GETDATA", INT2NUM(CS_OPT_GETDATA));
  rb_define_const(cls, "CS_OPT_IDENTITYOFF", INT2NUM(CS_OPT_IDENTITYOFF));
  rb_define_const(cls, "CS_OPT_IDENTITYON", INT2NUM(CS_OPT_IDENTITYON));
  rb_define_const(cls, "CS_OPT_ISOLATION", INT2NUM(CS_OPT_ISOLATION));
#ifdef CS_OPT_NATLANG
  rb_define_const(cls, "CS_OPT_NATLANG", INT2NUM(CS_OPT_NATLANG));
#endif
  rb_define_const(cls, "CS_OPT_NOCOUNT", INT2NUM(CS_OPT_NOCOUNT));
  rb_define_const(cls, "CS_OPT_NOEXEC", INT2NUM(CS_OPT_NOEXEC));
  rb_define_const(cls, "CS_OPT_PARSEONLY", INT2NUM(CS_OPT_PARSEONLY));
  rb_define_const(cls, "CS_OPT_QUOTED_IDENT", INT2NUM(CS_OPT_QUOTED_IDENT));
  rb_define_const(cls, "CS_OPT_RESTREES", INT2NUM(CS_OPT_RESTREES));
  rb_define_const(cls, "CS_OPT_ROWCOUNT", INT2NUM(CS_OPT_ROWCOUNT));
  rb_define_const(cls, "CS_OPT_SHOWPLAN", INT2NUM(CS_OPT_SHOWPLAN));
  rb_define_const(cls, "CS_OPT_STATS_IO", INT2NUM(CS_OPT_STATS_IO));
  rb_define_const(cls, "CS_OPT_STATS_TIME", INT2NUM(CS_OPT_STATS_TIME));
  rb_define_const(cls, "CS_OPT_STR_RTRUNC", INT2NUM(CS_OPT_STR_RTRUNC));
  rb_define_const(cls, "CS_OPT_TEXTSIZE", INT2NUM(CS_OPT_TEXTSIZE));
  rb_define_const(cls, "CS_OPT_TRUNCIGNORE", INT2NUM(CS_OPT_TRUNCIGNORE));

  /* CS_OPT_DATEFIRST value */
  rb_define_const(cls, "CS_OPT_MONDAY", INT2NUM(CS_OPT_MONDAY));
  rb_define_const(cls, "CS_OPT_TUESDAY", INT2NUM(CS_OPT_TUESDAY));
  rb_define_const(cls, "CS_OPT_WEDNESDAY", INT2NUM(CS_OPT_WEDNESDAY));
  rb_define_const(cls, "CS_OPT_THURSDAY", INT2NUM(CS_OPT_THURSDAY));
  rb_define_const(cls, "CS_OPT_FRIDAY", INT2NUM(CS_OPT_FRIDAY));
  rb_define_const(cls, "CS_OPT_SATURDAY", INT2NUM(CS_OPT_SATURDAY));
  rb_define_const(cls, "CS_OPT_SUNDAY", INT2NUM(CS_OPT_SUNDAY));
  /* CS_OPT_DATEFORMAT  values */
  rb_define_const(cls, "CS_OPT_FMTMDY", INT2NUM(CS_OPT_FMTMDY));
  rb_define_const(cls, "CS_OPT_FMTDMY", INT2NUM(CS_OPT_FMTDMY));
  rb_define_const(cls, "CS_OPT_FMTYMD", INT2NUM(CS_OPT_FMTYMD));
  rb_define_const(cls, "CS_OPT_FMTYDM", INT2NUM(CS_OPT_FMTYDM));
  rb_define_const(cls, "CS_OPT_FMTMYD", INT2NUM(CS_OPT_FMTMYD));
  rb_define_const(cls, "CS_OPT_FMTDYM", INT2NUM(CS_OPT_FMTDYM));
  /* CS_OPT_ISOLATION values*/
  rb_define_const(cls, "CS_OPT_LEVEL1", INT2NUM(CS_OPT_LEVEL1));
  rb_define_const(cls, "CS_OPT_LEVEL3", INT2NUM(CS_OPT_LEVEL3));
}

/**  define column type constants  */
static void defconst_coltype(cls)
  VALUE cls;
{
#ifdef CS_ILLEGAL_TYPE
  rb_define_const(cls, "CS_ILLEGAL_TYPE", INT2NUM(CS_ILLEGAL_TYPE));
#endif
#ifdef CS_CHAR_TYPE
  rb_define_const(cls, "CS_CHAR_TYPE", INT2NUM(CS_CHAR_TYPE));
#endif
#ifdef CS_BINARY_TYPE
  rb_define_const(cls, "CS_BINARY_TYPE", INT2NUM(CS_BINARY_TYPE));
#endif
#ifdef CS_LONGCHAR_TYPE
  rb_define_const(cls, "CS_LONGCHAR_TYPE", INT2NUM(CS_LONGCHAR_TYPE));
#endif
#ifdef CS_LONGBINARY_TYPE
  rb_define_const(cls, "CS_LONGBINARY_TYPE", INT2NUM(CS_LONGBINARY_TYPE));
#endif
#ifdef CS_TINYINT_TYPE
  rb_define_const(cls, "CS_TINYINT_TYPE", INT2NUM(CS_TINYINT_TYPE));
#endif
#ifdef CS_SMALLINT_TYPE
  rb_define_const(cls, "CS_SMALLINT_TYPE", INT2NUM(CS_SMALLINT_TYPE));
#endif
#ifdef CS_INT_TYPE
  rb_define_const(cls, "CS_INT_TYPE", INT2NUM(CS_INT_TYPE));
#endif
#ifdef CS_REAL_TYPE
  rb_define_const(cls, "CS_REAL_TYPE", INT2NUM(CS_REAL_TYPE));
#endif
#ifdef CS_FLOAT_TYPE
  rb_define_const(cls, "CS_FLOAT_TYPE", INT2NUM(CS_FLOAT_TYPE));
#endif
#ifdef CS_BIT_TYPE
  rb_define_const(cls, "CS_BIT_TYPE", INT2NUM(CS_BIT_TYPE));
#endif
#ifdef CS_DATETIME_TYPE
  rb_define_const(cls, "CS_DATETIME_TYPE", INT2NUM(CS_DATETIME_TYPE));
#endif
#ifdef CS_DATETIME4_TYPE
  rb_define_const(cls, "CS_DATETIME4_TYPE", INT2NUM(CS_DATETIME4_TYPE));
#endif
#ifdef CS_MONEY_TYPE
  rb_define_const(cls, "CS_MONEY_TYPE", INT2NUM(CS_MONEY_TYPE));
#endif
#ifdef CS_MONEY4_TYPE
  rb_define_const(cls, "CS_MONEY4_TYPE", INT2NUM(CS_MONEY4_TYPE));
#endif
#ifdef CS_NUMERIC_TYPE
  rb_define_const(cls, "CS_NUMERIC_TYPE", INT2NUM(CS_NUMERIC_TYPE));
#endif
#ifdef CS_DECIMAL_TYPE
  rb_define_const(cls, "CS_DECIMAL_TYPE", INT2NUM(CS_DECIMAL_TYPE));
#endif
#ifdef CS_VARCHAR_TYPE
  rb_define_const(cls, "CS_VARCHAR_TYPE", INT2NUM(CS_VARCHAR_TYPE));
#endif
#ifdef CS_VARBINARY_TYPE
  rb_define_const(cls, "CS_VARBINARY_TYPE", INT2NUM(CS_VARBINARY_TYPE));
#endif
#ifdef CS_LONG_TYPE
  rb_define_const(cls, "CS_LONG_TYPE", INT2NUM(CS_LONG_TYPE));
#endif
#ifdef CS_SENSITIVITY_TYPE
  rb_define_const(cls, "CS_SENSITIVITY_TYPE", INT2NUM(CS_SENSITIVITY_TYPE));
#endif
#ifdef CS_BOUNDARY_TYPE
  rb_define_const(cls, "CS_BOUNDARY_TYPE", INT2NUM(CS_BOUNDARY_TYPE));
#endif
#ifdef CS_VOID_TYPE
  rb_define_const(cls, "CS_VOID_TYPE", INT2NUM(CS_VOID_TYPE));
#endif
#ifdef CS_USHORT_TYPE
  rb_define_const(cls, "CS_USHORT_TYPE", INT2NUM(CS_USHORT_TYPE));
#endif
#ifdef CS_UNICHAR_TYPE
  rb_define_const(cls, "CS_UNICHAR_TYPE", INT2NUM(CS_UNICHAR_TYPE));
#endif
#ifdef CS_BLOB_TYPE
  rb_define_const(cls, "CS_BLOB_TYPE", INT2NUM(CS_BLOB_TYPE));
#endif
#ifdef CS_DATE_TYPE
  rb_define_const(cls, "CS_DATE_TYPE", INT2NUM(CS_DATE_TYPE));
#endif
#ifdef CS_TIME_TYPE
  rb_define_const(cls, "CS_TIME_TYPE", INT2NUM(CS_TIME_TYPE));
#endif
#ifdef CS_UNITEXT_TYPE
  rb_define_const(cls, "CS_UNITEXT_TYPE", INT2NUM(CS_UNITEXT_TYPE));
#endif
#ifdef CS_BIGINT_TYPE
  rb_define_const(cls, "CS_BIGINT_TYPE", INT2NUM(CS_BIGINT_TYPE));
#endif
#ifdef CS_USMALLINT_TYPE
  rb_define_const(cls, "CS_USMALLINT_TYPE", INT2NUM(CS_USMALLINT_TYPE));
#endif
#ifdef CS_UINT_TYPE
  rb_define_const(cls, "CS_UINT_TYPE", INT2NUM(CS_UINT_TYPE));
#endif
#ifdef CS_UBIGINT_TYPE
  rb_define_const(cls, "CS_UBIGINT_TYPE", INT2NUM(CS_UBIGINT_TYPE));
#endif
#ifdef CS_XML_TYPE
  rb_define_const(cls, "CS_XML_TYPE", INT2NUM(CS_XML_TYPE));
#endif
#ifdef CS_UNIQUE_TYPE
  rb_define_const(cls, "CS_UNIQUE_TYPE", INT2NUM(CS_UNIQUE_TYPE));
#endif
#ifdef CS_USER_TYPE
  rb_define_const(cls, "CS_USER_TYPE", INT2NUM(CS_USER_TYPE));
#endif
}

/* Generate other constants  */
static void defconst_etc(cls)
     VALUE cls;
{
  rb_define_const(cls, "CS_VERSION_100", INT2NUM(CS_VERSION_100));
  /*  basic default types. */
  rb_define_const(cls, "CS_NULLTERM", INT2NUM(CS_NULLTERM));
  rb_define_const(cls, "CS_WILDCARD", INT2NUM(CS_WILDCARD));
  rb_define_const(cls, "CS_NO_LIMIT", INT2NUM(CS_NO_LIMIT));
  rb_define_const(cls, "CS_UNUSED", INT2NUM(CS_UNUSED));

  /* ct_command constants */
  rb_define_const(cls, "CS_LANG_CMD", INT2NUM(CS_LANG_CMD));
  rb_define_const(cls, "CS_RPC_CMD", INT2NUM(CS_RPC_CMD));
  rb_define_const(cls, "CS_SEND_DATA_CMD", INT2NUM(CS_SEND_DATA_CMD));
#ifdef CS_PACKAGE_CMD
  rb_define_const(cls, "CS_PACKAGE_CMD", INT2NUM(CS_PACKAGE_CMD));
#endif

  rb_define_const(cls, "CS_RECOMPILE", INT2NUM(CS_RECOMPILE));
  rb_define_const(cls, "CS_NO_RECOMPILE", INT2NUM(CS_NO_RECOMPILE));
  rb_define_const(cls, "CS_COLUMN_DATA", INT2NUM(CS_COLUMN_DATA));
  rb_define_const(cls, "CS_BULK_DATA", INT2NUM(CS_BULK_DATA));
  
  /* Constants for ct_res_info() */
  rb_define_const(cls, "CS_ROW_COUNT", INT2NUM(CS_ROW_COUNT));
  rb_define_const(cls, "CS_CMD_NUMBER", INT2NUM(CS_CMD_NUMBER));
  rb_define_const(cls, "CS_NUM_COMPUTES", INT2NUM(CS_NUM_COMPUTES));
  rb_define_const(cls, "CS_NUMDATA", INT2NUM(CS_NUMDATA));
#ifdef CS_ORDERBY_COLS
  rb_define_const(cls, "CS_ORDERBY_COLS", INT2NUM(CS_ORDERBY_COLS));
#endif
  rb_define_const(cls, "CS_NUMORDERCOLS", INT2NUM(CS_NUMORDERCOLS));
  rb_define_const(cls, "CS_MSGTYPE", INT2NUM(CS_MSGTYPE));
  rb_define_const(cls, "CS_BROWSE_INFO", INT2NUM(CS_BROWSE_INFO));
  rb_define_const(cls, "CS_TRANS_STATE", INT2NUM(CS_TRANS_STATE));
  
  /*
    CS_TRANS_STATE
    */
  /* tran undefined */
  rb_define_const(cls, "CS_TRAN_UNDEFINED", INT2NUM(CS_TRAN_UNDEFINED));
  /* A transaction is progress. */
  rb_define_const(cls, "CS_TRAN_IN_PROGRESS", INT2NUM(CS_TRAN_IN_PROGRESS));
  /* Transaction completed successfully */
  rb_define_const(cls, "CS_TRAN_COMPLETED", INT2NUM(CS_TRAN_COMPLETED));
  /* Transaction failed */
  rb_define_const(cls, "CS_TRAN_FAIL", INT2NUM(CS_TRAN_FAIL));
  /* Transaction failed (in the last executing command) */
  rb_define_const(cls, "CS_TRAN_STMT_FAIL", INT2NUM(CS_TRAN_STMT_FAIL));

  /* for ct_fetch */
  rb_define_const(cls, "CS_END_DATA", INT2NUM(CS_END_DATA));  /* not use */
  rb_define_const(cls, "CS_SUCCEED", INT2NUM(CS_SUCCEED));  /* not use */
  rb_define_const(cls, "CS_FAIL", INT2NUM(CS_FAIL));  /* not use */
  rb_define_const(cls, "CS_ROW_FAIL", INT2NUM(CS_ROW_FAIL));
  rb_define_const(cls, "CS_CANCELED", INT2NUM(CS_CANCELED));
  rb_define_const(cls, "CS_PENDING", INT2NUM(CS_PENDING));
#ifdef CS_BUSY
  rb_define_const(cls, "CS_BUSY", INT2NUM(CS_BUSY));
#endif

  /* for ct_get_data() */
  rb_define_const(cls, "CS_END_ITEM", INT2NUM(CS_END_ITEM));

  /* for IODESC */
  rb_define_const(cls, "CS_IMAGE_TYPE", INT2NUM(CS_IMAGE_TYPE));
  rb_define_const(cls, "CS_TEXT_TYPE", INT2NUM(CS_TEXT_TYPE));

  /* Declares  constants for ct_cursor()  */
  rb_define_const(cls, "CS_CURSOR_DECLARE", INT2NUM(CS_CURSOR_DECLARE ));
  rb_define_const(cls, "CS_CURSOR_ROWS", INT2NUM(CS_CURSOR_ROWS ));
  rb_define_const(cls, "CS_CURSOR_OPEN", INT2NUM(CS_CURSOR_OPEN ));
  rb_define_const(cls, "CS_CURSOR_CLOSE", INT2NUM(CS_CURSOR_CLOSE ));
  rb_define_const(cls, "CS_CURSOR_OPTION", INT2NUM(CS_CURSOR_OPTION ));
  rb_define_const(cls, "CS_CURSOR_UPDATE", INT2NUM(CS_CURSOR_UPDATE ));
  rb_define_const(cls, "CS_CURSOR_DELETE", INT2NUM(CS_CURSOR_DELETE ));
  rb_define_const(cls, "CS_CURSOR_DEALLOC", INT2NUM(CS_CURSOR_DEALLOC ));

  /* Options for ct_cursor() */
#ifdef CS_MORE
  rb_define_const(cls, "CS_MORE", INT2NUM( CS_MORE ));
#endif
#ifdef CS_END
  rb_define_const(cls, "CS_END", INT2NUM( CS_END ));
#endif
  rb_define_const(cls, "CS_FOR_UPDATE", INT2NUM( CS_FOR_UPDATE ));
  rb_define_const(cls, "CS_READ_ONLY", INT2NUM( CS_READ_ONLY ));
#ifdef CS_RESTORE_OPTION
  rb_define_const(cls, "CS_RESTORE_OPTION", INT2NUM( CS_RESTORE_OPTION ));
#endif

  /* Declares bit mask constants for ct_cmd_props (CS_CUR_STATUS )  */
  rb_define_const(cls, "CS_CURSTAT_NONE", INT2NUM(CS_CURSTAT_NONE ));
  rb_define_const(cls, "CS_CURSTAT_CLOSED", INT2NUM(CS_CURSTAT_CLOSED ));  
  rb_define_const(cls, "CS_CURSTAT_DECLARED", INT2NUM(CS_CURSTAT_DECLARED ));
  rb_define_const(cls, "CS_CURSTAT_ROWCOUNT", INT2NUM(CS_CURSTAT_ROWCOUNT ));
  rb_define_const(cls, "CS_CURSTAT_OPEN", INT2NUM(CS_CURSTAT_OPEN ));
  rb_define_const(cls, "CS_CURSTAT_RDONLY", INT2NUM(CS_CURSTAT_RDONLY ));
  rb_define_const(cls, "CS_CURSTAT_UPDATABLE", INT2NUM(CS_CURSTAT_UPDATABLE ));

  rb_define_const(cls, "CS_CONSTAT_CONNECTED", INT2NUM(CS_CONSTAT_CONNECTED ));
  rb_define_const(cls, "CS_CONSTAT_DEAD", INT2NUM(CS_CONSTAT_DEAD ));

  rb_define_const(cls, "SYB_CTLIB_VERSION", INT2NUM(SYB_CTLIB_VERSION));
#ifdef FREETDS
  rb_define_const(cls, "IS_FREETDS", Qtrue);
#else
  rb_define_const(cls, "IS_FREETDS", Qfalse);
#endif

  /* for dbi */
#ifdef CS_SERVERADDR
  rb_define_const(cls, "CS_SERVERADDR", INT2NUM(CS_SERVERADDR));
#endif
#ifdef CS_PORT
  rb_define_const(cls, "CS_PORT", INT2NUM(CS_PORT));
#endif

}

/* 
   Retrieves buffer type when it access property.
   return
	'B'	Boolean value
	'S'	String value
	'I'	Integer value
	'?'	Special
	'U'	Un support
   */
static char props_buffer_type(  prop)
     CS_INT prop;
{
  switch(prop){
  case CS_ANSI_BINDS :
#ifdef CS_ASYNC_NOTIFS
  case CS_ASYNC_NOTIFS :
#endif
  case CS_BULK_LOGIN :
  case CS_CHARSETCNV :
  /* case CS_DIAG_TIMEOUT : */
  case CS_DISABLE_POLL :
  case CS_EXPOSE_FMTS :
  /* case CS_EXTRA_INF : */
  case CS_HIDDEN_KEYS :
  case CS_LOGIN_STATUS :
#ifdef CS_NO_TRUNCATE
  case CS_NO_TRUNCATE :
#endif
#ifdef CS_NOINTERRUPT
  case CS_NOINTERRUPT:
#endif
  case CS_SEC_APPDEFINED :
  case CS_SEC_CHALLENGE :
  case CS_SEC_ENCRYPTION :
  case CS_SEC_NEGOTIATE :
    /* boolean type */
    return 'B';
  case CS_APPNAME :
  case CS_CUR_NAME :
  case CS_HOSTNAME :
  case CS_IFILE :
  case CS_PASSWORD :
  case CS_SERVERNAME :
#ifdef CS_TRANSACTION_NAME
  case CS_TRANSACTION_NAME :
#endif
  case CS_USERNAME :
  case CS_VER_STRING :
#ifdef CS_SERVERADDR
  case CS_SERVERADDR : /* DBI patch by Charles */
#endif
    /* string type */
    return 'S';
  case CS_CON_STATUS :
  case CS_CUR_ID :
  case CS_CUR_ROWCOUNT :
  case CS_CUR_STATUS :
#ifdef CS_ENDPOINT
  case CS_ENDPOINT :
#endif
  case CS_LOGIN_TIMEOUT :
  case CS_MAX_CONNECT :
  case CS_PACKETSIZE :
  case CS_TEXTLIMIT : /* -1 */
  case CS_TIMEOUT :  /* -1 */
  case CS_VERSION :
  case CS_TDS_VERSION :
#ifdef CS_PORT 
  case CS_PORT :  /* DBI patch by Charles */
#endif
    /* integer */
    return 'I';
  case CS_NETIO :	/* */
  /* case CS_USERDATA : */
    /* kobetu */
    return '?';

    /* case CS_COMMBLOCK : */
    /* case CS_EED_CMD : */
    /* case CS_LOC_PROP : */
    /* case CS_MEM_POOL : */
    /* case CS_PARENT_HANDLE : */

    /* case CS_USER_ALLOC : */
    /* case CS_USER_FREE : */
  default:
    break;
  }
  return 'U';
}

/* 
   Retrieves property
   nil return -- unsupport or ERROR
   */
static VALUE get_props( level, prop, applyfoo)
     void *level;	/* context, connection, or command */
     CS_RETCODE (*applyfoo)();
     CS_INT prop;
{
  VALUE ret = Qnil;
  char type;
  CS_RETCODE csret;
  CS_INT outlen = 0;  /* On freetds, must set 0  */

  type = props_buffer_type(prop);
  if( type == 'B' ){
    /* boolean type */
    CS_BOOL boo;
    csret = 
      (*applyfoo)(level, CS_GET, prop, &boo, CS_UNUSED, &outlen);
    if( (csret == CS_SUCCEED ) || (outlen > 0 ) ){
      if( boo == CS_TRUE ) ret = Qtrue;
      else if ( boo == CS_FALSE) ret = Qfalse;
    }
  }
  else if( type == 'S' ) {
    /* string type */
    char buf[1023 + 1];  /* +1 -- for peace of mind */
    char *pbuf;
    pbuf = buf;
    do {	/* Block that does not iterates  */
      csret = 
	(*applyfoo)(level, CS_GET, prop, pbuf, 1023, &outlen);
      if( csret != CS_SUCCEED ){
	int len ;
	if( outlen <= 1023)
	  break;
	/* when lack of length */
	len = outlen;
	pbuf = mymalloc(len + 1 ); /* +1 -- for peace of mind */
	csret = 
	  (*applyfoo)(level, CS_GET, prop,pbuf, len,  &outlen);
	if( (csret != CS_SUCCEED ) || (outlen < 0 ) )
	  break;
	if( outlen > len) outlen = len;
      }
      if(outlen < 0 )break;
      if( outlen > 0 ){
	/* outlen --  NULL madeno nagasa? */
	if( *(pbuf + outlen - 1) == '\0' )
	  --outlen;
      }
      ret = rb_str_new(pbuf, outlen);
    } while(0);
    if( pbuf != buf ) free(pbuf);
  }

  else if( type == 'I' ){
    /* integer */
    CS_INT val;
    csret = 
      (*applyfoo)(level, CS_GET, prop, &val, CS_UNUSED, &outlen);
    if( (csret == CS_SUCCEED ) || (outlen > 0 ) ){
      ret = INT2NUM( val );
    }
  }
  return ret;
}

/* 
   Sets property
   CS_SUCCEED return --- no unsupport and no ERROR
   Checked data type
   */
static CS_RETCODE set_props( level, prop, val, applyfoo)
     void *level;	/* context, connection, or command */
     VALUE val;
     CS_RETCODE (*applyfoo)();
     CS_INT prop;
{
  char type;
  CS_RETCODE csret = CS_FAIL;

  type = props_buffer_type(prop);
  if( type == 'B' ){
    /* boolean type */
    CS_BOOL boo;
    /* Sets CS_TRUE if it is not false or nil */
    if( (val == Qfalse ) || (val == Qnil) )
      boo = CS_FALSE;
    else
      boo = CS_TRUE;

    csret = 
      (*applyfoo)(level, CS_SET, prop, &boo, CS_UNUSED, NULL);
  }
  else if( type == 'S' ) {
    /* string type */
    char *pbuf;
    if( TYPE(val) == T_STRING ){
      pbuf = (CS_CHAR *)(MY_STR2CSTR(val) );
      if( pbuf == NULL ) pbuf = "";
      csret = 
	(*applyfoo)(level, CS_SET, prop, pbuf, CS_NULLTERM, NULL);
    }
  }
  else if( type == 'I' ){
    /* integer */
    CS_INT ival;
    if( (TYPE(val) == T_FIXNUM) ||
	(TYPE(val) == T_BIGNUM) || (TYPE(val) == T_FLOAT) ){
      ival = (CS_INT)( NUM2INT(val) );
      csret = 
	(*applyfoo)(level, CS_SET, prop, &ival, CS_UNUSED, NULL);
    }
  }
  return csret;
}
/* 
   Retrieves buffer type when access server option
   return
	'B'	Boolean value
	'S'	String value
	'I'	Integer value
	'C'	Symbolic value
	'U'	unsupported
   */
static char options_buffer_type(opt)
     CS_INT opt;
{
  switch(opt){
  case CS_OPT_ANSINULL:
  case CS_OPT_ANSIPERM:
  case CS_OPT_ARITHABORT:
  case CS_OPT_ARITHIGNORE:
  case CS_OPT_CHAINXACTS:
  case CS_OPT_CURCLOSEONXACT:
  case CS_OPT_FIPSFLAG:
  case CS_OPT_FORCEPLAN:
  case CS_OPT_FORMATONLY:
  case CS_OPT_GETDATA:
  case CS_OPT_NOCOUNT:
  case CS_OPT_NOEXEC:
  case CS_OPT_PARSEONLY:
  case CS_OPT_QUOTED_IDENT:
  case CS_OPT_RESTREES:
  case CS_OPT_SHOWPLAN:
  case CS_OPT_STATS_IO:
  case CS_OPT_STATS_TIME:
  case CS_OPT_STR_RTRUNC:
  case CS_OPT_TRUNCIGNORE:
    /* boolean */
    return 'B';
  case CS_OPT_AUTHOFF:	/* cannot CS_GET */
  case CS_OPT_AUTHON:	/* cannot CS_GET */
#ifdef CS_OPT_CHARSET
  case CS_OPT_CHARSET:	/* Character set (cannot CS_GET) */
#endif
  case CS_OPT_CURREAD:	/* cannot CS_GET */
  case CS_OPT_CURWRITE:	/* cannot CS_GET */
  case CS_OPT_IDENTITYOFF:
  case CS_OPT_IDENTITYON:
#ifdef CS_OPT_NATLANG
  case CS_OPT_NATLANG:	/* National Lang */
#endif
    return 'S';
  case CS_OPT_DATEFIRST:
  case CS_OPT_DATEFORMAT:
  case CS_OPT_ISOLATION:
    return 'C';
  case CS_OPT_ROWCOUNT:
  case CS_OPT_TEXTSIZE:
    return 'I';
  default:
    break;
  }
  return 'U';
}

static void ctx_free( ctxdata )
     SYB_CONTEXT_DATA *ctxdata;
{
#ifdef USE_MEMPOOL
  if( ctxdata->mempool ){
    free(ctxdata->mempool);
    ctxdata->mempool = NULL;
  }
#endif
  if( ctxdata->val != NULL ){
    ct_exit( ctxdata->val, CS_FORCE_EXIT);
    cs_ctx_drop(ctxdata->val);
  }
  ctxdata->val = NULL;
#ifdef SYB_DEBUG
  fprintf(stderr,"Free ctxdata\n");	/* debug */
#endif
  free( ctxdata);
}

#ifdef USE_MY_DATE2STR
static CS_RETCODE myconv_date2str( ctx, sfmt, src,
				   dfmt, dest, destlen)
     CS_CONTEXT *ctx;
     CS_DATAFMT *sfmt, *dfmt;
     CS_VOID *src, *dest;
     CS_INT *destlen;
{
  CS_DATEREC drec;
  CS_RETCODE ret = CS_SUCCEED;
  char tmpdate[64];
  int len;

  memset(tmpdate, 0, sizeof(tmpdate));
  if( dfmt->maxlength > 0 )
    /* CS_NULLTERM sika kangaeteinai !! */
    memset(dest, 0, dfmt->maxlength);
  else
    return CS_ESYNTAX;

  if( cs_dt_crack(ctx, sfmt->datatype,src, &drec) != CS_SUCCEED){
    strcpy(tmpdate,"????/??/?? ??:??:??");
    ret = CS_ESYNTAX;
  } 
  else {
    sprintf(tmpdate,"%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d",
	    (int)(drec.dateyear), (int)(drec.datemonth) + 1, 
	    (int)(drec.datedmonth),
	    (int)(drec.datehour), (int)(drec.dateminute), 
	    (int)(drec.datesecond) );
  }

  len = strlen(tmpdate);
  if( dfmt->maxlength <= len ){
    ret = CS_TRUNCATED;
    len = dfmt->maxlength - 1;
  }
  if( len > 0 ) {
    strncpy(dest,tmpdate, len);
    *( (char *)dest + len ) = '\0';
  }

  if( destlen != NULL )
    *destlen = len;  /* destlen = strlen(tmpdate) ???? */

  return ret;
}
#endif

static CS_CONTEXT *ctx_init( locname, tmout, is_async)
     char *locname;
     CS_INT tmout;
     int is_async; /* if ON , set CS_DEFER_IO */
{
  CS_CONTEXT *context;
  CS_RETCODE	retcode;

  retcode = cs_ctx_alloc( SYB_CTLIB_VERSION, &context );
  if( retcode != CS_SUCCEED )
    return( NULL );

  /*
    Set cs_config
    locname
    */
#ifndef FREETDS
  if( (locname != NULL) && (*locname != '\0') ){
    /*  Set locale */
    CS_LOCALE *locale;

    retcode = cs_loc_alloc(context, &locale);
    if (retcode != CS_SUCCEED){
      cs_ctx_drop(context);
      return NULL;
    }

    retcode = cs_locale(context, CS_SET, locale, CS_LC_ALL, locname, 
			CS_NULLTERM, NULL);
    if (retcode != CS_SUCCEED){
      cs_ctx_drop(context);
      return NULL;
    }
    retcode = cs_config(context, CS_SET, CS_LOC_PROP,locale,
			CS_UNUSED, NULL);
    if (retcode != CS_SUCCEED){
      cs_ctx_drop(context);
      return NULL;
    }
  }
#endif

  /*
   ** Initialize Open Client.
   */
  retcode = ct_init(context, SYB_CTLIB_VERSION);
  if (retcode != CS_SUCCEED){
    goto initfail;
  }
  
#ifdef USE_MY_DATE2STR
  {
    /* INSTALL my date -> string conversion */
    CS_CONV_FUNC	myfunc;
    myfunc = (CS_CONV_FUNC) myconv_date2str;
    cs_set_convert(context, CS_SET, CS_DATETIME_TYPE, CS_CHAR_TYPE,
		   &myfunc );
    cs_set_convert(context, CS_SET, CS_DATETIME4_TYPE, CS_CHAR_TYPE,
		   &myfunc );
  }
#endif

  /*
   ** Install client and server message handlers.
   */
  retcode = ct_callback(context, NULL, CS_SET, CS_CLIENTMSG_CB,
			(CS_VOID *)syb_clientmsg_cb);
  if (retcode != CS_SUCCEED) 
    goto initfail;

  retcode = ct_callback(context, NULL, CS_SET, CS_SERVERMSG_CB,
			(CS_VOID *)syb_servermsg_cb);
  if (retcode != CS_SUCCEED) 
    goto initfail;

  /* 
     synchronous 
   */
  {
    CS_INT		netio_type = CS_SYNC_IO; 
    if( is_async ) netio_type = CS_DEFER_IO;
    /* if( is_async ) netio_type = CS_ASYNC_IO; */

    retcode = ct_config(context, CS_SET, CS_NETIO, &netio_type, 
			CS_UNUSED, NULL);
    if (retcode != CS_SUCCEED){
      goto initfail;
    }
  }

  if( (tmout > 0) && ( is_async == 0 ) ){
    /*  Set Timeout 
	Not set when ASYNC IO ( avoid signal level calling )
     */
#ifdef SYB_DEBUG
    fprintf(stderr,"Set CS_TIMEOUT (%d) in ctx_init\n", (int)tmout);
#endif
    retcode = ct_config(context, CS_SET, CS_TIMEOUT, &tmout,
			CS_UNUSED, NULL);
    if (retcode != CS_SUCCEED){
      goto initfail;
    }
  }

  return( context );

 initfail:
  ct_exit( context, CS_FORCE_EXIT);
  cs_ctx_drop(context);
  return NULL;
}

static VALUE f_ctx_create (argc, argv, class) 
     int argc;
     VALUE  *argv; 
     VALUE class;
{
  char *locname = NULL;
  CS_INT tmout = 0;
  int is_async = 0;
  VALUE obj;
  SYB_CONTEXT_DATA *ctxdata;
  CS_CONTEXT *ctx;

  if( (argc >= 1) && (argv[0] != Qnil) )
    locname = MY_STR2CSTR( argv[0] );
  if( argc >= 2 && (argv[1] != Qnil))
    tmout = (CS_INT)( FIX2INT( argv[1] ) ); 
  if( argc >= 3 && (argv[2] == Qtrue))
    is_async = 1;
  
  ctx = ctx_init( locname, tmout, is_async);
  if( ctx == NULL )
    rb_raise(rb_eRuntimeError,"ctx_init failed");

  obj = Data_Make_Struct
    (class, SYB_CONTEXT_DATA, 0, ctx_free, ctxdata);
  ctxdata->val = ctx; ctxdata->ct_init_flag = 1;
  (ctxdata->ioinfo).is_async = is_async;
  (ctxdata->ioinfo).timeout = tmout;
  /* debug */
  /* fprintf(stderr,"ctxdata adr = %ld\n", (unsigned long)ctxdata); */

#ifdef USE_MEMPOOL
  NAI USE_MEMPOOL ;
  {
    ctxdata->mempool = NULL;
    if( (ctxdata->ioinfo).is_async ){
      CS_INT plen = 1024 * 6 * 10;
      ctxdata->mempool = mymalloc( plen);
      if( ct_config( ctx, CS_SET, CS_MEM_POOL, ctxdata->mempool,
		     plen, NULL) != CS_SUCCEED ){
	free( ctxdata->mempool);
	ctxdata->mempool = NULL;
#ifdef SYB_DEBUG
	fprintf(stderr,"Fail CS_MEM_POOL\n");
#endif
      } 
    } 
  }
#endif
  
/* Record equivalence list in current_objects for ctx <-> obj  */
#ifdef RUBY_TAIOUHYOU  /* It does not take GC when I do this */
  {
    VALUE hash;
    hash = rb_ivar_get( cSybContext, rb_intern("@current_objects") );
    if( TYPE(hash) != T_HASH ){
      rb_bug("ctx_init Programing miss");
    }
    rb_hash_aset(hash, INT2NUM( (long)ctx ) , obj);
  }
#endif

  {
    /* Set CS_USERDATA as ctxobj data */
    CS_RETCODE retcode;
#ifdef SYB_DEBUG
    fprintf(stderr,"ctxobj = %lx\n", (unsigned long)obj);	/* debug */
#endif
    retcode = cs_config(ctx, CS_SET, CS_USERDATA, &obj,
		      sizeof(obj) , NULL);
  }
  return obj;
}

/*
   destroy( &REST option )
   if option == TRUE then CS_FORCE_EXIT
   */
static VALUE f_ctx_destroy (argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
  CS_RETCODE	retcode;
  SYB_CONTEXT_DATA *ctxdata;
  CS_INT option;

  option = CS_UNUSED;
  if( (argc >= 1) && (argv[0] == Qtrue) )
    option = CS_FORCE_EXIT;

  Data_Get_Struct(self, SYB_CONTEXT_DATA, ctxdata);

#ifdef USE_MEMPOOL
  {
    if( ctxdata->mempool ){
      if( ct_config( ctxdata->val, CS_CLEAR, CS_MEM_POOL, NULL,
		     CS_UNUSED, NULL) != CS_SUCCEED ){
#ifdef SYB_DEBUG
	fprintf(stderr,"Fail CS_MEM_POOL CLEAR\n");
#endif
      }
      free( ctxdata->mempool);
      ctxdata->mempool = NULL;
    }
  }
#endif

  retcode = ct_exit( ctxdata->val, option );
  if (retcode != CS_SUCCEED){
    if( option != CS_FORCE_EXIT )return Qfalse;
  }

  retcode = cs_ctx_drop(ctxdata->val);
  ctxdata->val = NULL;
  /* 
     if (retcode != CS_SUCCEED){
     return Qfalse;
     }
     */

#ifdef RUBY_TAIOUHYOU
  /* Delete ctx <-> obj from @current_objects */
  {
    VALUE hash;
    hash = rb_ivar_get( cSybContext, rb_intern("@current_objects") );
    if( TYPE(hash) != T_HASH ){
      rb_bug("ctx_destroy Programing miss");
    }
    /* rb_hash_delete(hash, INT2NUM( (long)(ctxdata->val)) ); */
    rb_hash_aset(hash, INT2NUM( (long)(ctxdata->val)) , Qnil);
  }
#endif
#ifdef SYB_DEBUG
  fprintf(stderr,"f_ctx_destroy\n");
#endif
  return Qtrue;
}

static VALUE f_ctx_getprop(self, proptype )
     VALUE self;
     VALUE proptype;
{
  VALUE ret = Qnil;
  SYB_CONTEXT_DATA *ctxdata;
  Data_Get_Struct(self, SYB_CONTEXT_DATA, ctxdata);
  if( ctxdata->val != NULL ){
    ret = get_props(ctxdata->val, NUM2INT( proptype),
		    ct_config);
  }
  return ret;
}

/*
  Sets property
  return	true/false
*/
static VALUE f_ctx_setprop(self, proptype, val )
     VALUE self;
     VALUE proptype;
     VALUE val;
{
  VALUE ret = Qfalse;
  SYB_CONTEXT_DATA *ctxdata;
  Data_Get_Struct(self, SYB_CONTEXT_DATA, ctxdata);
  if( ctxdata->val != NULL ){
    CS_RETCODE csret;
    csret = set_props(ctxdata->val, NUM2INT( proptype), val,
		    ct_config);
    if( csret == CS_SUCCEED ) ret = Qtrue;
  }
  return ret;
}

/*
  Context CS_SERVERMSG_CB default
  */
static VALUE f_ctx_srvmsgCB(self, conobj, hash)
  VALUE conobj, hash;
  VALUE self;
{
  rb_warning("f_ctx_srvmsgCB default\n");
  return Qtrue;
}
static VALUE f_ctx_cltmsgCB(self, conobj, hash)
  VALUE conobj, hash;
  VALUE self;
{
  rb_warning("f_ctx_cltmsgCB default\n");
  return Qtrue;
}

static void con_free( condata )
     SYB_CONNECTION_DATA *condata;
{
  if( condata->val != NULL ){
    if( condata->is_connect != 0 ){
      CS_RETCODE retcode;
      retcode = ct_close( condata->val, CS_FORCE_CLOSE );
      IOWAIT(retcode, &(condata->ioinfo), condata->val, CT_CLOSE);
    }
    ct_con_drop(condata->val);
  }
  /* For BAD reference */
  condata->is_connect = 0;
  condata->val = NULL;

#ifdef SYB_DEBUG
  fprintf(stderr,"Free sybcon\n");	/* debug */
#endif
  free( condata);
}

static VALUE f_con_new(class, ctxobj)
     VALUE class, ctxobj;
{
  VALUE obj = Qnil;
  SYB_CONTEXT_DATA *ctxdata;

  CS_CONNECTION *conn = NULL;
  SYB_CONNECTION_DATA *condata;
  CS_RETCODE	retcode;

  /* Get SybContext */
  Data_Get_Struct(ctxobj, SYB_CONTEXT_DATA, ctxdata );
  if( (ctxdata->val == NULL) || (ctxdata->ct_init_flag == 0 ) )
    rb_raise(rb_eRuntimeError,"SybContext object is not initialized");

  if( ct_con_alloc( ctxdata->val, &conn ) != CS_SUCCEED)
    rb_raise(rb_eRuntimeError,"ct_con_alloc failed");

  obj = Data_Make_Struct
    (class, SYB_CONNECTION_DATA, 0, con_free, condata);
  condata->val = conn;
  condata->is_connect = 0;
  memcpy( &(condata->ioinfo), &(ctxdata->ioinfo),
	  sizeof( SYB_IO_INFO) ); /* copy ioinfo from parent */

  {
    /* Set CS_USERDATA  */
    SYB_CALLBACK_USERDATA udata;
#ifdef SYB_DEBUG
    fprintf(stderr,"XX ctxobj=%lx, obj=%lx\n",ctxobj,obj); /* debug */
#endif
    udata.conobj = obj;
    udata.ctxobj = ctxobj;
    retcode = ct_con_props(conn, CS_SET, CS_USERDATA, &udata,
			   sizeof(udata), NULL); 
  }

  return obj;
}

/*
  connect server
  conn.connect
     (SybContext server user &REST passwd appname )
  */
static VALUE f_con_connect(argc, argv, self)
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  SYB_CONTEXT_DATA *ctxdata;
  VALUE *parg, *lastarg;
  SYB_CONNECTION_DATA *condata;
  VALUE ctxobj;

  CS_CHAR *server, *username, *passwd, *appname ;
  CS_CONNECTION	*con = NULL;
  CS_RETCODE	retcode;
  char *emsg = "";

  server = username = passwd  = appname = NULL;

  if( argc < 3 ){
    rb_raise(rb_eArgError,"wrong # of arguments");
  }
  lastarg = argv + argc - 1;
  parg = argv;

  /* Get SybContext */
  ctxobj = *parg;
  Data_Get_Struct(*parg, SYB_CONTEXT_DATA, ctxdata );
  if( ctxdata->val == NULL )
    rb_raise(rb_eRuntimeError,"SybContext not initialized");
#ifdef SYB_DEBUG
  /* debug Need? free(ctxdata) */
  fprintf(stderr,"ctxdata adr = %ld\n", (unsigned long)ctxdata);
#endif

  ++parg;
  if( *parg != Qnil )
    server = (CS_CHAR *)(MY_STR2CSTR(*parg) );

  ++parg;
  if( *parg != Qnil )
    username = (CS_CHAR *)(MY_STR2CSTR(*parg));

  if( parg == lastarg ) goto next;
  ++parg;
  if( *parg != Qnil)
    passwd = (CS_CHAR *)(MY_STR2CSTR(*parg) );

  if( parg == lastarg ) goto next;
  ++parg;
  if( *parg != Qnil)
    appname = (CS_CHAR *)(MY_STR2CSTR(*parg) );

 next:
  if( username == NULL )
    rb_raise(rb_eArgError,"No user");

  Data_Get_Struct(self, SYB_CONNECTION_DATA, condata);
  if(condata->is_connect != 0)
    rb_raise(rb_eRuntimeError,"Already connected");
  con = condata->val;
  if( con == NULL )
    rb_raise(rb_eRuntimeError,"No object");  /* ??? */

  /*	
	Set connection property . username,passwd,appname
   */
  if ((retcode = ct_con_props(con, CS_SET, CS_USERNAME, 
			      username, CS_NULLTERM, NULL)) != CS_SUCCEED){
    emsg = "failed in setting of username";
    goto equit;
  }

  if( passwd != NULL ){
    if ((retcode = ct_con_props(con, CS_SET, CS_PASSWORD, 
				passwd, CS_NULLTERM, NULL)) != CS_SUCCEED){
      emsg = "failed in setting password";
      goto equit;
    }
  }

  if( appname != NULL ){
    if ((retcode = ct_con_props(con, CS_SET, CS_APPNAME, 
				appname, CS_NULLTERM, NULL)) != CS_SUCCEED){
      emsg = "failed in setting appname";
      goto equit;
    }
  }

  /*	
	connect servr
   */
  {
    CS_INT len;
    len = (server == NULL) ? 0 : CS_NULLTERM;
    /* printf("con open begin\n"); */  /* debug */
    retcode = ct_connect(con, server, len);
    IOWAIT(retcode, &(condata->ioinfo), con, CT_CONNECT);
    if (retcode != CS_SUCCEED) {
      /* printf("con open fail\n"); */ /* debug */
      emsg =  "connect failed";
      goto equit;
    }
    /* printf("con open end\n"); */  /* debug */
  }

  condata->is_connect = 1;
  return( Qtrue );

 equit:
  rb_raise(rb_eRuntimeError,emsg);
}

#ifdef HURUI
/*
  connect server
  SybConnection.open
     (SybContext server user &REST passwd app-name hostname)
  server is nil , use $DSQUERY 
  */
static VALUE f_con_open(argc, argv, class)
     int argc;
     VALUE  *argv; 
     VALUE class;
{
  SYB_CONTEXT_DATA *ctxdata;
  VALUE *parg, *lastarg;
  VALUE obj;
  SYB_CONNECTION_DATA *condata;
  VALUE ctxobj;

  CS_CHAR *server, *username, *passwd, *appname, *hostname ;
  CS_CONNECTION	*con = NULL;
  CS_RETCODE	retcode;
  char *emsg = "";

  server = username = passwd = appname = hostname = NULL;

  if( argc < 3 ){
    rb_raise(rb_eArgError,"wrong # of arguments");
  }
  lastarg = argv + argc - 1;
  parg = argv;

  /* Get SybContext */
  ctxobj = *parg;
  Data_Get_Struct(*parg, SYB_CONTEXT_DATA, ctxdata );
  if( ctxdata->val == NULL )
    rb_raise(rb_eRuntimeError,"SybContext is not initialized");
#ifdef SYB_DEBUG
  /* debug Need? free(ctxdata) */
  fprintf(stderr,"ctxdata adr = %ld\n", (unsigned long)ctxdata);
#endif

  ++parg;
  if( *parg != Qnil )
    server = (CS_CHAR *)(MY_STR2CSTR(*parg) );

  ++parg;
  if( *parg != Qnil )
    username = (CS_CHAR *)(MY_STR2CSTR(*parg));

  if( parg == lastarg ) goto next;
  ++parg;
  if( *parg != Qnil)
    passwd = (CS_CHAR *)(MY_STR2CSTR(*parg) );

  if( parg == lastarg ) goto next;
  ++parg;
  if( *parg != Qnil)
    appname = (CS_CHAR *)(MY_STR2CSTR(*parg) );

  if( parg == lastarg ) goto next;
  ++parg;
  if( *parg != Qnil)
    hostname = (CS_CHAR *)( MY_STR2CSTR(*parg) );

 next:
  if( username == NULL )
    rb_raise(rb_eArgError,"No user");

  /* alloc connection */
  retcode = ct_con_alloc( ctxdata->val, &con );
  if( retcode != CS_SUCCEED ){
    rb_raise(rb_eRuntimeError,"ct_con_alloc failed");
  }

  /*	
   ** Set connection property . username,passwd,appname
   */
  if ((retcode = ct_con_props(con, CS_SET, CS_USERNAME, 
			      username, CS_NULLTERM, NULL)) != CS_SUCCEED){
    emsg = "failed in setting username";
    goto equit;
  }

  if( passwd != NULL ){
    if ((retcode = ct_con_props(con, CS_SET, CS_PASSWORD, 
				passwd, CS_NULLTERM, NULL)) != CS_SUCCEED){
      emsg = "failed in setting password";
      goto equit;
    }
  }

  if( appname != NULL ){
    if ((retcode = ct_con_props(con, CS_SET, CS_APPNAME, 
				appname, CS_NULLTERM, NULL)) != CS_SUCCEED){
      emsg = "failed in setting appname";
      goto equit;
    }
  }

  if( hostname != NULL ){
    if ((retcode = ct_con_props(con, CS_SET, CS_HOSTNAME, 
				hostname, CS_NULLTERM, NULL)) != CS_SUCCEED){
      emsg = "failed in setting hostname";
    }
  }

#if 0
  {
    CS_INT val,lval;
    val = 512;
    ct_con_props(con, CS_SET, CS_PACKETSIZE, &val
		 , CS_UNUSED, NULL);
  }
#endif

  /*	
	connect server
   */
  {
    CS_INT len;
    len = (server == NULL) ? 0 : CS_NULLTERM;
    /* printf("con open begin\n"); */  /* debug */
    retcode = ct_connect(con, server, len);
    HURUI_IOWAIT(retcode, 1, con, CT_CONNECT);   /* not use condata */
    if (retcode != CS_SUCCEED) {
      /* printf("con open fail\n"); */  /* debug */
      emsg =  "connect failed";
      goto equit;
    }
    /* printf("con open end\n");  */  /* debug */
  }

#if 0
  {
    CS_BOOL val;
    val = CS_TRUE;
    printf("FORCEPLAN\n");
    ct_options( con, CS_SET, CS_OPT_FORCEPLAN, &val, CS_UNUSED,
	       NULL);
    HURUI_IOWAIT();
    printf("FORCEPLAN\n");
  }
#endif
#if 0
  {
    CS_INT val;
    ct_con_props(con, CS_GET, CS_PACKETSIZE, &val
		 , CS_UNUSED, NULL);
    printf("PACKETSIZE = %d\n", val );
    val = 500;
    printf("CS_ROWCOUNT\n");
    ct_options( con, CS_SET, CS_OPT_ROWCOUNT, &val, CS_UNUSED,
	       NULL);
    HURUI_IOWAIT();
  }
#endif

  obj = Data_Make_Struct
    (class, SYB_CONNECTION_DATA, 0, con_free, condata);

  condata->val = con;
  condata->is_connect = 1;
  memcpy( &(condata->ioinfo), &(ctxdata->ioinfo),
	  sizeof( SYB_IO_INFO) ); /* copy ioinfo from parent */

  {
    /* Set CS_USERDATA  */
    SYB_CALLBACK_USERDATA udata;
#ifdef SYB_DEBUG
    fprintf(stderr,"XX ctxobj=%lx, obj=%lx\n",ctxobj,obj); /* debug */
#endif
    udata.conobj = obj;
    udata.ctxobj = ctxobj;
    retcode = ct_con_props(con, CS_SET, CS_USERDATA, &udata,
			   sizeof(udata), NULL);
  }
  return( obj );

 equit:
  if( con != NULL )
    retcode = ct_con_drop(con);
  if (retcode != CS_SUCCEED){
    rb_raise(rb_eRuntimeError,"ct_con_drop");
  }
  rb_raise(rb_eRuntimeError,"emsg");
}
#endif

/*
   close connection
   close( &REST option )
   if option == TRUE then CS_FORCE_CLOSE
  */
static VALUE f_con_close(argc, argv, self)
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  CS_RETCODE	retcode;
  CS_INT option;
  SYB_CONNECTION_DATA *condata;

  option = CS_UNUSED;
  if( (argc >= 1) && (argv[0] == Qtrue) )
    option = CS_FORCE_CLOSE;
  
  Data_Get_Struct(self, SYB_CONNECTION_DATA, condata);

  if( condata->val != NULL ){
    if(condata->is_connect != 0){
      retcode = ct_close( condata->val, option );
      IOWAIT(retcode, &(condata->ioinfo), condata->val, CT_CLOSE);
      if (retcode != CS_SUCCEED){
	if( option != CS_FORCE_EXIT )return Qfalse;
      }
      condata->is_connect = 0;
    }
#ifdef SYB_DEBUG
    fprintf(stderr,"f_con_close\n");
#endif
    return Qtrue;
  }
  return Qnil;
}
/*
   close connection and delete SybConnection
   delete( &REST option )
   if option == TRUE then CS_FORCE_CLOSE
  */
static VALUE f_con_delete(argc, argv, self)
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  CS_RETCODE	retcode;
  CS_INT option;
  SYB_CONNECTION_DATA *condata;

  option = CS_UNUSED;
  if( (argc >= 1) && (argv[0] == Qtrue) )
    option = CS_FORCE_CLOSE;
  
  Data_Get_Struct(self, SYB_CONNECTION_DATA, condata);

  if( condata->val != NULL ){
    if(condata->is_connect != 0){
      retcode = ct_close( condata->val, option );
      IOWAIT(retcode, &(condata->ioinfo), condata->val, CT_CLOSE);
      if (retcode != CS_SUCCEED){
	if( option != CS_FORCE_EXIT )return Qfalse;
      }
      condata->is_connect = 0;
    }

    ct_con_drop(condata->val);
    condata->val = NULL;
#ifdef SYB_DEBUG
    fprintf(stderr,"f_con_delete\n");
#endif
    return Qtrue;
  }
  return Qnil;
}

/*
  Gets property.
  getprop( property )
  ERROR: nil return
  */
static VALUE f_con_getprop(self, proptype )
     VALUE self;
     VALUE proptype;
{
  VALUE ret = Qnil;
  SYB_CONNECTION_DATA *condata;
  Data_Get_Struct(self, SYB_CONNECTION_DATA, condata);
  if( condata->val != NULL ){
    ret = get_props(condata->val, NUM2INT( proptype),
		    ct_con_props);
  }
  return ret;
}

/*
  Sets property.
  return	true/false
  */
static VALUE f_con_setprop(self, proptype, val )
     VALUE self;
     VALUE proptype;
     VALUE val;
{
  VALUE ret = Qfalse;
  SYB_CONNECTION_DATA *condata;
  Data_Get_Struct(self, SYB_CONNECTION_DATA, condata);
  if( condata->val != NULL ){
    CS_RETCODE csret;
    csret = set_props(condata->val, NUM2INT( proptype), val,
		    ct_con_props);
    if( csret == CS_SUCCEED ) ret = Qtrue;
  }
  return ret;
}

/*
  Gets serverc options
  ERROR: nil return
  */
static VALUE f_con_getopt(self, option )
     VALUE self;
     VALUE option;
{
  char type;
  VALUE ret = Qnil;
  SYB_CONNECTION_DATA *condata;
  CS_INT outlen = 0;  /* when freetds, must zero initialize */
  CS_RETCODE csret;

  Data_Get_Struct(self, SYB_CONNECTION_DATA, condata);
  if( condata->val != NULL ){
    type = options_buffer_type(NUM2INT(option));
    if( type == 'B' ){
      /* boolean type */
      CS_BOOL boo;
      csret = 
	ct_options( condata->val, CS_GET, NUM2INT(option),
		    &boo, CS_UNUSED, &outlen);
      IOWAIT(csret, &(condata->ioinfo), condata->val, CT_OPTIONS);
      if( (csret == CS_SUCCEED ) || (outlen > 0 ) ){
	if( boo == CS_TRUE ) ret = Qtrue;
	else if ( boo == CS_FALSE) ret = Qfalse;
      }
    }
    else if( type == 'S' ) {
      /* string type */
      char buf[1023 + 1];  /* +1 -- For peace of mind */
      char *pbuf;
      pbuf = buf;
      do {	/* Block that does not iterates */
	csret = 
	  ct_options( condata->val, CS_GET, NUM2INT(option),
		      pbuf, 1023, &outlen);
	IOWAIT(csret, &(condata->ioinfo), condata->val, CT_OPTIONS);
	if( csret != CS_SUCCEED ){
	  int len ;
	  if( outlen <= 1023)
	    break;
	  /* When length was not enough */
	  len = outlen;
	  pbuf = mymalloc(len + 1 ); /* +1 -- for peace of mind */
	  csret = 
	    ct_options( condata->val, CS_GET, NUM2INT(option),
			pbuf, len, &outlen);
	  IOWAIT(csret, &(condata->ioinfo), condata->val, CT_OPTIONS);
	  if( (csret != CS_SUCCEED ) || (outlen < 0 ) )
	    break;
	  if( outlen > len) outlen = len;
	}
	if(outlen < 0 )break;
	if( outlen > 0 ){
	  /* outlen ha NULL madeno nagasa ?? */
	  if( *(pbuf + outlen - 1) == '\0' )
	    --outlen;
	}
	ret = rb_str_new(pbuf, outlen);
      } while(0);
      if( pbuf != buf ) free(pbuf);
    }
    else if( (type == 'I') || (type == 'C') ){
      /* integer */
      CS_INT val;
      csret = 
	ct_options( condata->val, CS_GET, NUM2INT(option),
		    &val, CS_UNUSED, &outlen);
      IOWAIT(csret, &(condata->ioinfo), condata->val, CT_OPTIONS);
      if( (csret == CS_SUCCEED ) || (outlen > 0 ) ){
	ret = INT2NUM( val );
      }
    }
  }
  return ret;
}

/*
  Set server options
  return	true/false
  */
static VALUE f_con_setopt(self, option, val )
     VALUE self;
     VALUE option;
     VALUE val;
{
  char type;
  VALUE ret = Qfalse;
  SYB_CONNECTION_DATA *condata;
  CS_RETCODE csret = CS_FAIL;

  Data_Get_Struct(self, SYB_CONNECTION_DATA, condata);
  if( condata->val != NULL ){
    type = options_buffer_type(NUM2INT(option));
    if( type == 'B' ){
      /* boolean type */
      CS_BOOL boo;
      /* Sets CS_TRUE , if it is not false or  nil */
      if( (val == Qfalse ) || (val == Qnil) )
	boo = CS_FALSE;
      else
	boo = CS_TRUE;
      csret = 
	ct_options( condata->val, CS_SET, NUM2INT(option),
		    &boo, CS_UNUSED, NULL);
      IOWAIT(csret, &(condata->ioinfo), condata->val, CT_OPTIONS);
    }
    else if( type == 'S' ) {
      /* string type */
      char *pbuf;
      if( TYPE(val) == T_STRING ){
	pbuf = (CS_CHAR *)(MY_STR2CSTR(val) );
	if( pbuf == NULL ) pbuf = "";  /* BUG ? */
	csret = 
	  ct_options( condata->val, CS_SET, NUM2INT(option),
		      pbuf, CS_NULLTERM, NULL);
	IOWAIT(csret, &(condata->ioinfo), condata->val, CT_OPTIONS);
      }
    }
    else if( (type == 'I') || (type == 'C') ){
      /* integer */
      CS_INT ival;
      if( (TYPE(val) == T_FIXNUM) ||
	  (TYPE(val) == T_BIGNUM) || (TYPE(val) == T_FLOAT) ){
	ival = (CS_INT)( NUM2INT(val) );
	csret = 
	  ct_options( condata->val, CS_SET, NUM2INT(option),
		      &ival, CS_UNUSED, NULL);
	IOWAIT(csret, &(condata->ioinfo), condata->val, CT_OPTIONS);
      }
    }
    if( csret == CS_SUCCEED ) ret = Qtrue;
  }
  return ret;
}

/*
   SybCommand
   */

/*
  is connection alive (OPEN, and NOT DEAD)
*/
static int is_alive_con( conn )
  CS_CONNECTION *conn;
{
  CS_INT status = 0;
  if( conn == NULL ) return 0;
  if( ct_con_props(conn, CS_GET, CS_CON_STATUS, &status, CS_UNUSED,
		   NULL) != CS_FAIL ) {
    if( status & CS_CONSTAT_CONNECTED ){
      /* opend! */
      if( status & CS_CONSTAT_DEAD ) {
#ifdef SYB_DEBUG
	fprintf(stderr,"Connection DEAD !\n");
#endif	
	return 0;  /* dead */
      }
      return 1;
    }
#ifdef SYB_DEBUG
	fprintf(stderr,"Connection NOOPEN !\n");
#endif	
  }
  return 0;
}

static void cmd_colbuf_free();

/*
  memory allocate for SYB_COMMAND_DATA->SYB_COLUMN_DATA
  initialize 
	ruby_type <= RUBY_STR_TYPE
  */
static void cmd_colbuf_new( cmddata, num_cols)
     SYB_COMMAND_DATA *cmddata;
     int num_cols;
{
  int i;

  if( cmddata->colbuf != NULL )
    cmd_colbuf_free( cmddata );
  cmddata->colbuf = 
    mymalloc( sizeof( SYB_COLUMN_DATA ) * num_cols );
  cmddata->len_colbuf = num_cols;
  for(i=0 ; i< num_cols; ++i ){
    cmddata->colbuf[i].ruby_type = RUBY_STR_TYPE;
    cmddata->colbuf[i].is_bind = 0;
    cmddata->colbuf[i].svalue = NULL;
    cmddata->colbuf[i].indicator = 0;	/* No need ? */
    cmddata->colbuf[i].valuelen = 0;	/* No need ? */
    memset(&(cmddata->colbuf[i].datafmt), 0, sizeof (CS_DATAFMT)); /* in BSD bzero? */
  }
}
/*
  release memory for SYB_COMMAND_DATA->SYB_COLUMN_DATA
  */
static void cmd_colbuf_free( cmddata)
     SYB_COMMAND_DATA *cmddata;
{
  int i;
  if( cmddata == NULL )return;
  if( cmddata->colbuf != NULL ){
    for (i = 0; i < cmddata->len_colbuf; i++)
      free( cmddata->colbuf[i].svalue);
    free( cmddata->colbuf );
    cmddata->colbuf = NULL;
  }
  cmddata->len_colbuf = 0;
}

static void cmd_free( cmddata )
     SYB_COMMAND_DATA *cmddata;
{
  cmd_colbuf_free(cmddata);

  if( cmddata->val != NULL ){
    CS_RETCODE retcode;
    if( ISALIVE( cmddata ) ){
      retcode = cmd_or_cursor_cancel(cmddata, CS_CANCEL_ALL );
      /*  retcode = ct_cancel( NULL, cmddata->val, CS_CANCEL_ALL ); */
      /* IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_CANCEL ); */
    }
    ct_cmd_drop( cmddata->val);
    cmddata->val = NULL;
    cmddata->status = 0;
  }
#ifdef SYB_DEBUG
  fprintf(stderr,"Free sybcmd\n");	/* debug */
#endif
  free( cmddata);
}

/*
  Command open 
  SybCommand.new(SybConnection, strobj, &REST type, opt)
  type			strobj
  CS_LANG_CMD(default)	T-SQL command string
  CS_RPC_CMD		RPC name
  CS_SEND_DATA_CMD	send data
  opt	CS_RECOMPILE, CS_NORECOMPILE, CS_UNUSED(default)

  ERROR: RuntimeError
  */
static VALUE f_cmd_new( argc, argv, class)
     int argc;
     VALUE  *argv; 
     VALUE class;
{
  VALUE conobj,strobj;
  VALUE obj;
  SYB_CONNECTION_DATA *condata;
  CS_COMMAND *cmd = NULL;
  CS_RETCODE	retcode;
  char *str;
  SYB_COMMAND_DATA *cmddata;
  CS_INT type, lstr, opt;

  if( argc < 2 ){
    rb_raise(rb_eArgError,"wrong # of arguments");
  }
  conobj = argv[0];
  /* Get SybConnection */
  Data_Get_Struct(conobj, SYB_CONNECTION_DATA, condata );
  if( (condata->val == NULL) || (condata->is_connect == 0 ) )
    rb_raise(rb_eRuntimeError,"SybConnection object is not connected");
  
  strobj = argv[1];
  type = CS_LANG_CMD;
  opt = CS_UNUSED;
  str = NULL;
  lstr = CS_UNUSED;
  if( argc >= 3 )
    type = (CS_INT)( NUM2INT( argv[2] ) );
  if( argc >= 4 )
    opt = (CS_INT)( NUM2INT( argv[3] ) );
  
  if( type != CS_SEND_DATA_CMD ){
    /* Get sql string */
    str = MY_STR2CSTR( strobj );
    lstr = CS_NULLTERM;
  }
  else if ( opt == CS_UNUSED ) {
    /* default opt is CS_COLUMN_DATA when type is CS_SEND_DATA_CMD */
    opt = CS_COLUMN_DATA;
  }

  if ( ct_cmd_alloc(condata->val, &cmd) != CS_SUCCEED) 
    rb_raise(rb_eRuntimeError,"ct_cmd_alloc failes");
  
  retcode = ct_command(cmd, type, str, lstr, (CS_INT)opt);
  if (retcode != CS_SUCCEED){
    ct_cmd_drop( cmd );
    rb_raise(rb_eRuntimeError,"ct_command failed");
  }

  obj = Data_Make_Struct
    (class, SYB_COMMAND_DATA, 0, cmd_free, cmddata);

  cmddata->val = cmd;
  cmddata->status = 0;
  cmddata->colbuf = NULL;
  cmddata->len_colbuf = 0;
  cmddata->conn = condata->val;	/* Set parent connection handle */
  memcpy( &(cmddata->ioinfo), &(condata->ioinfo),
	  sizeof( SYB_IO_INFO) ); /* copy ioinfo from parent */
  
  /* Set instance variable */
  rb_ivar_set(obj, rb_intern("@bind_numeric2fixnum"),Qfalse);
  rb_ivar_set(obj, rb_intern("@fetch_rowfail"),Qfalse);

  return( obj );
}

/*
  Create CS_COMMAND structure , and declare cursor 
  SybCommand.new_cursor(SybConnection, curname, langcmd, &REST opt )

  curname		The cursor name
  langcmd			language command for cursor
  opt			default is CS_UNUSED
                        Following Bit mask
			  CS_FOR_UPDATE, CS_READ_ONLY,
			  CS_MORE, CS_END (only CTLib version 11 or later )
  ERROR: RuntimeError
  */
static VALUE f_cmd_cursor_new( argc, argv, class)
     int argc;
     VALUE  *argv; 
     VALUE class;
{
  VALUE conobj, curname, langcmd;
  VALUE obj;
  SYB_CONNECTION_DATA *condata;
  CS_COMMAND *cmd = NULL;
  CS_RETCODE	retcode;
  char *namestr, *cmdstr;
  SYB_COMMAND_DATA *cmddata;
  CS_INT  opt;

  if( argc < 3 ){
    rb_raise(rb_eArgError,"wrong # of arguments");
  }
  conobj = argv[0];
  /* Get SybConnection */
  Data_Get_Struct(conobj, SYB_CONNECTION_DATA, condata );
  if( (condata->val == NULL) || (condata->is_connect == 0 ) )
    rb_raise(rb_eRuntimeError,"SybConnection object is not connected");
  
  curname = argv[1];
  langcmd = argv[2];
  opt = CS_UNUSED;
  if( (argc >= 4) && (argv[3] != Qnil))
    opt = (CS_INT)( NUM2INT( argv[3] ) );
  
  namestr = MY_STR2CSTR( curname);
  cmdstr = MY_STR2CSTR( langcmd);

  if ( ct_cmd_alloc(condata->val, &cmd) != CS_SUCCEED) 
    rb_raise(rb_eRuntimeError,"ct_cmd_alloc failed");
  
  retcode = ct_cursor(cmd, CS_CURSOR_DECLARE,
		      namestr, CS_NULLTERM, cmdstr, CS_NULLTERM,
		      (CS_INT)opt);
  if (retcode != CS_SUCCEED){
    ct_cmd_drop( cmd );
    rb_raise(rb_eRuntimeError,"ct_cursor failed");
  }

  obj = Data_Make_Struct
    (class, SYB_COMMAND_DATA, 0, cmd_free, cmddata);

  cmddata->val = cmd;
  cmddata->status = SYBSTATUS_CURSOR_DECLARED;
  cmddata->colbuf = NULL;
  cmddata->len_colbuf = 0;
  cmddata->conn = condata->val;	/* Set parent connection handle */
  memcpy( &(cmddata->ioinfo), &(condata->ioinfo),
	  sizeof( SYB_IO_INFO) ); /* copy ioinfo from parent */
  
  return( obj );
}

/*
  Command send
  cmd.send()
  */
static VALUE f_cmd_send(self)
     VALUE self;
{
  SYB_COMMAND_DATA *cmddata;
  CS_RETCODE	retcode;

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    rb_raise(rb_eRuntimeError,"SybCommand is not active\n");
  retcode = ct_send(cmddata->val);
  IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_SEND ) ;
  if (retcode  != CS_SUCCEED)
    return Qfalse; 
  return Qtrue;
}

static VALUE f_cmd_delete(self)
     VALUE self;
{
  SYB_COMMAND_DATA *cmddata;
  /* CS_RETCODE	retcode; */

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);

  cmd_colbuf_free(cmddata);

  if( cmddata->val != NULL ){
    CS_RETCODE retcode;
#ifdef SYB_DEBUG
    printf("--K2.1 \n");  /* RDEBUG */
#endif
    if( ISALIVE( cmddata) ){
      /* retcode = ct_cancel( NULL, cmddata->val, CS_CANCEL_ALL ); */
      /* ct_poll has become CS_QUIET at this place  ???? */
      /* IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_CANCEL ) ; */
      retcode = cmd_or_cursor_cancel(cmddata, CS_CANCEL_ALL );
    }
    cmddata->status = 0;
#ifdef SYB_DEBUG
    printf("--K2 %ld, %ld\n",(long)cmddata, (long)(cmddata->val));  /* RDEBUG */
#endif
    if( ct_cmd_drop( cmddata->val ) != CS_SUCCEED ){
#ifdef SYB_DEBUG
      fprintf(stderr,"This is BUG\n");
#endif
      cmddata->val = NULL;  /* Sikatanai */
      return Qfalse;
    }
    cmddata->val = NULL;
#ifdef SYB_DEBUG
    fprintf(stderr,"f_cmd_delete\n");
#endif
    return Qtrue;
  }
  return Qnil;
}

/*
  Retrieves Result Sets
  return
  nil		No Result Sets available(CS_END_RESULTS)
  false		ERROR 
  int		Result type constant to process ( CS_ROW_RESULT etc. )
   */
static VALUE f_cmd_results(self)
     VALUE self;
{
  SYB_COMMAND_DATA *cmddata;
  CS_RETCODE	retcode; 
  CS_INT	res_type;	/* result type from ct_results */

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val != NULL ){
    retcode = ct_results(cmddata->val, &res_type);
    IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_RESULTS ) ;
    if( retcode != CS_SUCCEED ){
      if( retcode != CS_END_RESULTS )
	return Qfalse;
      else
	return Qnil;
    }
    return INT2NUM( (int)res_type );
  }
  return Qnil;
}

static CS_INT CS_PUBLIC syb_display_dlen(column)
     CS_DATAFMT 		*column;
{
  CS_INT		len;

  switch ((int) column->datatype)
    {
    case CS_CHAR_TYPE:
    case CS_VARCHAR_TYPE:
    case CS_TEXT_TYPE:
    case CS_IMAGE_TYPE:
      len = column->maxlength;
      break;

    case CS_BINARY_TYPE:
    case CS_VARBINARY_TYPE:
      len = (2 * column->maxlength) + 2 ;
      break;

    case CS_BIT_TYPE:
    case CS_TINYINT_TYPE:
      /* len = 3; */
      len = 6; 
      break;

    case CS_SMALLINT_TYPE:
      /* len = 6; */
      len = 8; 
      break;

    case CS_INT_TYPE:
      /* len = 11; */
      len = 12;
      break;

    case CS_REAL_TYPE:
    case CS_FLOAT_TYPE:
      /* len = 20; */
      len = 20;
      break;

    case CS_MONEY_TYPE:
    case CS_MONEY4_TYPE:
      len = 24;
      break;

    case CS_DATETIME_TYPE:
    case CS_DATETIME4_TYPE:
      len = 30;
      break;

    case CS_NUMERIC_TYPE:
    case CS_DECIMAL_TYPE:
      /* len = (CS_MAX_PREC + 2); */
      len = column->precision + 3;
      break;

    default:
      len = column->maxlength;
      break;
    }

  return MAX(strlen(column->name) + 1, len);
}

/*
  pdata wo fetch de uketoru format ni henkou
  mata, ct_bind ni ataeru pcol data wo hennshuu

  pdata		ct_describe sareta atono  CS_DATAFMT
  pcol		ct_bind ni ataeru memory ryouiki
*/
#define USE_NULLTERM 1
static void set_bindfmt ( pdata, pcol, is_bind_numeric2fixnum )
  CS_DATAFMT *pdata;
  SYB_COLUMN_DATA *pcol;
  int is_bind_numeric2fixnum;  /* is bind numeric(N,0 ) to fixnum ? */
{

  pdata->count = 0;  /* every one row */
  pcol->svalue = NULL;

  switch ((int) pdata->datatype)
    {
    case CS_TINYINT_TYPE:
    case CS_SMALLINT_TYPE:
    case CS_INT_TYPE:
    case CS_BIT_TYPE:        /* (By Will Sobel) */
      pdata->maxlength = sizeof( CS_INT );
      pdata->datatype = CS_INT_TYPE;
      pdata->format = CS_FMT_UNUSED;

      pcol->ruby_type = RUBY_NUM_TYPE;  /* NUMERIC in Ruby */
      break;
    case CS_REAL_TYPE:
    case CS_FLOAT_TYPE:
      pdata->maxlength = sizeof( CS_FLOAT );
      pdata->datatype = CS_FLOAT_TYPE;
      pdata->format = CS_FMT_UNUSED;

      pcol->ruby_type = RUBY_FLOAT_TYPE;  /* FLOAT in Ruby  */
      break;

#ifndef BIND_TIMESTAMP_AS_CHAR
    case CS_BINARY_TYPE:	/* Before default!!! */
      if( pdata->status & CS_TIMESTAMP ){
	/* 
	   For fetching timestamp  PARAM_RESULT in ct_send_data, 
	   timestamp is binded as 8 byte (not CHAR) datas to ruby String.
	*/
	pdata->maxlength = sizeof(pcol->timestamp);
	pdata->format = CS_FMT_UNUSED;
	pcol->ruby_type = RUBY_TIMESTAMP_TYPE; /* treat it as String in ruby */
#ifdef SYB_DEBUG
	fprintf(stderr,"TIMESTAMP COLUMN size is %ld\n",pdata->maxlength);
#endif
	break;
      }
      /* CS_BINARY_TYPE( not timestamp) is treated as String  */
#endif  /* NOT BIND_TIMESTAMP_AS_CHAR endif */

    /* For Rails identity column (patchd by William Sobel).
     * Bind numeric(N,0) to ruby-fixnum if (N <= 10)
     */
    case CS_DECIMAL_TYPE:
    case CS_NUMERIC_TYPE:  
#ifdef SYB_DEBUG2
      printf("NUMERIC COLUMN scale is %d\n",pdata->scale);
#endif
      if( is_bind_numeric2fixnum  &&
	  /* pdata->datatype == CS_NUMERIC_TYPE &&  */
	  /* (pdata->status & CS_IDENTITY ) && */
	  pdata->scale == 0 && pdata->precision <= 10)
      {
#ifdef SYB_DEBUG2
	printf("%s bind numeric(N,0) to ruby fixnum\n", pdata->name);
#endif
	pdata->maxlength = sizeof( CS_INT );
	pdata->datatype = CS_INT_TYPE;
	pdata->format = CS_FMT_UNUSED;
	
	pcol->ruby_type = RUBY_NUM_TYPE;  /* NUMERIC in Ruby */
	break;
      }
    default:
      /* Others --> binds to String */
      pdata->maxlength = syb_display_dlen(pdata) + 1;
#ifdef USE_NULLTERM
      if( pdata->datatype == CS_IMAGE_TYPE )
	pdata->format = CS_FMT_UNUSED;
      else
	pdata->format   = CS_FMT_NULLTERM; 
      pdata->datatype = CS_CHAR_TYPE;
#else
      pdata->format   = CS_FMT_UNUSED; 
      fefew;
#endif
      pcol->ruby_type = RUBY_STR_TYPE; /* Treat it as String in ruby  */
      /* Allocate memory for the column string */
      pcol->svalue = (CS_CHAR *)mymalloc(pdata->maxlength);
    }
}

/*
   bind_columns(&REST maxcolumns=nil )
   maxcolumns  nil	bind all column
		0	bind nothing
               else    bind from 1 column to maxcolumns
  return
    Array of column name (It includes the column which is not done bind of)
     Qfalse	ERROR
  side effect
      set cmddata->colbuf[]
     */
static VALUE f_cmd_bind_columns (argc, argv, self) 
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  long maxcolumns = -1;	/* fetch max row size */
  SYB_COMMAND_DATA *cmddata;
  CS_COMMAND *cmd;
  CS_RETCODE	retcode; 
  CS_INT num_cols;
  CS_DATAFMT *pdata;
  int i;
  VALUE columns = Qnil;
  VALUE types = Qnil;

  SYB_COLUMN_DATA *pcol;
  
  VALUE bind_numeric2fixnum = Qfalse;
  
  bind_numeric2fixnum = rb_ivar_get(self, rb_intern("@bind_numeric2fixnum"));
  /* DBI patch:    do this here, in case we have a problem  */
  rb_ivar_set(self, rb_intern("@column_types"), types);
#ifdef SYB_DEBUG2
  printf("bind_numeric2fixnum=%d\n", bind_numeric2fixnum == Qtrue);
#endif

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    return Qfalse;
  cmd = cmddata->val;

  /* Retrieves number of columns  */
  retcode =
    ct_res_info(cmd, CS_NUMDATA, &num_cols, CS_UNUSED, NULL);
  if (retcode != CS_SUCCEED)
    goto equit;

  /* Sets maxcolumns by argument */
  if( (argc >= 1 ) && (argv[0] != Qnil ) )
    maxcolumns = (long)( FIX2INT( argv[0] ) );
  else
    maxcolumns = num_cols;
  /*
    No columns data ?
   */
  if (num_cols <= 0)
    goto equit;

  /* prepare for ct_bind buffer */
  cmd_colbuf_new(cmddata, (int)num_cols);
  types = rb_ary_new2( num_cols );

  pcol = cmddata->colbuf;

  for (i = 0; i < num_cols ; i++, ++pcol){
    /*
     */
    pdata = &(pcol->datafmt);
    retcode = ct_describe(cmd, (i + 1), pdata);
    if (retcode != CS_SUCCEED)
      goto equit;

    /* DBI patch by Charles 
     *   though there is a lot of other data, i'll just be happy with
     *   getting the column types */
    rb_ary_push( types, INT2FIX((int)cmddata->colbuf[i].datafmt.datatype) );

    /* Debug */
#ifdef SYB_DEBUG2
    if( (pdata->datatype == CS_NUMERIC_TYPE) ||
	(pdata->datatype == CS_DECIMAL_TYPE) ){
      printf("<%d> -- %s(%d %d.%d) (%d)\n",i+1, pdata->name,pdata->maxlength,
	     pdata->precision, pdata->scale, pdata->datatype);
    }
    else {
      printf("<%d> -- %s(%d) (%d)\n",i+1,pdata->name, pdata->maxlength, pdata->datatype);
    }
#endif

    if( i < maxcolumns ) {
      /* Change pdata,pcol to The format that I want to receive */
      set_bindfmt( pdata, pcol, (bind_numeric2fixnum == Qtrue) );
    
      /* Now bind. */
      {
	CS_VOID *bufp;
	bufp = pcol->svalue;
	if( pcol->ruby_type == RUBY_NUM_TYPE )
	  bufp = &(pcol->ivalue);
	else if( pcol->ruby_type == RUBY_FLOAT_TYPE )
	  bufp = &(pcol->fvalue);
#ifndef BIND_TIMESTAMP_AS_CHAR
	else if( pcol->ruby_type == RUBY_TIMESTAMP_TYPE )
	  bufp = &(pcol->timestamp);
#endif
	retcode = ct_bind(cmd, (i + 1), pdata,
			  bufp, &(pcol->valuelen),
			  &(pcol->indicator) );
	if (retcode != CS_SUCCEED)
	  goto equit;
	pcol->is_bind = 1;
      }
    }
  }

  /*
    columns ==> ryby array
   */
  num_cols = cmddata->len_colbuf;
  {
    char *p;
    columns = rb_ary_new2( num_cols );
    for(i=0; i<num_cols ; ++i){
      p = cmddata->colbuf[i].datafmt.name;
      if( p == NULL) p = "";
      rb_ary_push( columns, rb_str_new2(p) );
    }
  }
  rb_ivar_set(self, rb_intern("@column_types"), types); /* DBI patch  */
  return columns;
equit:
  cmd_colbuf_free(cmddata);
  return( Qfalse );
}

/*
  cmddata->colbuf[] convert to Ruby Array , as SYBASE ROWDATA
  */
static VALUE colbuf_to_rbarray(cmddata, strip)
     SYB_COMMAND_DATA *cmddata;
     int strip;   /* Fetch each String rows with stripped trailing
		     blanks */
{
  /*
    fetching RAW ==> ruby array
    */
  VALUE rows;
  CS_INT num_cols;
  int i;
  char *p;

  num_cols = cmddata->len_colbuf;
  /* initial rows_array */
  rows = rb_ary_new2( num_cols );

  for (i = 0; i < num_cols; i++){
    /* results type */
    int rtype = cmddata->colbuf[i].ruby_type;
    if( cmddata->colbuf[i].is_bind == 0 ){
      rb_ary_push(rows, Qnil);  	    /* Not bind */
    }
    else if( cmddata->colbuf[i].indicator == -1 ){
      rb_ary_push(rows, Qnil);  	    /* NULL fetch */
    }
    else if( rtype == RUBY_NUM_TYPE ){
      rb_ary_push(rows, INT2NUM( (int)(cmddata->colbuf[i].ivalue) ));
    }
    else if( rtype == RUBY_FLOAT_TYPE ){
      rb_ary_push(rows,
		  rb_float_new((double)(cmddata->colbuf[i].fvalue)) );
    }
#ifndef BIND_TIMESTAMP_AS_CHAR
    else if( rtype == RUBY_TIMESTAMP_TYPE ){
      rb_ary_push(rows,
		  rb_str_new(cmddata->colbuf[i].timestamp,
			     sizeof(cmddata->colbuf[i].timestamp)) );
    }
#endif
    else {
      p = cmddata->colbuf[i].svalue;
      if( cmddata->colbuf[i].datafmt.format == CS_FMT_NULLTERM ){
	if( p == NULL )p = "";
	if( strip != 0 )
	  strip_tail(p);
	rb_ary_push( rows, rb_str_new2(p) ); 
      }
      else {  /* image data */
	if( p == NULL )
	  rb_ary_push( rows, Qnil );  /* distinguish between "" and null */
	else
	  rb_ary_push( rows, 
		       rb_str_new(p,cmddata->colbuf[i].valuelen) ); 
      }
    }
  }
  return rows;
}

/*
   fetch( &REST strip)
   fetch 1 row
   CAUTION! 
       you must call bind_columns(), before fetch() call
   strip:	Fetch each String rows with stripped trailing blanks

   retrun (status, row) = [fettch-status, row-array]
      or  return FALSE, or return Nil

   SEE-ja-txt
     If (status, row ) = obj.fetch(..) , and obj.fetch returns FALSE,
       returns Array [status=false, row=nil]  This is features of ruby.
     row-array	rows data array, or Nil
                ( unbind column value ==>  NIL )
     fetch-status
       false		SybCommand initialize ERROR
       CS_END_DATA	No row available (CS_END_DATA) or Not binded
       CS_SUCCEED	OK , countinue
       CS_ROW_FAIL	recoverable error
       CS_FAIL, CS_CANCELED  any ERROR
       CS_PENDING, CS_BUSY   
     */
static VALUE f_cmd_fetch (argc, argv, self) 
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  int strip = 0;
  SYB_COMMAND_DATA *cmddata;
  CS_COMMAND *cmd;
  CS_RETCODE	retcode; 
  CS_INT rows_read;
  VALUE ret;
  VALUE row = Qnil;

  if( (argc >= 1 ) && (argv[0] == Qtrue ) )
    strip = 1;
  
  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    return Qfalse;
  if( (cmd = cmddata->val) == NULL )
    return Qfalse;

  if( cmddata->colbuf == NULL ){
    /* return Qfalse;  */
    return Qnil;	/* Not binded or no fetch data */
  }
  retcode = ct_fetch(cmd, CS_UNUSED, CS_UNUSED, CS_UNUSED, &rows_read);
  IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_FETCH ) ;
  if( (retcode == CS_SUCCEED) || (retcode == CS_ROW_FAIL) ){
    /* colbuf convert to Ruby Array */
    row =  colbuf_to_rbarray(cmddata, strip);
  }
  ret = rb_ary_new2(2);
  rb_ary_push( ret, INT2NUM( retcode) );
  rb_ary_push( ret, row );
  return ret;
}

/*
   fetchall( &REST maxrows , strip)
   bind all columns and fetch all row
   strip:	Fetch each String rows with stripped trailing blanks
   retrun 
     array
       [0]	columns name array
       [2]	rows data array of array
     Qfalse
       error occurred
   CAUTION!
     Cannot use Client-Library cursor.
       */
static VALUE f_cmd_fetchall (argc, argv, self) 
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  long maxrows = 0L;	/* fetch max row size */
  int strip = 0;
  long row_count;
  SYB_COMMAND_DATA *cmddata;
  CS_COMMAND *cmd;
  CS_RETCODE	retcode; 
  CS_INT rows_read;
  VALUE results = Qnil;
  VALUE rows; 
  VALUE rows_array = Qnil;
  VALUE columns = Qnil;

  if( (argc >= 1 ) && (argv[0] != Qnil ) )
    maxrows = (long)( FIX2INT( argv[0] ) );

  if( (argc >= 2 ) && (argv[1] == Qtrue ) )
    strip = 1;

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    return Qfalse;
  cmd = cmddata->val;
  
  /*
    bind all columns, and get column-name array to 'columns'
    */
  columns = f_cmd_bind_columns(0,NULL,self);
  if( columns == Qfalse )goto equit;

  /* initial rows_array */
  rows_array = rb_ary_new2( SYB_INITIAL_ROWSIZE );

  row_count = 0;

  /*
    Fetch
   */
  /*
   while (((retcode = ct_fetch(cmd, CS_UNUSED, CS_UNUSED, CS_UNUSED,
			      &rows_read)) == CS_SUCCEED) ||
	 (retcode == CS_ROW_FAIL))
    {
  */
  for( ; ; )
    {
      retcode = ct_fetch(cmd, CS_UNUSED, CS_UNUSED, CS_UNUSED,
			 &rows_read);
      IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_FETCH ) ;
      if( (retcode != CS_SUCCEED) && (retcode != CS_ROW_FAIL) )
	break;

      /* debug */
      /* fprintf(stderr,"rows_read=%d\n", (int)(rows_read)); */

      /* row_count = row_count + rows_read; */
      ++row_count;

      /*
       ** Check ERROR
       */
      if (retcode == CS_ROW_FAIL){
#ifdef SYB_DEBUG
	fprintf(stderr,"CS_ROW_FAIL come!\n");
#endif
	rb_ary_push( rows_array, Qnil );
      }
      else {
	/*
	  fetching RAW ==> ruby array
	 */
	rows = colbuf_to_rbarray(cmddata, strip );
	rb_ary_push(rows_array, rows );
      }

      row_count++;

      /* fetch_size exceed ? */
      if( (maxrows > 0) && (row_count >= maxrows) ){
	CS_RETCODE code;
	code = ct_cancel(NULL,cmd,CS_CANCEL_CURRENT);
	IOWAIT( code, &(cmddata->ioinfo), cmddata->conn, CT_CANCEL ) ;
	retcode = CS_END_DATA;
	break;
      }
    }
  
  /*
   ** Free allocated space.
   */
  cmd_colbuf_free( cmddata );

  /*
    Check last ct_fetch()'s return value
   */
  switch ((int)retcode){
  case CS_END_DATA:
    /* Returns [ columns, rows_array ]  */
    results = rb_ary_new2( 2 );
    /* push columns to results */
    rb_ary_push( results, columns);
    /* push rows_array to results */
    rb_ary_push( results, rows_array);
    return results;
    break;
  case CS_FAIL:
    goto equit;
  default:
    goto equit;
  }
  
equit:
  if( cmd != NULL ){
    CS_RETCODE code;
    code = ct_cancel(NULL, cmd, CS_CANCEL_CURRENT);
    IOWAIT( code, &(cmddata->ioinfo), cmddata->conn, CT_CANCEL ) ;
  }

  cmd_colbuf_free(cmddata);
  return( Qfalse );
}

/*
  Retrieves IODESC with ct_data_info() API 
  get_iodesc( item )
  item		column id
  return  
      SybIODesc		OK
      CS_FAIL, CS_BUSY	ERROR
 */
static VALUE f_cmd_get_iodesc(self, item)
     VALUE self;
     VALUE item;
{
  SYB_COMMAND_DATA *cmddata;
  CS_COMMAND *cmd;
  CS_RETCODE	retcode; 
  CS_IODESC iodesc;
  SYB_IODESC_DATA *iodesc_data;
  VALUE obj;

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    return Qfalse;
  cmd = cmddata->val;
  
  retcode = ct_data_info(cmd, CS_GET, (CS_INT)( NUM2INT( item) ),
			 &iodesc );
  if( retcode != CS_SUCCEED )
    return INT2NUM( retcode );

  obj = Data_Make_Struct
    (cSybIODesc, SYB_IODESC_DATA, 0, myfree, iodesc_data);
  memcpy( &(iodesc_data->body) , &(iodesc), sizeof( CS_IODESC ) );
  return obj;
}

/*
  Sets IO descriptor with ct_data_info() API.
  put_iodesc( iodesc )
  iodesc	SybIODesc object
  return  
      CS_SUCCEED	OK
      CS_FAIL, CS_BUSY	ERROR
 */
static VALUE f_cmd_set_iodesc(self, iodesc_obj)
     VALUE self;
     VALUE iodesc_obj;
{
  SYB_COMMAND_DATA *cmddata;
  CS_COMMAND *cmd;
  CS_RETCODE	retcode; 
  SYB_IODESC_DATA *desc;

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    return Qfalse;
  cmd = cmddata->val;

  Data_Get_Struct(iodesc_obj, SYB_IODESC_DATA, desc);
  
  retcode = ct_data_info(cmd, CS_SET, CS_UNUSED, &(desc->body) );
  return INT2NUM( retcode );
}


/*
  Reads a chunk of DATA with ct_get_data() API 
  get_data( item, fetchsize )
  item		column id
  fetchsize	data transfer size in bytes (0 means retrieving iodesc)
  return  (status, buffer) = [integer, Strings]
      buffer
	String		   The data that is retried
 	String.size	   The data size that is retried
        Nil		   No data reads
      status
	CS_END_ITEM ( find last chunk)
	CS_END_DATA ( find last chunk, and came to last column)
	CS_SUCCEED  ( OK, continue )
	CS_FAIL, CS_CANCELED, CS_PENDING,CS_BUSY --- any error	
 */
static VALUE f_cmd_get_data(self, item, fetchsize)
     VALUE self;
     VALUE item;
     VALUE fetchsize;
{
  CS_INT ncol, buflen, outlen;
  unsigned char *buf = NULL;
  SYB_COMMAND_DATA *cmddata;
  CS_COMMAND *cmd;
  CS_RETCODE	retcode; 
  VALUE results;

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    return Qfalse;
  cmd = cmddata->val;
  
  buflen = NUM2INT( fetchsize );
  ncol = NUM2INT( item);
  if( buflen > 0 )
    buf = (unsigned char *)mymalloc( buflen );
  else
    buf = (unsigned char *)mymalloc( 32 );	/* 32 ?? */
  retcode = ct_get_data(cmd, ncol, (CS_VOID *)buf, buflen, &outlen);
  IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_GET_DATA ) ;  

  results = rb_ary_new2( 2 );
  rb_ary_push( results, INT2NUM((int)retcode) );
  if( outlen > 0 )
    rb_ary_push( results, rb_str_new(buf, outlen) );
  else
    rb_ary_push( results, Qnil );
  
  if( buf != NULL ) free( buf );
  return results;
}

/*
  Sends a chunk of DATA with ct_send_data() API 
  .send_data( data )
  data	     Sending data (String)
			 Nil means 0 size data to send
  return  OK:     CS_SUCCEED
          ERROR:  CS_FAIL, CS_CANCELED, CS_PENDING,CS_BUSY
  CAUTION!
  send_data raise ERROR when same connection has active COMMAND
 */
static VALUE f_cmd_send_data(self, data)
     VALUE self;
     VALUE data;
{
  CS_INT buflen;
  unsigned char *buf;
  SYB_COMMAND_DATA *cmddata;
  CS_COMMAND *cmd;
  CS_RETCODE	retcode = CS_SUCCEED; 

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL )
    return Qfalse;
  cmd = cmddata->val;
  
  if( data == Qnil ){
    char dmy[32];
    retcode = ct_send_data(cmd, (CS_VOID *)dmy, 0);
    IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_SEND_DATA ) ;
  }
  else {
    buf = MY_RB_STR2CSTR( data, (long *)(&buflen) );
    if( (buf != NULL) && (buflen > 0) ){
      retcode = ct_send_data(cmd, (CS_VOID *)buf, buflen);
      IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_SEND_DATA ) ;
    }
  }
  return  INT2NUM((int)retcode);
}

/*
 * Is cursor?
 * cstat	cursor status (for output)
 */
static int is_cursor( cmddata, cstat )
  SYB_COMMAND_DATA *cmddata;
  CS_INT *cstat;
{
  CS_RETCODE retcode;
  *cstat = CS_CURSTAT_NONE;
  if( cmddata->status !=  SYBSTATUS_CURSOR_DECLARED )
    return 0;
  /* Check CURSOR STATUS */
  retcode = ct_cmd_props(cmddata->val, CS_GET, CS_CUR_STATUS,
			 cstat, CS_UNUSED, NULL );
  if( retcode != CS_SUCCEED ){
#ifdef SYB_DEBUG
    fprintf(stderr,"ERR: is_cursor %d\n", (int)retcode);
#endif
    return 0;
  }
  
  if( *cstat == CS_CURSTAT_NONE ) return 0;

/*  if( CS_CURSTAT_NONE ){
    if( ((*cstat) & CS_CURSTAT_NONE) == CS_CURSTAT_NONE )
      return 0;
  }
  else {
    if( *cstat == CS_CURSTAT_NONE ) return 0;
  }
*/

  return 1;
}

/*
  cstat	cursor status
  return	if CS_SUCCEED:  OK
		else  : ERROR
*/
static CS_RETCODE cursor_cancel( cmddata, cstat) 
    SYB_COMMAND_DATA *cmddata;
    CS_INT cstat;
{
  CS_RETCODE retcode;
  CS_INT closetype;
  CS_INT closeopt;
  CS_INT res_type;	/* result type from ct_results */
  CS_RETCODE ret ;

  // Define close type
  if( (cstat & CS_CURSTAT_OPEN ) == CS_CURSTAT_OPEN ){
    closetype = CS_CURSOR_CLOSE;
    closeopt = CS_DEALLOC;
  }
  /*  else if( (cstat & CS_CURSTAT_DECLARED) == CS_CURSTAT_DECLARED ){  */
  else if( (cstat & CS_CURSTAT_DEALLOC) != CS_CURSTAT_DEALLOC ){
    closetype = CS_CURSOR_DEALLOC;
    closeopt = CS_UNUSED;
  }
  else {
#ifdef SYB_DEBUG
    fprintf(stderr,"ERR: BUG? in cursor_cancel , cstat=%ld\n",
	    cstat);
#endif
    return CS_SUCCEED;
  }
  /* Close cursor */
  retcode = ct_cursor(cmddata->val, closetype,
		      NULL, CS_UNUSED, NULL, CS_UNUSED, closeopt);
  if( retcode != CS_SUCCEED ){
#ifdef SYB_DEBUG
    fprintf(stderr,"ERR: CURSOR DEALLOC in cursor_cancel %ld %ld\n", 
	    retcode, closetype);
#endif
    return retcode;
  }

  retcode = ct_send(cmddata->val);
  IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_SEND ) ;
  if (retcode  != CS_SUCCEED)
    return retcode;
  
  ret = CS_SUCCEED;
  for( ; ; ){
    retcode = ct_results(cmddata->val, &res_type);
    IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_RESULTS ) ;
    if( retcode != CS_SUCCEED ) break;
    if( (res_type != CS_CMD_SUCCEED) &&
	(res_type != CS_CMD_DONE) && (res_type != CS_CMD_FAIL)){
#ifdef SYB_DEBUG
      fprintf(stderr,"ERR: Unexpected res type! in cursor_cancel %ld %ld\n", 
	      res_type, closetype);
#endif
      return CS_FAIL;
    }
    if( retcode == CS_CMD_FAIL )
      ret = CS_FAIL;
  }
  
  if (retcode == CS_END_RESULTS)
    return ret;
  return CS_FAIL;
} 

/**
 * Cancel a command, or Close a cursor if CS_COMMAND is cursor.
 * MyNote:
 *   (cond ((and has-command-cursor-p is-cursor-opend-p)
 *            (CURSOR-CLOSE) (CURSOR-DEALLOC)
 *            (if (not (equal type-of-cancel  CS_CANCEL_CURRENT))
 *                (return RETCODE))
 *		 ))
 *   (return (ct_cancel))
 */
static CS_RETCODE cmd_or_cursor_cancel( cmddata, type ) 
  SYB_COMMAND_DATA *cmddata;
  CS_INT type;
{
  CS_RETCODE retcode;
  CS_INT cstat;

  if( is_cursor( cmddata, &cstat ) ){
    retcode = cursor_cancel( cmddata, cstat );
    if( type != CS_CANCEL_CURRENT )
	return retcode;
  }  
  /* End of CURSOR cancel */
  retcode = ct_cancel(NULL, cmddata->val, type);
  IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_CANCEL ) ;
  return retcode;
}

/*
   cancel( &REST type )
   type default CS_CANCEL_CURRENT
   Success, return true
   error, return false
   no command, return nil
   */
static VALUE f_cmd_cancel (argc, argv, self) 
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  CS_INT type = CS_CANCEL_CURRENT;
  SYB_COMMAND_DATA *cmddata;

  if( (argc >= 1 ) && (argv[0] != Qnil ) )
    type = FIX2INT( argv[0] );

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  cmd_colbuf_free( cmddata);  /* NAI-HOU-GA-IIKA?  */
  if( cmddata->val != NULL ){
    CS_RETCODE retcode;
    if( ! ISALIVE(cmddata) )
      return Qfalse;	/* DEAD */

    /* retcode = cmd_or_cursor_cancel(cmddata, type ); */
    retcode = ct_cancel(NULL, cmddata->val, type); 
    IOWAIT( retcode, &(cmddata->ioinfo), cmddata->conn, CT_CANCEL ) ; 

    if( retcode != CS_SUCCEED )
      return Qfalse;

    else {
#ifdef SYB_DEBUG
      fprintf(stderr,"f_cmd_cancel\n");
#endif
      return Qtrue;
    }
  }
  return Qnil;
}
/*
  Gets property.
  getprop( property )
  ERROR: nil return
  */
static VALUE f_cmd_getprop(self, proptype )
     VALUE self;
     VALUE proptype;
{
  VALUE ret = Qnil;
  SYB_COMMAND_DATA *cmddata;
  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val != NULL ){
    ret = get_props(cmddata->val, NUM2INT( proptype),
		    ct_cmd_props);
  }
  return ret;
}

/*
  Set properties
  return	true/false
  */
static VALUE f_cmd_setprop(self, proptype,val )
     VALUE self;
     VALUE proptype;
     VALUE val;
{
  VALUE ret = Qfalse;
  SYB_COMMAND_DATA *cmddata;
  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val != NULL ){
    CS_RETCODE csret;
    csret = get_props(cmddata->val, NUM2INT( proptype), val,
		    ct_cmd_props);
    if( csret == CS_SUCCEED ) ret = Qtrue;
  }
  return ret;
}
/*
  Gets information about Results set
  res_info( type )
  ERROR :  nil return
  */
static VALUE f_cmd_res_info(self, type )
     VALUE self;
     VALUE type;
{
  CS_INT cs_type;
  SYB_COMMAND_DATA *cmddata;

  CS_BOOL booldata;
  CS_SMALLINT sintdata;
  CS_INT intdata;

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val == NULL ) return Qnil;

  cs_type = NUM2INT(type);
  switch(cs_type){
  case  CS_ROW_COUNT:
  case  CS_NUMDATA:
#ifndef FREETDS  /* freetds ct.c (ver 0.63 ) is not support yet */
  case  CS_TRANS_STATE:
  case  CS_CMD_NUMBER:
  case  CS_NUM_COMPUTES:
  case  CS_NUMORDERCOLS:
#endif
    if( ct_res_info(cmddata->val, cs_type, (CS_VOID *)&intdata, 
		    CS_UNUSED,NULL) != CS_SUCCEED )
      return Qnil;
    return( INT2NUM(intdata) );
  case CS_BROWSE_INFO:
    if( ct_res_info(cmddata->val, cs_type, (CS_VOID *)&booldata, 
		    CS_SIZEOF(booldata),NULL) != CS_SUCCEED )
      return Qnil;
    return( INT2NUM(intdata) );
  case  CS_MSGTYPE  :
    if( ct_res_info(cmddata->val, cs_type, (CS_VOID *)&sintdata, 
		    CS_SIZEOF(sintdata),NULL) != CS_SUCCEED )
      return Qnil;
    return( INT2FIX(sintdata) );
#ifdef CS_ORDERBY_COLS
  case CS_ORDERBY_COLS:
    /* Must orders.length <= 256  */
    {
      VALUE ret;
      CS_INT orders[256], norder;
      int i;
      if( ct_res_info(cmddata->val, CS_NUMORDERCOLS, (CS_VOID *)&norder,
		    CS_UNUSED,NULL) != CS_SUCCEED )
      return Qnil;
      if( (norder > 256) || (norder <= 0) )return Qnil;
      if( ct_res_info(cmddata->val, CS_ORDERBY_COLS, (CS_VOID *)orders,
		      CS_SIZEOF(orders),NULL) != CS_SUCCEED )
	return Qnil;
      ret = rb_ary_new2( norder );
      for(i=0 ; i<norder; ++i)
	rb_ary_push(ret, INT2NUM( orders[i] ) );
      return ret;
    }
#endif
  default:
    return Qnil;
  }
}

/*
  Starts cursor
  type		CS_CURSOR_ROWS, CS_CURSOR_OPEN, CS_CURSOR_CLOSE,
		CS_CURSOR_OPTION, CS_CURSOR_UPDATE, CS_CURSOR_DELETE
  name		String or Nil
  text		String or Nil
  opt		CS_FOR_UPDATE,CS_READ_ONLY  or Nil(CS_UNUSED)
		(CS_MORE, CS_END and CS_RESTORE_OPTION cannot used in CTlib
		version 10)
                When type is CS_CUR_ROWCOUNT , opt means cursor rowcount.
  return	true/false
  */
static VALUE f_cmd_cursor(self, type, name, text, opt)
     VALUE self;
     VALUE type, name, text, opt;
{
  char *namestr, *textstr;
  CS_INT namelen, textlen, optval;
  SYB_COMMAND_DATA *cmddata;

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( cmddata->val != NULL ){
    CS_RETCODE csret;

    if( name == Qnil ){
      namestr = NULL;  namelen = CS_UNUSED;
    }
    else {
      namestr = MY_STR2CSTR( name );
      namelen = CS_NULLTERM;
    }
    if( text == Qnil ){
      textstr = NULL; textlen = CS_UNUSED;
    }
    else {
      textstr = MY_STR2CSTR( text );
      textlen = CS_NULLTERM;
    }
    if( opt == Qnil ) optval = CS_UNUSED;
    else
      optval = (CS_INT)(NUM2INT( opt ) );

    csret = ct_cursor(cmddata->val, (CS_INT)(NUM2INT( type )),
		      namestr, namelen,  textstr, textlen,
		      optval );
    if( csret == CS_SUCCEED )
      return Qtrue;
  }
  return Qfalse;
}

/*
  Sets parameter for stoad procedure.

Sybtax:
  param( var, &REST, name, conv-string, isoutput, )

Arguments:
  var	parameter value for ct_param
   support for
      ruby data type	CTlib data type
      ---------------------------------------------------
      T_STRING		CS_CHAR_TYPE
      T_FIXNUM		CS_INT_TYPE 
      T_FLOAT		CS_FLOAT_TYPE
      nil		conv-string 

  name		Parameter name for ct_param (nil means no name)

  conv-string		Ctlib data type when nil pass as  NULL 
    Support for:
	  "CHAR"	CS_CHAR_TYPE
	  "INT"		CS_INT_TYPE
	  "FLOAT"	CS_FLOAT_TYPE

  The default is NULL of CS_CHAR_TYPE

  isoutput	Outpur parameter if true 

  ERROR:
	false return , or
	RuntimeError (syntax )
  */
#define RPC_PARAM_MAXSTRINGLEN 255
static VALUE f_cmd_param (argc, argv, self) 
     int argc;
     VALUE  *argv; 
     VALUE self;
{

  CS_INT ivar;
  CS_FLOAT  fvar;
  CS_CHAR *pname, *pconv;
  CS_VOID *pvar;

  CS_DATAFMT	datafmt;
  CS_INT indicator;
  CS_INT lvar;
  SYB_COMMAND_DATA *cmddata;  
  VALUE ret = Qfalse;
  VALUE varobj;
  int isoutput;

  if( argc < 1 ){
    rb_raise(rb_eArgError,"wrong # of arguments");
  }

  Data_Get_Struct(self, SYB_COMMAND_DATA, cmddata);
  if( (cmddata == NULL) || ( cmddata->val == NULL) )
    return ret;

  varobj = argv[0];
  pname = "";
  isoutput = 0;
  pconv = NULL;
  if( (argc >= 2 ) && (TYPE(argv[1]) == T_STRING ) )
    pname = MY_STR2CSTR( argv[1] );
  if( (argc >= 3) && (TYPE(argv[2]) == T_STRING ) ){
    pconv = MY_STR2CSTR( argv[2] );
  }
  if( argc >= 4 ){
    if( (argv[3] != Qfalse) && (argv[3] != Qnil) )
      isoutput = 1;
  }

  memset(&datafmt, 0, sizeof (datafmt));	/* in BSD bzero? */
  strncpy( datafmt.name, pname, CS_MAX_NAME - 1 );
  datafmt.name[CS_MAX_NAME - 1 ] = '\0';
  datafmt.namelen = CS_NULLTERM;
  if( isoutput ){
    datafmt.status = CS_RETURN;
    datafmt.maxlength = RPC_PARAM_MAXSTRINGLEN;
  }
  else {
    datafmt.status = CS_INPUTVALUE;
    datafmt.maxlength = CS_UNUSED;
  }
  datafmt.locale = NULL;
  datafmt.datatype = CS_CHAR_TYPE;
  indicator = CS_UNUSED;

  if( (TYPE(varobj) == T_FIXNUM) || (TYPE(varobj) == T_BIGNUM) ){
    datafmt.datatype = CS_INT_TYPE;
    ivar =  (CS_INT)( NUM2INT(varobj) );
    pvar = (CS_VOID *)(&ivar);
    lvar = CS_SIZEOF( CS_INT );
  }
  else if( TYPE(varobj) == T_FLOAT ){
    datafmt.datatype = CS_FLOAT_TYPE;
    fvar =  (CS_FLOAT)( NUM2DBL(varobj) );
    pvar = (CS_VOID *)(&fvar);
    lvar = CS_SIZEOF( CS_FLOAT );
  }
  else if( TYPE(varobj) == T_STRING ){
    datafmt.datatype = CS_CHAR_TYPE;
    pvar = (CS_CHAR *)(MY_STR2CSTR(varobj) );
    if( pvar == NULL ) pvar = "";
    lvar = strlen( pvar );
  }
  else if ( varobj == Qnil ){
    datafmt.datatype = CS_CHAR_TYPE;
    if( pconv ){
      if( strcmp(pconv, "INT") == 0 )
	datafmt.datatype = CS_INT_TYPE;
      else if ( strcmp(pconv, "FLOAT") == 0 )
	datafmt.datatype = CS_FLOAT_TYPE;
    }
    pvar = NULL;
    lvar = 0;
    indicator = -1;
  }
  else
    return ret;

  if(ct_param(cmddata->val, &datafmt, pvar, lvar, indicator)
     == CS_SUCCEED){
    ret = Qtrue;
  }
  return ret;
}
    
/*
  .props( &REST hash )
  If specifies HASH , Set SybIODesc member, and return current value
  as HASH.  --> SEE-ja-txt
  If NOT specifies HASH, return current SybIODesc member as HASH.

  Key of hash:
	'datatype' FIXNUM	CS_IMAGE_TYPE or CS_TEXT_TYPE
	'total_txtlen' NUMERIC	TEXT/IMAGE  total length (Byte)
	'log_on_update' TRUE/FALSE  use transation LOG?
	'name'  STRING	column name
	'timestamp' STRING	TEXT timestamp
	'textptr' STRING	Text Pointer
*/
static VALUE f_iodesc_props (argc, argv, self) 
     int argc;
     VALUE  *argv; 
     VALUE self;
{
  SYB_IODESC_DATA *desc;
  CS_IODESC *iodesc;
  VALUE hash;
  VALUE key_datatype, key_total_txtlen, key_log_on_update,
    key_name, key_timestamp, key_textptr;

  key_datatype = rb_str_new2("datatype");
  key_total_txtlen = rb_str_new2("total_txtlen");
  key_log_on_update = rb_str_new2("log_on_update");
  key_name = rb_str_new2("name");
  key_timestamp = rb_str_new2("timestamp");
  key_textptr = rb_str_new2("textptr");

  Data_Get_Struct(self, SYB_IODESC_DATA, desc);
  iodesc = &(desc->body);

  if( (argc > 0) && (TYPE(argv[0]) == T_HASH) ){
    char *str; long len;
    VALUE val;
    /* set iodesc props */
    hash = argv[0];
    val = rb_hash_aref( hash, key_datatype);
    if( FIXNUM_P(val) ) iodesc->datatype = (CS_INT)( NUM2INT( val ) );

    val = rb_hash_aref( hash, key_total_txtlen);
    if( (TYPE(val) == T_FIXNUM) || (TYPE(val) == T_BIGNUM) )
      iodesc->total_txtlen = (CS_INT)(NUM2INT( val ) );

    val = rb_hash_aref( hash, key_log_on_update);
    if( val == Qtrue ) iodesc->log_on_update = CS_TRUE;
    else if( val == Qfalse ) iodesc->log_on_update = CS_FALSE;

    val = rb_hash_aref( hash, key_name);
    if( TYPE(val) == T_STRING ){
      str = MY_RB_STR2CSTR(val, (long *)&len);
      if( len >= CS_OBJ_NAME ) len = CS_OBJ_NAME - 1;
      strncpy( iodesc->name, str, len );
      *(iodesc->name + len) = '\0';
      iodesc->namelen = len;
    }

    val = rb_hash_aref( hash, key_timestamp);
    if( TYPE(val) == T_STRING ){
      str = MY_RB_STR2CSTR(val, (long *)&len);
      if( len > CS_TS_SIZE ) len = CS_TS_SIZE;
      memcpy( iodesc->timestamp, str, len );
      iodesc->timestamplen = len;
    }

    val = rb_hash_aref( hash, key_textptr);
    if( TYPE(val) == T_STRING ){
      str = MY_RB_STR2CSTR(val, (long *)&len);
      if( len > CS_TP_SIZE ) len = CS_TP_SIZE;
      memcpy( iodesc->textptr, str, len );
      iodesc->textptrlen = len;
    }
  }

  /* get iodesc props */
  hash = rb_hash_new();
  rb_hash_aset(hash, key_datatype, INT2NUM(iodesc->datatype));
  rb_hash_aset(hash, key_total_txtlen, INT2NUM(iodesc->total_txtlen));
  rb_hash_aset(hash, key_log_on_update,
	       iodesc->log_on_update ? Qtrue : Qfalse);
  rb_hash_aset(hash, key_name, rb_str_new2(iodesc->name));
  rb_hash_aset(hash, key_timestamp,
	       rb_str_new(iodesc->timestamp, iodesc->timestamplen));
  rb_hash_aset(hash, key_textptr,
	       rb_str_new(iodesc->textptr, iodesc->textptrlen));
  return hash;
}

void Init_sybct()
{
  /* define modules */
  cSybConstant = rb_define_module("SybConstant");

  /* define classes */
  cSybContext = rb_define_class("SybContext", rb_cObject);
  cSybConnection = rb_define_class("SybConnection", rb_cObject);
  cSybCommand = rb_define_class("SybCommand", rb_cObject);
  cSybIODesc = rb_define_class("SybIODesc", rb_cObject);

  /* define SybConstant's constants */
  defconst_props( cSybConstant );		/* Property */
  defconst_restypes( cSybConstant );	/* result type */
  defconst_options( cSybConstant );	/* serverc options */
  defconst_etc( cSybConstant );	/* result type */
  defconst_coltype( cSybConstant ); /* column type */

  /* define SybCommand class member */
#ifdef RUBY_TAIOUHYOU
  {
    /* 
       Sets empty HASH to @current_objects (Lookup table: 
                      context pointer of C <==> SybContext object)
    */
    rb_ivar_set( cSybContext, rb_intern("@current_objects"),
		 rb_hash_new()); 
  }
#endif

  /* define SybContext methods */
  {
    /* create( &REST locname timeout-sec) */
    rb_define_singleton_method
      (cSybContext, "create", f_ctx_create, -1);

    /* destroy( &REST force ) */
    rb_define_method(cSybContext, "destroy", f_ctx_destroy, -1);

    /* getprop( property ) */
    rb_define_method(cSybContext, "getprop", f_ctx_getprop, 1); 
    /* setprop( property, val ) */
    rb_define_method(cSybContext, "setprop", f_ctx_setprop, 2); 

    /* Define default callback methods  */
    rb_define_method
      (cSybContext, "srvmsgCB", f_ctx_srvmsgCB, 2);
    rb_define_method
      (cSybContext, "cltmsgCB", f_ctx_cltmsgCB, 2);
  }

  /* define SybConnection methods */
  {
    /* new( SybContext ) */
    rb_define_singleton_method 
      (cSybConnection, "new", f_con_new, 1);
    /* connect( SybContext srv usr &REST psw appname) */
    rb_define_method 
      (cSybConnection, "connect", f_con_connect, -1);
    /* open( &REST locname timeout-sec) */
    /* rb_define_singleton_method 
      (cSybConnection, "open", f_con_open, -1); */

    /* close( &REST force ) */
    rb_define_method(cSybConnection, "close", f_con_close, -1); 

    /* delete( &REST force ) */
    rb_define_method(cSybConnection, "delete", f_con_delete, -1); 

    /* getprop( property ) */
    rb_define_method(cSybConnection, "getprop", f_con_getprop, 1); 
    /* setprop( property, val ) */
    rb_define_method(cSybConnection, "setprop", f_con_setprop, 2); 
    /* getopt( option ) */
    rb_define_method(cSybConnection, "getopt", f_con_getopt, 1); 
    /* setopt( option, val ) */
    rb_define_method(cSybConnection, "setopt", f_con_setopt, 2); 
  }

  /* define SybCommand methods */
  {
    /* new( conn, sql ) */
    rb_define_singleton_method 
      (cSybCommand, "new", f_cmd_new, -1);

    /* new_cursor( conn, curname, sql ) */
    rb_define_singleton_method 
      (cSybCommand, "cursor_new", f_cmd_cursor_new, -1);

    /* send( ) */
    rb_define_method(cSybCommand, "send", f_cmd_send, 0); 

    /* results( ) */
    rb_define_method(cSybCommand, "results", f_cmd_results, 0);  

    /* fetchall(&REST maxrows, strip ) */
    rb_define_method(cSybCommand, "fetchall", f_cmd_fetchall, -1);  

    /* fetch(&REST strip) */
    rb_define_method(cSybCommand, "fetch", f_cmd_fetch, -1);  

    /* get_data(item, fetchsize) */
    rb_define_method(cSybCommand, "get_data", f_cmd_get_data, 2);  
    /* send_data(data) */
    rb_define_method(cSybCommand, "send_data", f_cmd_send_data, 1);  

    /* get_iodesc(item) */
    rb_define_method(cSybCommand, "get_iodesc", f_cmd_get_iodesc, 1);  
    /* set_iodesc(SybIODesc-obj) */
    rb_define_method(cSybCommand, "set_iodesc", f_cmd_set_iodesc, 1);  

    /* bind_columns(&REST maxcolumns=nil ) */
    rb_define_method(cSybCommand, "bind_columns", f_cmd_bind_columns, -1);  

    /* cancel(type ) */
    rb_define_method(cSybCommand, "cancel", f_cmd_cancel, -1);  

    /* close( &REST force ) */
    rb_define_method(cSybCommand, "delete", f_cmd_delete, 0); 

    /* getprop( property ) */
    rb_define_method(cSybCommand, "getprop", f_cmd_getprop, 1); 
    /* setprop( property, val ) */
    rb_define_method(cSybCommand, "setprop", f_cmd_setprop, 2); 
    /* param(var, name, &REST isoutput, conv-string-when-var-is-nil) */
    rb_define_method(cSybCommand, "param", f_cmd_param, -1); 

    /* res_info( type) */
    rb_define_method(cSybCommand, "res_info", f_cmd_res_info, 1); 

    /* cursor(type, name, text, option ) */
    rb_define_method(cSybCommand, "cursor", f_cmd_cursor, 4); 

  }

  /* define SybIODesc methods */
  {
    /* props(&REST, hash ) */
    rb_define_method(cSybIODesc, "props", f_iodesc_props, -1); 
  }

}










/*
   CALL BACKS
   */

/* The function getting CS_USERDATA */
static void get_userdata( context, connection, udata )
  CS_CONTEXT *context;
  CS_CONNECTION *connection;
  SYB_CALLBACK_USERDATA *udata;
{
    VALUE ctxobj;
    CS_INT len;
    CS_RETCODE retcode;
    SYB_CALLBACK_USERDATA udbuf;

    udata->ctxobj = Qnil;
    udata->conobj = Qnil;

    if( connection != NULL ){
      retcode = ct_con_props(connection, CS_GET, CS_USERDATA, &udbuf,
			     sizeof(udbuf), &len);
      if( (retcode == CS_SUCCEED) && (len == sizeof(udbuf)) ){
	/* printf("XX len=%d\n",(int)len); */
	udata->ctxobj = udbuf.ctxobj;
	udata->conobj = udbuf.conobj;
	
	if( udata->ctxobj != Qnil )
	  return;
      }
    }

    if( context != NULL ){
      retcode = cs_config(context, CS_GET, CS_USERDATA, &ctxobj,
			  sizeof(ctxobj) , &len);
      if( (retcode == CS_SUCCEED) && (len == sizeof(ctxobj)))
	udata->ctxobj = ctxobj;
    }
    return;
}

/* CS_RETCODE CS_PUBLIC syb_clientmsg_cb(context, connection, errmsg) */
/*
  SEE-ja-txt
  cltmsgCB ( the ruby method for client message ) must return the
  following of one
    FALSE		The connection is DEAD with CS_FAI
    others	Continue with CS_SUCCEED

  By my taste, only when CS_SV_RETRY_FAIL(timeout), return FALSE
*/		
static CS_RETCODE syb_clientmsg_cb(context, connection, errmsg)
     CS_CONTEXT	*context;
     CS_CONNECTION	*connection;	
     CS_CLIENTMSG	*errmsg;
{
  SYB_CALLBACK_USERDATA udbuf;
  VALUE funret = Qnil;

/*
#ifndef SYB_DEBUG
  if( CS_SEVERITY(errmsg->msgnumber ) == CS_SV_INFORM)
      return CS_SUCCEED;
#endif
*/
  get_userdata(context, connection, &udbuf);

#ifdef SYB_DEBUG
  /* debug */
  if( udbuf.ctxobj == Qnil ) fprintf(stderr,"client CB ctxobj is nil\n");
  else
    fprintf(stderr, "client CB ctxobj is %lx\n",
	    (unsigned long)(udbuf.ctxobj) );
  if( udbuf.conobj == Qnil ) fprintf(stderr,"client CB conobj is nil\n");
  else
    fprintf(stderr, "client CB conobj is %lx\n",
	    (unsigned long)(udbuf.conobj) );
#endif

  if( udbuf.ctxobj != Qnil ){
    VALUE hash;
    hash = rb_hash_new();

    /* message */
    rb_hash_aset(hash, rb_str_new2("msgstring"),
		 rb_str_new( errmsg->msgstring, errmsg->msgstringlen));

    /* OS message */
    rb_hash_aset(hash, rb_str_new2("osstring"),
		 rb_str_new( errmsg->osstring, errmsg->osstringlen));

    /* SQL message  */
    if( errmsg->sqlstatelen > 0 )
      rb_hash_aset(hash, rb_str_new2("sqlstate"),
		   rb_str_new( errmsg->sqlstate, errmsg->sqlstatelen));

    /* status */
    {
      char *status = "";
#ifdef CS_FIRST_CHUNK
      if( errmsg->status | CS_FIRST_CHUNK ){
	if( errmsg->status | CS_LAST_CHUNK )
	  status = "both";
	else
	  status = "first";
      }
      else if( errmsg->status | CS_LAST_CHUNK )
	status = "last";
#endif
      rb_hash_aset(hash, rb_str_new2("status"),
		   rb_str_new2(status) );
    }

      
    /* message number */
    rb_hash_aset(hash, rb_str_new2("msgnumber"),
		 INT2NUM((int)(errmsg->msgnumber)) );

    /* Information for msgnumber   */
    {
      char *severity;
      /* CTlib layer that support this message */
      rb_hash_aset(hash, rb_str_new2("layer"),
		   INT2FIX((int)( CS_LAYER(errmsg->msgnumber) ) )  );
      /* The origin of message ( Hassei basho ) */
      rb_hash_aset(hash, rb_str_new2("origin"),
		   INT2FIX((int)( CS_ORIGIN(errmsg->msgnumber) ) )  );
      /* message number */
      rb_hash_aset(hash, rb_str_new2("number"),
		   INT2FIX((int)( CS_NUMBER(errmsg->msgnumber) ) )  );
      /*  severity (JUUDAIDO)  */
      switch(CS_SEVERITY(errmsg->msgnumber)){
      case CS_SV_INFORM :		/* Not ERROR */
	severity = NULL;
	break;
      case CS_SV_CONFIG_FAIL :	/* SyBase configuration error  */
	severity = "CONFIG_FAIL";
	break;
      case CS_SV_RETRY_FAIL :	/* fail retry, perhaps TIMEOUT */
	severity = "RETRY_FAIL";
	break;
      case CS_SV_API_FAIL :	/* Probably mistaku sentence (SAIRIYOU KANOU) */
	severity = "API_FAIL";
	break;
      case CS_SV_RESOURCE_FAIL :  /* Probably less resources  (SAIRIYOU KANOU) */
	severity = "RESOIRCE_FAIL";
	break;
      case CS_SV_COMM_FAIL :	/* comminucation error (Need ct_close) */
	severity = "COMM_FAIL";
	break;
      case CS_SV_INTERNAL_FAIL :	/* Internal error (Need ct_exit)  */
	severity = "INTERNAL_FAIL";
	break;
      case CS_SV_FATAL :		/* Fatal ERROR (Need ct_exit )  */
	severity = "FATAL";
	break;
      default:			/* unknown error */
	severity = "unknown";
	break;
      }
      if( severity != NULL ) 
	rb_hash_aset(hash, rb_str_new2("severity"),
		     rb_str_new2(severity) );
      else
	rb_hash_aset(hash, rb_str_new2("severity"), Qnil);
    }

    /* 
       SEE-ja-txt
       !!!  Not raise exceptions in the Ruby callbacks method !!!!
    */
    /* 
       rb_funcall(rb_mGC,rb_intern("disable"),0); 
       DEFER_INTS; // when change thread in callback 
       */
    GUARD ;  /* when change thread in callback */ 
    funret = rb_funcall( udbuf.ctxobj, rb_intern("cltmsgCB"),
			 2, udbuf.conobj, hash);
    /* 
       ALLOW_INTS; 
       rb_funcall(rb_mGC,rb_intern("enable"),0); 
       */
    UNGUARD ;

  }
  else
    rb_warning("CS_CONTEXT is Nil, So I can not call 'cltmsgCB()'\n");

  if( funret == Qfalse )
    return CS_FAIL;

  return CS_SUCCEED;
}


/* CS_RETCODE CS_PUBLIC syb_servermsg_cb(context, connection, srvmsg) */
static CS_RETCODE syb_servermsg_cb(context, connection, srvmsg)
     CS_CONTEXT	*context;
     CS_CONNECTION	*connection;
     CS_SERVERMSG	*srvmsg;
{
  SYB_CALLBACK_USERDATA udbuf;
  
/*
#ifndef SYB_DEBUG
  if( (srvmsg->severity == 10) || (srvmsg->severity == 0 ))
    return CS_SUCCEED;
#endif
*/
  get_userdata(context, connection, &udbuf);

#ifdef SYB_DEBUG
  /* debug */
  if( udbuf.ctxobj == Qnil ) fprintf(stderr,"server CB ctxobj is nil\n");
  else
    fprintf(stderr, "server CB ctxobj is %lx\n",
	    (unsigned long)(udbuf.ctxobj) );
  if( udbuf.conobj == Qnil ) fprintf(stderr,"server CB conobj is nil\n");
  else
    fprintf(stderr, "server CB conobj is %lx\n",
	    (unsigned long)(udbuf.conobj) );
#endif

  if( udbuf.ctxobj != Qnil ){
    VALUE hash;
    hash = rb_hash_new();
    rb_hash_aset(hash, rb_str_new2("msgnumber"),
		 INT2NUM((int)(srvmsg->msgnumber)) );
    rb_hash_aset(hash, rb_str_new2("severity"),
		 INT2NUM((int)(srvmsg->severity)) );
    rb_hash_aset(hash, rb_str_new2("state"),
		 INT2NUM((int)(srvmsg->state)) );
    rb_hash_aset(hash, rb_str_new2("line"),
		 INT2NUM((int)(srvmsg->line)) );
    rb_hash_aset(hash, rb_str_new2("status"),
		 INT2NUM((int)(srvmsg->status)) );
    rb_hash_aset(hash, rb_str_new2("srvname"),
		 rb_str_new( srvmsg->svrname, srvmsg->svrnlen));
    rb_hash_aset(hash, rb_str_new2("text"),
		 rb_str_new( srvmsg->text, srvmsg->textlen));
    rb_hash_aset(hash, rb_str_new2("proc"),
		 rb_str_new( srvmsg->proc, srvmsg->proclen));
    rb_hash_aset(hash, rb_str_new2("sqlstate"),
		 rb_str_new( srvmsg->sqlstate, srvmsg->sqlstatelen));

    /* 
       SEE-ja-txt
       !!!  Not raise exceptions in the Ruby callbacks method !!!!
    */
    /* 
       rb_funcall(rb_mGC,rb_intern("disable"),0); 
       DEFER_INTS; // when change thread in callback 
       */
    GUARD ; /* when change thread in callback */ 
    rb_funcall( udbuf.ctxobj, rb_intern("srvmsgCB"),
		2, udbuf.conobj, hash);
    UNGUARD;

    /* 
       ALLOW_INTS; 
       rb_funcall(rb_mGC,rb_intern("enable"),0); 
       */
  }
  else
    rb_warning("CS_CONTEXT is Nil, So I can not call 'srvmsgCB()'\n");

  return CS_SUCCEED;
}
