#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS HTTP object lifetime after request cleanup.

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

    js_context_reuse 1;
    js_import test.js;
    js_var $test_value original;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /engine {
            js_content test.engine;
        }

        location /a {
            js_content test.content;
        }

        location /child {
            return 200 'child';
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    let saved, snapshots;

    function engine(r) {
        r.return(200, njs.engine);
    }

    const checks = {
        headers_in: () => saved.host,
        headers_in_keys: () => Object.keys(saved),
        headers_out: () => saved['X-Test'],
        headers_out_set: () => { saved['X-Test'] = 'changed'; },
        headers_out_delete: () => { delete saved['X-Test']; },
        headers_out_define: () => {
            Object.defineProperty(saved, 'X-Test', {value: 'changed'});
        },
        headers_out_keys: () => Object.keys(saved),
        variables: () => saved.test_value,
        variables_set: () => { saved.test_value = 'changed'; },
        raw_variables: () => saved.test_value,
        form: () => saved.get('x'),
        form_all: () => saved.getAll('x'),
        form_has: () => saved.has('x'),
        form_each: () => saved.forEach(() => {}),
        form_files: () => saved.hasFiles(),
        form_file: () => saved.get('upload'),
        request: () => saved.uri,
        args_hot: () => saved.args,
        body_cold: () => saved.requestText,
        body_hot: () => saved.requestText,
        body_switch: () => saved.requestBuffer,
        read_text: () => saved.readRequestText(),
        read_form: () => saved.readRequestForm(),
        reply: () => saved.status,
        reply_callback: () => saved.status,
        reply_headers: () => saved.headersOut,
        reply_header_view: () => saved['Content-Type'],
        reply_variables: () => saved.test_value,
        reply_body: () => saved.responseBuffer
    };

    async function content(r) {
        const name = r.args.case;
        if (r.args.keep) {
            snapshots = {args: r.args, uri: r.uri};

            if (name.startsWith('headers_in')) {
                saved = r.headersIn;
            } else if (name.startsWith('headers_out')) {
                r.headersOut['X-Test'] = 'original';
                saved = r.headersOut;
            } else if (name.startsWith('variables')) {
                saved = r.variables;
            } else if (name === 'raw_variables') {
                saved = r.rawVariables;
                snapshots.buffer = saved.test_value;
            } else if (name.startsWith('form')) {
                saved = await r.readRequestForm();
                snapshots.form = saved.get('x');
                if (name === 'form_file') {
                    snapshots.filename = saved.get('upload').name;
                }
            } else if (name.startsWith('reply')) {
                if (name === 'reply_callback') {
                    saved = await new Promise(resolve => {
                        r.subrequest('/child', resolve);
                    });
                } else {
                    saved = await r.subrequest('/child');
                }

                if (saved.status !== 200 || saved.headersOut === undefined)
                {
                    throw new Error('reply expired before parent cleanup');
                }

                snapshots.reply = saved.responseText;
                if (name === 'reply_header_view') {
                    saved = saved.headersOut;
                } else if (name === 'reply_variables') {
                    saved = saved.variables;
                }
            } else {
                saved = r;
                if (name === 'body_hot' || name === 'body_switch') {
                    snapshots.body = r.requestText;
                }
            }

            r.return(200, 'kept');
            return;
        }

        let expired = false;
        try {
            checks[name]();
        } catch (e) {
            expired = e instanceof TypeError;
        }

        const independent = snapshots.args.keep === '1'
                            && snapshots.uri === '/a'
                            && (snapshots.form === undefined
                                || snapshots.form === 'original')
                            && (snapshots.body === undefined
                                || snapshots.body === 'x=original')
                            && (snapshots.filename === undefined
                                || snapshots.filename === 'test.txt')
                            && (snapshots.reply === undefined
                                || snapshots.reply === 'child')
                            && (snapshots.buffer === undefined
                                || snapshots.buffer.toString() === 'original');

        saved = null;
        snapshots = null;
        r.return(200, expired + ':' + independent);
    }

    export default {content, engine};

EOF

my @cases = qw/
    headers_in headers_in_keys
    headers_out headers_out_set headers_out_delete headers_out_define
    headers_out_keys variables variables_set raw_variables
    form form_all form_has form_each form_files form_file
    request args_hot body_cold body_hot body_switch read_text read_form
    reply reply_callback reply_headers reply_header_view reply_variables
    reply_body
/;

@cases = grep { $_ eq $ENV{NJS_LIFETIME_CASE} } @cases
    if defined $ENV{NJS_LIFETIME_CASE};

$t->try_run('no njs available');

plan(skip_all => 'QuickJS-specific lifetime tests')
    if http_get('/engine') =~ /njs$/m;

$t->plan(2 * @cases);

###############################################################################

for my $case (@cases) {
    my $type = 'application/x-www-form-urlencoded';
    my $body = 'x=original';

    if ($case eq 'form_file') {
        $type = 'multipart/form-data; boundary=A';
        $body = "--A\r\nContent-Disposition: form-data; name=\"x\"\r\n\r\n"
                . "original\r\n--A\r\n"
                . "Content-Disposition: form-data; name=\"upload\"; "
                . "filename=\"test.txt\"\r\n\r\npayload\r\n--A--\r\n";
    }

    like(http("POST /a?keep=1&case=$case HTTP/1.0\r\n"
              . "Host: localhost\r\n"
              . "Content-Type: $type\r\n"
              . "Content-Length: " . length($body) . "\r\n\r\n$body"),
         qr/kept$/, "$case before cleanup");
    like(http_get("/a?case=$case"), qr/true:true$/,
         "$case expires without invalidating extracted values");
}

###############################################################################
