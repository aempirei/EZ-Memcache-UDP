#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>

#include "php.h"
#include "php_ini.h"
#include "php_ezmemcache.h"

int le_ezmc_ezd;

ZEND_DECLARE_MODULE_GLOBALS(ezmemcache)

static function_entry ezmemcache_functions[] = {
   PHP_FE(ezmemcache_open, NULL)
   PHP_FE(ezmemcache_close, NULL)
   PHP_FE(ezmemcache_raw, NULL)
   PHP_FE(ezmemcache_get, NULL)
   PHP_FE(ezmemcache_set, NULL)
   PHP_FE(ezmemcache_add, NULL)
   PHP_FE(ezmemcache_replace, NULL)
   PHP_FE(ezmemcache_delete, NULL)
   PHP_FE(ezmemcache_inc, NULL)
   PHP_FE(ezmemcache_dec, NULL)
   PHP_FE(ezmemcache_info, NULL)
   {NULL, NULL, NULL}
};

zend_module_entry ezmemcache_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
   STANDARD_MODULE_HEADER,
#endif
   PHP_EZMEMCACHE_EXTNAME,
   ezmemcache_functions,
   PHP_MINIT(ezmemcache),
   PHP_MSHUTDOWN(ezmemcache),
   PHP_RINIT(ezmemcache),
   NULL,
   PHP_MINFO(ezmemcache),
#if ZEND_MODULE_API_NO >= 20010901
   PHP_EZMEMCACHE_VERSION,
#endif
   STANDARD_MODULE_PROPERTIES
};

PHP_INI_BEGIN()
PHP_INI_ENTRY("payload.max", "1024", PHP_INI_ALL, NULL)
PHP_INI_ENTRY("default.port", "11211", PHP_INI_ALL, NULL)
PHP_INI_END()

#ifdef COMPILE_DL_EZMEMCACHE
ZEND_GET_MODULE(ezmemcache)
#endif

static void php_ezmemcache_init_globals(zend_ezmemcache_globals *ezmemcache_globals)
{
}

static void php_ezmc_ezd_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
   php_ezmc_ezd *ezdp = (php_ezmc_ezd *)rsrc->ptr;

   if (ezdp) {
      if(ezdp->sa)
         efree(ezdp->sa);
      efree(ezdp);         
   }
}

