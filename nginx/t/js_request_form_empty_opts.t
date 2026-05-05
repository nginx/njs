#!/usr/bin/perl

# (C) Vadim Zhestikov
# (C) F5, Inc.

# Regression test: readRequestForm({}) with an empty options object should
# use default maxKeys and succeed.  In the NJS engine path,
# njs_vm_object_prop() returns NULL for an absent property — the same
# sentinel used for a real VM error — so the validator incorrectly returns
# NJS_ERROR, causing HTTP 500 instead of a normal response.
# The QJS engine uses JS_GetPropertyStr() which returns undefined for absent
# properties; it handles this case correctly.

###############################################################################

use warnings;
use strict;

use Test::More;
use Socket qw/ CRLF /;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx qw/ :DEFAULT /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)
    ->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /qjs {
            js_engine    qjs;
            js_content   test.content_form_empty_opts;
        }

        location /njs {
            js_engine    njs;
            js_content   test.content_form_empty_opts;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    async function content_form_empty_opts(r) {
        try {
            let form = await r.readRequestForm({});
            r.return(200, form.get('a'));
        } catch (e) {
            r.return(500, `${e.constructor.name}:${e.message}`);
        }
    }

    export default { content_form_empty_opts };
EOF

$t->try_run('no js_engine directive')->plan(2);

###############################################################################

like(http_post_form('/qjs', urlencoded_form('a=1')),
    qr/200.*1/s,
    'QJS: readRequestForm({}) with empty options uses default maxKeys');

like(http_post_form('/njs', urlencoded_form('a=1')),
    qr/200.*1/s,
    'NJS: readRequestForm({}) with empty options uses default maxKeys');

$t->stop();

###############################################################################

sub http_post_form {
    my ($url, $form, %extra) = @_;
    my ($content_type, $body) = @{$form};

    my $r = "POST $url HTTP/1.0" . CRLF
        . "Host: localhost" . CRLF
        . "Content-Type: $content_type" . CRLF
        . "Content-Length: " . length($body) . CRLF
        . CRLF
        . $body;

    return http($r, %extra);
}

sub urlencoded_form {
    my ($body) = @_;
    return ['application/x-www-form-urlencoded', $body];
}
