#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (c) Nginx, Inc.

# Tests for http njs module, check for proper location blocks merging.

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

    js_import main.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /a {
            js_content main.version;
        }

        location /b {
            js_content main.version;
        }

        location /c {
            js_content main.version;
        }

        location /d {
            js_content main.version;
        }
    }
}

EOF

$t->write_file('main.js', <<EOF);
    function version(r) {
        r.return(200, njs.version);
    }

    export default {version};

EOF

$t->try_run('no njs available')->plan(1);

###############################################################################

$t->stop();

my $content = $t->read_file('error.log');
my $count = () = $content =~ m/ js vm init/g;
ok($count == 1, 'http js block imported once');

###############################################################################