PHP_MINIT_FUNCTION(ezmemcache)
{
   le_ezmc_ezd = zend_register_list_destructors_ex(php_ezmc_ezd_dtor, NULL, PHP_EZMC_EZD_RES_NAME, module_number); 
   REGISTER_INI_ENTRIES();
   ZEND_INIT_MODULE_GLOBALS(ezmemcache, php_ezmemcache_init_globals, NULL);
   openlog("ezmemcache", LOG_PID, LOG_USER);
   return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(hello)
{
    /* TODO: it would be good to interate through
     * all opened objects and close them so as to
     * not leak desriptors */
    return SUCCESS;
}

PHP_RINIT_FUNCTION(ezmemcache)
{
   srand(getpid() + time(NULL));

   EZG(payload_max) = INI_INT("payload.max");
   EZG(default_port) = INI_INT("default.port");
   EZG(id) = rand();

   return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(ezmemcache)
{
   UNREGISTER_INI_ENTRIES();
   closelog();
   return SUCCESS;
}

PHP_MINFO_FUNCTION(ezmemcache)
{
   php_info_print_table_start();
   php_info_print_table_header(2, "ezmemcache support", "enabled");
   php_info_print_table_row(2, "Version", PHP_EZMEMCACHE_VERSION);
   php_info_print_table_end();

   DISPLAY_INI_ENTRIES();
}

/*
 * memcache udp header
 */

struct mcudphdr {
   u_int16_t id;
   u_int16_t seq;
   u_int16_t cnt;
   u_int16_t zero;
} __attribute__ ((__packed__));

/*
 * pack sockaddr_in structure with server/port information
 * =1 success   =0 failure
 */

static int php_ezmc_packaddr(struct sockaddr_in *sa, const char *server, const u_int16_t port) {

   memset(sa, 0, sizeof(*sa));

   sa->sin_family = AF_INET;
   sa->sin_port = htons(port);      

   /* FIXME: this shit doesnt actually support anything other than ip addresses */

   if(inet_aton(server, &(sa->sin_addr)) == 0)
      return 0;

   return 1;
}

/*
 * build a connection to the target address
 * >-1 success   =-1 failure
 */

static int php_ezmc_conn(const struct sockaddr *sa, socklen_t sl) {

   int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if(sd == -1)
      return -1;

   if(connect(sd, sa, sl) == -1) {
      close(sd);
      return -1;
   }      

   return sd;
}

/*
 * send payload on the given udp socket, given a command, prepend the udp header and send entire header+cmd payload
 * =1 success   =0 failure
 */

static int php_ezmc_send(int sd, const char *cmd, size_t cmd_len) {

   ssize_t sz;
   struct mcudphdr hdr;
   void *payload = NULL;
   size_t payload_len;

   /* set memcache header information */

   hdr.id = htons(EZG(id));
   hdr.seq = 0;
   hdr.cnt = htons(1);
   hdr.zero = 0;

   /* alloc payload storage */

   payload_len = cmd_len + sizeof(struct mcudphdr);
   if(payload_len > EZG(payload_max))
      goto send_fail;

   payload = emalloc(payload_len);
   if(payload == NULL)
      goto send_fail;

   /* build the payload (header+data) */

   memcpy(payload, &hdr, sizeof(hdr));
   memcpy((char *)payload + sizeof(hdr), cmd, cmd_len);

   /* send to server while we keep getting interrupted */

   do {
      sz = send(sd, payload, payload_len, 0);
   } while(sz == -1 && errno == EAGAIN);

   /* cleanup */

   if(sz == -1)
      goto send_fail;

   efree(payload);
   payload = NULL;

   return 1;

send_fail:

   /* failure */

   if(payload) {
      efree(payload);
      payload = NULL;
   }      

   return 0;
}

/*
 * recv payload on the given udp socket, parse the udp header and set the response
 * =1 success   =0 failure
 */

static int php_ezmc_recv(int sd, char **payload, int *payload_len) {

   size_t sz;
   struct mcudphdr hdr;
   void *data = NULL;
   size_t data_len;

   /* allocate payload space */

   data_len = EZG(payload_max);
   data = emalloc(data_len);
   if(data == NULL)
      goto recv_fail;

   /* try to recieve data while process keeps getting interrupted */

   do {
      sz = recv(sd, data, data_len, 0);
   } while(sz == -1 && errno == EAGAIN);

   /* parse the header */

   if(sz == -1)
      goto recv_fail;

   if(sz < sizeof(hdr))
      goto recv_fail;

   memcpy(&hdr, data, sizeof(hdr));

   if((ntohs(hdr.id) != EZG(id)) || (hdr.seq != 0) || (ntohs(hdr.cnt) != 1))
      goto recv_fail;

   /* save the payload minus the memcache-udp header */

   *payload_len = sz - sizeof(hdr);
   *payload = emalloc(*payload_len);
   memset(*payload, 0, *payload_len);

   memcpy(*payload, (char *)data + sizeof(hdr), *payload_len);

   efree(data);
   data = NULL;

   return 1;

recv_fail:

   /* failure */

   if(data) {
      efree(data);
      data = NULL;
   }      

   return 0;      
}

/*
 * issue a command to the memcache server using the configuration initialized by the global variables
 */

static int php_ezmc_cmd_global() {

   /* send data */

   if(php_ezmc_send(EZG(ezd).sd, EZG(send_buf), EZG(send_len)) == 0)
      goto cmd_fail;

   /* recv. data */

   if(php_ezmc_recv(EZG(ezd).sd, &EZG(recv_buf), &EZG(recv_len)) == 0)
      goto cmd_fail;

   return 1;

cmd_fail:

   /* failure */

   return 0;
}

#ifndef MIN
#define MIN(a,b)  ((a) < (b) ? (a) : (b))
#endif
#define EZMC_TEST_RECV(a)     EZMC_TEST_STR(EZG(recv_buf), EZG(recv_len), (a))
#define EZMC_TEST_STR(s,l,a)  ((strlen(a) == (l)) && (memcmp((s), (a), (l)) == 0))

static int php_ezmc_store(INTERNAL_FUNCTION_PARAMETERS, const char *call, const char *response) {

   int pass;

   /* get parameters */

   EZG(send_buf) = NULL;
   EZG(recv_buf) = NULL;

   if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zssll", /* resource,key,value,flags,exptime */
      &EZG(zezd),
      &EZG(key), &EZG(key_len),
      &EZG(value), &EZG(value_len),
      &EZG(flags), &EZG(exptime)) == FAILURE)
      return 0;

   ZEND_FETCH_RESOURCE(EZG(ezdp), php_ezmc_ezd *, &EZG(zezd), -1, PHP_EZMC_EZD_RES_NAME, le_ezmc_ezd);

   EZG(ezd) = *EZG(ezdp);

   EZG(send_buf) = emalloc(EZG(payload_max));      
   if(EZG(send_buf) == NULL)
      return 0;

   EZG(send_len) = snprintf(EZG(send_buf), EZG(payload_max), "%s %.*s %li %li %i\r\n",
                            call, EZG(key_len), EZG(key), EZG(flags), EZG(exptime), EZG(value_len));

   if(EZG(send_len) + EZG(value_len) + 2 > EZG(payload_max))
      goto store_fail;
                            
   memcpy(EZG(send_buf) + EZG(send_len), EZG(value), EZG(value_len));
   memcpy(EZG(send_buf) + EZG(send_len) + EZG(value_len), "\r\n", 2);

   EZG(send_len) += EZG(value_len) + 2;

   if(php_ezmc_cmd_global() == 0)
      goto store_fail;

   /* make sure value was stored sucessfully, then cleanup & return */

   pass = EZMC_TEST_RECV(response);

   efree(EZG(send_buf));
   efree(EZG(recv_buf));
   
   return pass;

store_fail:

    if(EZG(send_buf))
        efree(EZG(send_buf));
    if(EZG(recv_buf))
        efree(EZG(recv_buf));

    return 0;
}

