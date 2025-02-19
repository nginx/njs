#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for js_periodic directive.

###############################################################################

use warnings;
use strict;

use Test::More;
use Socket qw/ CRLF /;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;
worker_processes 4;

events {
}

worker_shutdown_timeout 100ms;

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location @periodic {
            js_periodic test.file interval=1s;
        }

        location /test_file {
            js_content test.test_file;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    import fs from 'fs';

    async function file() {
        let fh = await fs.promises.open(ngx.conf_prefix + 'file', 'a+');

        await fh.write('abc');
        await fh.close();
    }

    function test_file(r) {
        r.return(200,
             fs.readFileSync(ngx.conf_prefix + 'file').toString()  == 'abc');
    }

    export default { file, test_file };
EOF

$t->try_run('no js_periodic with fs support');

$t->plan(2);

###############################################################################

select undef, undef, undef, 0.1;

like(http_get('/test_file'), qr/true/, 'file test');

$t->stop();

unlike($t->read_file('error.log'), qr/\[error\].*should not be seen/,
	'check for not discadred events');
