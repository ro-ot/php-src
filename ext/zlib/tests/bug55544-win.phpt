--TEST--
Bug #55544 (ob_gzhandler always conflicts with zlib.output_compression)
--EXTENSIONS--
zlib
--SKIPIF--
<?php
if (substr(PHP_OS, 0, 3) != 'WIN') {
    die("skip windows only");
}
include 'data/func.inc';
if (version_compare(get_zlib_version(), "1.2.11") < 0) {
    die("skip - at least zlib 1.2.11 required, got " . get_zlib_version());
}
?>
--INI--
output_handler=ob_gzhandler
zlib.output_compression=0
--ENV--
HTTP_ACCEPT_ENCODING=gzip
--FILE--
<?php
echo "hi";
?>
--EXPECT--
�      
�� �*��
--EXPECTHEADERS--
Content-Encoding: gzip
Vary: Accept-Encoding
