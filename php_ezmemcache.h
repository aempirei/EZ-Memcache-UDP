#ifndef PHP_EZMEMCACHE_H
#define PHP_EZMEMCACHE_H 1

#define PHP_EZMEMCACHE_VERSION "1.1"
#define PHP_EZMEMCACHE_EXTNAME "ezmemcache"

#define PHP_EZMEMCACHE_DEBUGLOG 0

#ifdef ZTS
#include "TSRM.h"
#endif

/* ezmemcache descriptor */

typedef struct _php_ezmc_ezd {
   int sd;
   struct sockaddr *sa;
   socklen_t sl;
} php_ezmc_ezd;

/*
 * in contrast to intuition and usual programming paradigms,
 * mindless trends, and dogma about the 'right way' to code
 * global variables are chosen over local variables to reduce
 * unnecessary repetition of declarations in memcache methods
 */

ZEND_BEGIN_MODULE_GLOBALS(ezmemcache)

   u_int16_t id;
   u_int16_t default_port;
   u_int16_t port;

   char *server;
   int server_len;

   int payload_max;

   char *send_buf;
   int send_len;

   char *recv_buf;
   int recv_len;

   char *key;
   int key_len;

   char *value;
   int value_len;

   long exptime;
   long flags;

   php_ezmc_ezd ezd;
   php_ezmc_ezd *ezdp;
   zval *zezd;

ZEND_END_MODULE_GLOBALS(ezmemcache)

#ifdef ZTS
#define EZG(v) TSRMG(ezmemcache_globals_id, zend_ezmemcache_globals *, v)
#else
#define EZG(v) (ezmemcache_globals.v)
#endif

#define PHP_EZMC_EZD_RES_NAME "ezmemcache descriptor"

PHP_MINIT_FUNCTION(ezmemcache);
PHP_MSHUTDOWN_FUNCTION(ezmemcache);
PHP_RINIT_FUNCTION(ezmemcache);
PHP_MINFO_FUNCTION(ezmemcache);

PHP_FUNCTION(ezmemcache_open);
PHP_FUNCTION(ezmemcache_close);
PHP_FUNCTION(ezmemcache_raw);
PHP_FUNCTION(ezmemcache_get);
PHP_FUNCTION(ezmemcache_set);
PHP_FUNCTION(ezmemcache_add);
PHP_FUNCTION(ezmemcache_replace);
PHP_FUNCTION(ezmemcache_delete);
PHP_FUNCTION(ezmemcache_inc);
PHP_FUNCTION(ezmemcache_dec);
PHP_FUNCTION(ezmemcache_info);

extern zend_module_entry ezmemcache_module_entry;
#define phpext_ezmemcache_ptr &ezmemcache_module_entry

#endif
