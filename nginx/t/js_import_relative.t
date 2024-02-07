#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (c) Nginx, Inc.

# Tests for http njs module, js_import directive, importing relative paths.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import main from lib/main.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /local {
            js_content main.test_local;
        }

        location /top {
            js_content main.test_top;
        }
    }
}

EOF

my $d = $t->testdir();

mkdir("$d/lib");
mkdir("$d/lib/sub");

$t->write_file('lib/main.js', <<EOF);
    import sub from './sub/foo.js';
    import local from './foo.js';
    import top from '../foo.js';

    function test_local(r) {
        r.return(200, local.test);
    }

    function test_top(r) {
        r.return(200, top.test);
    }

    export default {test_local, test_top};

EOF

$t->write_file('lib/sub/foo.js', <<EOF);
    export default {test: "SUB"};

EOF

$t->write_file('lib/foo.js', <<EOF);
    export default {test: "LOCAL"};

EOF

$t->write_file('foo.js', <<EOF);
    export default {test: "TOP"};

EOF

$t->try_run('no njs available')->plan(2);

###############################################################################

like(http_get('/local'), qr/LOCAL/s, 'local relative import');
like(http_get('/top'), qr/TOP/s, 'local relative import 2');

###############################################################################
