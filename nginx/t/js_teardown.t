#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for asynchronous work started by JavaScript code that runs while
# a context is being destroyed.

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
worker_processes 1;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /zero {
            js_context_reuse 0;
            js_content test.content;
        }

        location /reuse {
            js_context_reuse 1;
            js_content test.content;
        }

        location /native {
            return 200 native;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function late(name) {
        return () => ngx.log(ngx.ERR, 'late ' + name);
    }

    function content(r) {
        const mode = r.args.mode;

        if (mode === 'timer') {
            Promise.reject({toString() {
                setTimeout(late('rejection timer'), 10);
                return 'rejection timer reason';
            }});

        } else if (mode === 'fetch') {
            Promise.reject({toString() {
                ngx.fetch('http://127.0.0.1:' + r.variables.server_port
                          + '/native').then(late('rejection fetch'));
                return 'rejection fetch reason';
            }});

        } else if (mode === 'exit') {
            njs.on('exit', () => {
                setTimeout(late('exit timer'), 10);
                ngx.log(ngx.ERR, 'exit hook ' + r.uri);
            });

        } else if (mode === 'reinstall') {
            njs.on('exit', () => {
                Promise.resolve().then(() => {
                    njs.on('exit', late('reinstalled exit hook'));
                });
            });

        }

        r.return(200, 'ok');
    }

    export default {content};
EOF

$t->try_run('no njs available')->plan(11);

###############################################################################

for my $scope ('zero', 'reuse') {
    for my $mode ('timer', 'fetch', 'exit', 'reinstall') {
        like(http_get("/$scope?mode=$mode"), qr/ok$/, "$scope $mode");
    }
}

http_get('/reuse?mode=none') for 1 .. 2;
select undef, undef, undef, 0.2;

$t->stop();

my $log = $t->read_file('error.log');

like($log, qr/unhandled rejection: rejection fetch reason/,
    'rejection logged');
like($log, qr/exit hook \/reuse/, 'exit hook runs');
unlike($log, qr/late /, 'no teardown work runs later');

###############################################################################
