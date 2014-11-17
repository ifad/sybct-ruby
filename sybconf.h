
#undef RUBY_TAIOUHYOU

/* DATETIME always  "yyyy/mm/dd hh:mm:ss" */
#define USE_MY_DATE2STR
#ifdef FREETDS
#undef USE_MY_DATE2STR
#endif

/* bind tymestamp column as CS_CHAR_TYPE */
#define BIND_TIMESTAMP_AS_CHAR

#undef USE_MEMPOOL