static long php_ezmc_delta(INTERNAL_FUNCTION_PARAMETERS, const char *call) {
  
   int n, skip;
   char *ptr;
   int len;
   long delta;

   /* get parameters */

   EZG(send_buf) = NULL;
   EZG(recv_buf) = NULL;

   if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zsl", /* resource,key,delta */
      &EZG(ezdp),
      &EZG(key), &EZG(key_len),
      &delta) == FAILURE)
      return -1;

   ZEND_FETCH_RESOURCE(EZG(ezdp), php_ezmc_ezd *, &EZG(zezd), -1, PHP_EZMC_EZD_RES_NAME, le_ezmc_ezd);

   EZG(ezd) = *EZG(ezdp);

   EZG(send_buf) = emalloc(EZG(payload_max));      
   if(EZG(send_buf) == NULL)
      goto delta_fail;

   /* the spec says no control codes or whitespace in keys, so the key could be cleaned here */

   EZG(send_len) = snprintf(EZG(send_buf), EZG(payload_max), "%s %.*s %li\r\n", call, EZG(key_len), EZG(key), delta);
   if(EZG(send_len) > EZG(payload_max))
      goto delta_fail;

   if(php_ezmc_cmd_global() == 0)
      goto delta_fail;

   /* parse out the value */

   n = sscanf(EZG(recv_buf), "%li \r\n", &delta);
   if(n != 1)
      goto delta_fail;

   /* cleanup & return value */      

   efree(EZG(send_buf));
   efree(EZG(recv_buf));

   return delta;
  
delta_fail:
   
   /* failure */

   if(EZG(send_buf))
      efree(EZG(send_buf));
   if(EZG(recv_buf))
      efree(EZG(recv_buf));

   return -1;
}

PHP_FUNCTION(ezmemcache_info)
{
   char str[256];

   snprintf(str, sizeof(str), "extension=%s version=%s payload.max=%li default.port=%u",
            PHP_EZMEMCACHE_EXTNAME, PHP_EZMEMCACHE_VERSION, EZG(payload_max), EZG(default_port));

   RETURN_STRING(str, 1);
}

/*
 *
 * create a new ezmemcache resource, which is essentially a socket connect on a datagram socket
 * which just binds the target address/port for send/recv calls but doesnt really have any
 * state/flowcontrol on top of that
 *
 */

