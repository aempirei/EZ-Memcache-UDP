PHP_ARG_ENABLE(ezmemcache, whether to enable EZ Memcache support,
[  --enable-ezmemcache     Enable EZ Memcache support])

if test "$PHP_EZMEMCACHE" = "yes"; then
  AC_DEFINE(HAVE_EZMEMCACHE, 1, [Whether you have EZ Memcache])
  PHP_NEW_EXTENSION(ezmemcache, ezmemcache.c, $ext_shared)
fi
