--TEST--
Handlebars phpinfo
--SKIPIF--
<?php if( !extension_loaded('handlebars') ) die('skip '); ?>
--INI--
handlebars.cache.enable=0
handlebars.cache.enable_cli=0
--FILE--
<?php
phpinfo(INFO_MODULES);
--EXPECTF--
%A
handlebars
%A
Version => %s
Released => %s
Authors => %s
Spec Version => %s
libhandlebars Version => %s
libhandlebars Handlebars Spec Version => %s
libhandlebars Mustache Spec Version => %s
PSR support => %s
Local memory usage => %d
%A
Directive => Local Value => Master Value
handlebars.cache.backend => mmap => mmap
handlebars.cache.enable => Off => Off
handlebars.cache.enable_cli => Off => Off
handlebars.cache.max_age => %s => %s
handlebars.cache.max_entries => %s => %s
handlebars.cache.max_size => %s => %s
handlebars.cache.save_path => %s => %s
handlebars.cache.stat => %s => %s
handlebars.pool_size => %s => %s
%A