PHP_FUNCTION(ezmemcache_open) 
{

   EZG(port) = EZG(default_port);

   /* ezmemcache_open(server[, port]) */

   if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &EZG(server), &EZG(server_len), &EZG(port)) == FAILURE)
      RETURN_FALSE;

   EZG(ezdp) = emalloc(sizeof(*EZG(ezdp)));
   EZG(ezdp)->sl = sizeof(struct sockaddr_in);
   EZG(ezdp)->sa = emalloc(EZG(ezdp)->sl);
   if(EZG(ezdp)->sa == NULL)
      RETURN_FALSE;

   /* setup socket */

   if(php_ezmc_packaddr((struct sockaddr_in *)(EZG(ezdp)->sa), EZG(server), EZG(port)) == 0)
      RETURN_FALSE;

   EZG(ezdp)->sd = php_ezmc_conn(EZG(ezdp)->sa, EZG(ezdp)->sl);
   if(EZG(ezdp)->sd == -1) {
      efree(EZG(ezdp)->sa);
      efree(EZG(ezdp));
      RETURN_FALSE;
   }

   ZEND_REGISTER_RESOURCE(return_value, EZG(ezdp), le_ezmc_ezd);
}

/*
 *
 * destroy the resource
 *
 */

PHP_FUNCTION(ezmemcache_close)
{
      
   /* ezmemcache_close(ezd) */

   if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &EZG(zezd)) == FAILURE)
      RETURN_FALSE;

   ZEND_FETCH_RESOURCE(EZG(ezdp), php_ezmc_ezd *, &EZG(zezd), -1, PHP_EZMC_EZD_RES_NAME, le_ezmc_ezd);

   EZG(ezd) = *EZG(ezdp);

   close(EZG(ezd).sd);

   zend_list_delete(Z_LVAL_P(EZG(zezd)));

   RETURN_TRUE;
}

/*
 *
 * command, server, port
 *
 */

PHP_FUNCTION(ezmemcache_raw)
{
   /* get parameters */

   /* ezmemcache_raw(ezd, command) */

   if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &EZG(zezd), &EZG(send_buf), &EZG(send_len)) == FAILURE)
      RETURN_NULL();

   ZEND_FETCH_RESOURCE(EZG(ezdp), php_ezmc_ezd *, &EZG(zezd), -1, PHP_EZMC_EZD_RES_NAME, le_ezmc_ezd);

   EZG(ezd) = *EZG(ezdp);

   if(php_ezmc_cmd_global() == 0)
      RETURN_NULL();

   RETURN_STRINGL(EZG(recv_buf), EZG(recv_len), 0);
}

PHP_FUNCTION(ezmemcache_get) /* get <key>\r\n */
{

   int n, skip;
   char *ptr;
   int len;

   /* get parameters */

   EZG(send_buf) = NULL;
   EZG(recv_buf) = NULL;

   /* ezmemcache_get(ezd, key) */

   if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zs", &EZG(zezd), &EZG(key), &EZG(key_len)) == FAILURE)
      RETURN_NULL();

   ZEND_FETCH_RESOURCE(EZG(ezdp), php_ezmc_ezd *, &EZG(zezd), -1, PHP_EZMC_EZD_RES_NAME, le_ezmc_ezd);

   EZG(ezd) = *EZG(ezdp);

   EZG(send_buf) = emalloc(EZG(payload_max));      
   if(EZG(send_buf) == NULL)
      goto get_fail;

   /* the spec says no control codes or whitespace in keys, so the key could be cleaned here */

   EZG(send_len) = snprintf(EZG(send_buf), EZG(payload_max), "get %.*s\r\n", EZG(key_len), EZG(key));
   if(EZG(send_len) > EZG(payload_max))
      goto get_fail;

   if(php_ezmc_cmd_global() == 0)
      goto get_fail;

   /* parse out the value */

   n = sscanf(EZG(recv_buf), "VALUE %*s %li %i%n", &EZG(flags), &EZG(value_len), &skip);
   if(n != 2)
      goto get_fail;

   EZG(value) = emalloc(EZG(value_len));
   if(EZG(value) == NULL)
      goto get_fail;

   memcpy(EZG(value), EZG(recv_buf) + skip + 2, EZG(value_len));

   /* make sure response ends in END\r\n */

   ptr = EZG(recv_buf) + skip + 2 + 2 + EZG(value_len);
   len = EZG(recv_len) - skip - 2 - 2 - EZG(value_len);

   if(!EZMC_TEST_STR(ptr, len, "END\r\n"))
      goto get_fail;

   /* setup return array - return array($value, $flags); */

   array_init(return_value);

   add_next_index_stringl(return_value, EZG(value), EZG(value_len), 0);
   add_next_index_long(return_value, EZG(flags));

   /* cleanup & return value */      

   efree(EZG(send_buf));
   efree(EZG(recv_buf));

   // RETURN_STRINGL(EZG(value), EZG(value_len), 0);
   return; 

get_fail:
   
   /* failure */

   if(EZG(send_buf))
      efree(EZG(send_buf));
   if(EZG(recv_buf))
      efree(EZG(recv_buf));

   RETURN_NULL();
}

