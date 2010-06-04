<?php
echo ezmemcache_info()."\n";

/*
 * some test values
 */

$key = 'cat';
$v = "\33[43;34m[mon\0ster value!]\33[0m";
$inc = 333;
$dec = 111;
$tv = trans($v);
$flags = 666;

function trans($str) {
   return strtr($str, array("\n" => '\n', "\r" => '\r', "\0" => '\0'));
}

/*
 * get resource
 */

$ez = ezmemcache_open('127.0.0.1'); 

/*
 * test some raw queries
 */

$query = "set testkey $flags 0 ".strlen($v)."\r\n$v\r\n";
echo "query=".trans($query)."\n";
$resp = ezmemcache_raw($ez, $query);
echo 'resp('.strlen($resp).")=".trans($resp)."\n";

$query = "get testkey\r\n";
echo "query=".trans($query)."\n";
$resp = ezmemcache_raw($ez, $query);
echo 'resp('.strlen($resp).")=".trans($resp)."\n";

/*
 *
 * test some normal memcache queries
 *
 */

echo "     set $key=>$tv ".(ezmemcache_set    ($ez, $key, $v, $flags, 0) ? 'passed' : 'failed')." (expected: passed)\n";
echo " replace $key=>$tv ".(ezmemcache_replace($ez, $key, $v, $flags, 0) ? 'passed' : 'failed')." (expected: passed)\n";
echo "     add $key=>$tv ".(ezmemcache_add    ($ez, $key, $v, $flags, 0) ? 'passed' : 'failed')." (expected: failed)\n";
echo "  delete $key=>$tv ".(ezmemcache_delete ($ez, $key,             0) ? 'passed' : 'failed')." (expected: passed)\n";
echo "  delete $key=>$tv ".(ezmemcache_delete ($ez, $key,             0) ? 'passed' : 'failed')." (expected: failed)\n";
echo " replace $key=>$tv ".(ezmemcache_replace($ez, $key, $v, $flags, 0) ? 'passed' : 'failed')." (expected: failed)\n";
echo "     add $key=>$tv ".(ezmemcache_add    ($ez, $key, $v, $flags, 0) ? 'passed' : 'failed')." (expected: passed)\n";

list($value, $flags) = ezmemcache_get($ez, $key);

echo "     get $key=>".(($value==null)?'NULL':trans($value).' flags='.$flags)." (expected: $tv flags=$flags)\n";
echo "  delete $key=>$tv ".(ezmemcache_delete($ez, $key, 0) ? 'passed' : 'failed')." (expected: passed)\n";

list($value, $flags) = ezmemcache_get($ez, $key);

echo "     get $key=>".(($value==null)?'NULL':trans($value).' flags='.$flags)." (expected: NULL)\n";

/*
 * 
 * increment & decrement
 *
 */

echo "     set $key=>$inc ".(ezmemcache_set($ez, $key, $inc, $flags, 0) ? 'passed' : 'failed')." (expected: passed)\n";

$value = ezmemcache_inc($ez, $key, $inc);
echo "incr $key $inc=>$value (expected: ".(2*$inc).")\n";

$value = ezmemcache_dec($ez, $key, $dec);
echo "decr $key $dec=>$value (expected: ".(2*$inc-$dec).")\n";

ezmemcache_close($ez);
