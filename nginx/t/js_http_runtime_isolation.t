#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for HTTP request host bindings and JavaScript runtime isolation.

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

my $t = Test::Nginx->new()->has(qw/http rewrite/)
    ->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;
worker_processes 1;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;
    js_set $retval test.variable;

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

        location /zero_child {
            js_context_reuse 0;
            js_content test.child;
        }

        location /reuse_child {
            js_context_reuse 1;
            js_content test.child;
        }

        location /native {
            return 200 native;
        }

        location /variable {
            return 200 $retval;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function reject_set(request, value) {
        try {
            request.setReturnValue(value);
            return 'accepted';
        } catch (e) {
            return e instanceof TypeError ? 'rejected' : e.name;
        }
    }

    async function content(r) {
        const mode = r.args.mode;
        const uri = mode === 'native_set' ? '/native'
                                         : r.uri + '_child?mode=' + mode;

        if (mode === 'callback') {
            r.subrequest(uri, reply => {
                reply.parent.return(200, String(reply.parent === r));
            });
            return;
        }

        const reply = await r.subrequest(uri);
        if (mode === 'reply_set_object' || mode === 'native_set') {
            r.return(200, reject_set(reply, {local: [1, 2, 3]}));

        } else if (mode === 'reply_set_string') {
            r.return(200, reject_set(reply, 'local'));

        } else if (mode === 'own_set') {
            reply.parent.setReturnValue('local');
            r.return(200, String(reply.parent === r));

        } else {
            r.return(200, reply.responseText + ':' + (reply.parent === r));
        }
    }

    function child(r) {
        const parent = r.parent;
        if (parent === undefined) {
            r.return(200, 'undefined');
            return;
        }

        const mode = r.args.mode;
        if (mode === 'shape') {
            parent.localRuntimeProperty = {local: [1, 2, 3]};
            delete parent.localRuntimeProperty;

        } else if (mode === 'args') {
            parent.args.local = r.args;

        } else if (mode === 'parent_set') {
            parent.setReturnValue({local: [1, 2, 3]});
        }

        r.return(200, 'parent exposed');
    }

    async function variable(r) {
        await Promise.resolve();
        r.setReturnValue('own result');
    }

    export default {content, child, variable};
EOF

$t->try_run('no njs available')->plan(19);

for my $scope ('zero', 'reuse') {
    for my $mode ('parent', 'shape', 'args', 'parent_set') {
        like(http_get("/$scope?mode=$mode"), qr/undefined:true$/m,
            "$scope $mode stays in host runtime");
    }

    for my $mode ('reply_set_object', 'reply_set_string', 'native_set') {
        like(http_get("/$scope?mode=$mode"), qr/rejected$/m,
            "$scope $mode rejects foreign result slot");
    }

    for my $mode ('callback', 'own_set') {
        like(http_get("/$scope?mode=$mode"), qr/true$/m,
            "$scope $mode preserves original parent");
    }
}

like(http_get('/variable'), qr/own result$/m, 'own async js_set result');

###############################################################################