PHP_FUNCTION(ezmemcache_set) /* set <key> <flags> <exptime> <length>\r\n<data>\r\n */
{
   int retval = php_ezmc_store(INTERNAL_FUNCTION_PARAM_PASSTHRU, "set", "STORED\r\n");
   RETURN_BOOL(retval);
}

PHP_FUNCTION(ezmemcache_add) /* add <key> <flags> <exptime> <length>\r\n<data>\r\n */
{
   int retval = php_ezmc_store(INTERNAL_FUNCTION_PARAM_PASSTHRU, "add", "STORED\r\n");
   RETURN_BOOL(retval);
}

PHP_FUNCTION(ezmemcache_replace) /* replace <key> <flags> <exptime> <length>\r\n<data>\r\n */
{
   int retval = php_ezmc_store(INTERNAL_FUNCTION_PARAM_PASSTHRU, "replace", "STORED\r\n");
   RETURN_BOOL(retval);
}
PHP_FUNCTION(ezmemcache_delete) /* delete <key> [time]\r\n */
{

   int pass;

   /* get parameters */

   EZG(send_buf) = NULL;
   EZG(recv_buf) = NULL;

   /* ezmemcache_delete(ezd, key, time) */

   if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zsl", &EZG(zezd), &EZG(key), &EZG(key_len), &EZG(exptime)) == FAILURE)
      RETURN_BOOL(0);

   ZEND_FETCH_RESOURCE(EZG(ezdp), php_ezmc_ezd *, &EZG(zezd), -1, PHP_EZMC_EZD_RES_NAME, le_ezmc_ezd);

   EZG(ezd) = *EZG(ezdp);

   EZG(send_buf) = emalloc(EZG(payload_max));      
   if(EZG(send_buf) == NULL)
      RETURN_BOOL(0);

   EZG(send_len) = snprintf(EZG(send_buf), EZG(payload_max), "delete %.*s %li\r\n", EZG(key_len), EZG(key), EZG(exptime));
   if(EZG(send_len) > EZG(payload_max))
      goto delete_fail;

   if(php_ezmc_cmd_global() == 0)
      goto delete_fail;

   /* make sure value was stored sucessfully, then cleanup & return */

   pass = EZMC_TEST_RECV("DELETED\r\n");

   efree(EZG(send_buf));
   efree(EZG(recv_buf));
   
   RETURN_BOOL(pass);

delete_fail:

    if(EZG(send_buf))
        efree(EZG(send_buf));
    if(EZG(recv_buf))
        efree(EZG(recv_buf));

   RETURN_BOOL(0);

}
PHP_FUNCTION(ezmemcache_inc) /* incr <key> <delta>\r\n */
{
   long retval = php_ezmc_delta(INTERNAL_FUNCTION_PARAM_PASSTHRU, "incr");
   if(retval == -1) {
      RETURN_NULL();
   } else {
      RETURN_LONG(retval);
   }      
}
PHP_FUNCTION(ezmemcache_dec) /* decr <key> <delta>\r\n */
{
   long retval = php_ezmc_delta(INTERNAL_FUNCTION_PARAM_PASSTHRU, "decr");
   if(retval == -1) {
      RETURN_NULL();
   } else {      
      RETURN_LONG(retval);
   }
}
