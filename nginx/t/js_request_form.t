#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, r.readRequestForm() method.

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

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    js_var $foo;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /access_form {
            js_access test.access_form;
            js_content test.content;
        }

        location /content_form {
            js_content test.content_form;
        }

        location /content_form_hex {
            js_content test.content_form_hex;
        }

        location /content_form_cache {
            js_content test.content_form_cache;
        }

        location /content_text_then_form {
            js_content test.content_text_then_form;
        }

        location /content_form_error {
            js_content test.content_form_error;
        }

        location /content_form_limit {
            js_content test.content_form_limit;
        }

        location /content_form_no_options {
            js_content test.content_form_no_options;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function hex(s) {
        let out = '';

        for (let i = 0; i < s.length; i++) {
            let c = s.charCodeAt(i);
            out += (c < 0x10 ? '0' : '') + c.toString(16);
        }

        return out;
    }

    function render(form) {
        let first = form.get('a');
        let files = [];
        let pairs = [];
        let upload = form.get('upload');
        let uploadAll = form.getAll('upload')
                            .map(v => typeof v == 'string' ? v : v.name);
        let uploadFirst = '';

        if (first === null) {
            first = 'null';

        } else if (typeof first != 'string') {
            first = `[file:${first.name}]`;
        }

        if (upload !== null && typeof upload != 'string') {
            uploadFirst = upload.name;
        }

        form.forEach((value, key) => {
            if (typeof value == 'string') {
                pairs.push(`${key}=${value}`);
                return;
            }

            files.push(`${key}:${value.name}`);
            pairs.push(`${key}=[file:${value.name}]`);
        });

        return [
            first,
            form.getAll('a')
                .map(v => typeof v == 'string' ? v : `[file:${v.name}]`)
                .join(','),
            form.has('a'),
            form.has('upload'),
            form.hasFiles(),
            files.length == 0 ? ''
                              : `get:${uploadFirst};all:${uploadAll};`
                                + `each:${files.join(',')}`,
            pairs.join('&')
        ].join('|');
    }

    function content(r) {
        r.return(200, `var:${r.variables.foo}`);
    }

    async function access_form(r) {
        try {
            r.variables.foo = render(await r.readRequestForm({maxKeys: 8}));

        } catch (e) {
            r.variables.foo = `${e.constructor.name}:${e.message}`;
        }
    }

    async function content_form(r) {
        try {
            r.return(200, render(await r.readRequestForm({maxKeys: 8})));

        } catch (e) {
            r.return(500, `${e.constructor.name}:${e.message}`);
        }
    }

    async function content_form_hex(r) {
        try {
            let form = await r.readRequestForm({maxKeys: 8});
            let value = form.get('a');

            if (value === null) {
                value = 'NULL';
            }

            r.return(200, hex(value));

        } catch (e) {
            r.return(500, `${e.constructor.name}:${e.message}`);
        }
    }

    async function content_form_cache(r) {
        let form = await r.readRequestForm({maxKeys: 8});

        await r.readRequestForm({maxKeys: 1});

        r.return(200, render(form));
    }

    async function content_text_then_form(r) {
        let text = await r.readRequestText();
        let form = await r.readRequestForm({maxKeys: 8});

        r.return(200, `${text.length}|${render(form)}`);
    }

    async function content_form_error(r) {
        try {
            await r.readRequestForm({maxKeys: 8});
            r.return(200, 'no_error');

        } catch (e) {
            r.return(500, `${e.constructor.name}:${e.message}`);
        }
    }

    async function content_form_limit(r) {
        try {
            await r.readRequestForm({maxKeys: 1});
            r.return(200, 'no_error');

        } catch (e) {
            r.return(500, e.message);
        }
    }

    async function content_form_no_options(r) {
        try {
            r.return(200, render(await r.readRequestForm({})));

        } catch (e) {
            r.return(500, `${e.constructor.name}:${e.message}`);
        }
    }

    export default { access_form, content, content_form, content_form_hex,
                     content_form_cache, content_text_then_form,
                     content_form_error, content_form_limit,
                     content_form_no_options };
EOF

$t->try_run('no readRequestForm')->plan(60);

###############################################################################

like(http_post_form('/access_form',
    urlencoded_form('a=1&a=2&empty=&=blank&space=one+two')),
    qr/200.*var:1\|1,2\|true\|false\|false\|\|a=1&a=2&empty=&=blank&space=one two/s,
    'readRequestForm() in js_access with urlencoded body');

like(http_post_form('/content_form',
    urlencoded_form('a=1&a=2&empty=&=blank&space=one+two')),
    qr/200.*1\|1,2\|true\|false\|false\|\|a=1&a=2&empty=&=blank&space=one two/s,
    'readRequestForm() in js_content with urlencoded body');

like(http_post_form('/content_form_cache', urlencoded_form('a=1&a=2')),
    qr/200.*1\|1,2\|true\|false\|false\|\|a=1&a=2/s,
    'successful form parse is cached');

like(http_post_form('/content_text_then_form',
    urlencoded_form('a=1&a=2&z=3')),
    qr/200.*11\|1\|1,2\|true\|false\|false\|\|a=1&a=2&z=3$/s,
    'readRequestText() then readRequestForm() reuses cached body');

like(http_post_form('/content_form', urlencoded_form('')),
    qr/200.*null\|\|false\|false\|false\|\|$/s,
    'empty urlencoded body returns an empty form');

like(http_post_form('/content_form',
    urlencoded_form('&baz=fuz&&muz=tax&')),
    qr/200.*null\|\|false\|false\|false\|\|baz=fuz&muz=tax/s,
    'urlencoded empty fields are skipped');

like(http_post_form('/content_form',
    urlencoded_form('freespace&name&value=12')),
    qr/200.*null\|\|false\|false\|false\|\|freespace=&name=&value=12/s,
    'urlencoded fields without equals have an empty value');

like(http_post_form('/content_form',
    urlencoded_form('==fu=z&baz=bar')),
    qr/200.*null\|\|false\|false\|false\|\|==fu=z&baz=bar/s,
    'urlencoded first equals separates name and value');

like(http_post_form('/content_form',
    urlencoded_form('ba+z=f+uz')),
    qr/200.*null\|\|false\|false\|false\|\|ba z=f uz/s,
    'urlencoded plus is decoded in names and values');

like(http_post_form('/content_form_hex', urlencoded_form('a=%41%42%43')),
    qr/200.*414243$/s,
    'urlencoded percent-decoding of %41%42%43 returns ABC');

like(http_post_form('/content_form_hex', urlencoded_form('a=%2a%5F%7e')),
    qr/200.*2a5f7e$/s,
    'urlencoded percent-decoding accepts mixed-case hex digits');

like(http_post_form('/content_form_hex', urlencoded_form('a=%00')),
    qr/200.*00$/s,
    'urlencoded percent-decoding accepts NUL byte');

like(http_post_form('/content_form_hex', urlencoded_form('a=x%20+y')),
    qr/200.*78202079$/s,
    'urlencoded percent-decoding handles %20 and + in one value');

like(http_post_form('/content_form',
    ['application/x-www-form-urlencoded ; charset=utf-8', 'a=1']),
    qr/200.*1\|1\|true\|false\|false\|\|a=1/s,
    'content type OWS before parameters is skipped');

like(http_post_form('/content_form',
    multipart_form(
        { name => 'a', value => '1' },
        { name => 'upload', filename => 'a.txt', value => 'AAA' },
        { name => 'a', value => '2' },
        { name => 'upload', filename => 'b.txt', value => 'BBB' },
        { name => 'z', value => '3' },
    )),
    qr{
        200.*1\|1,2\|true\|true\|true\|
        get:a.txt;all:a.txt,b.txt;each:upload:a.txt,upload:b.txt\|
        a=1&upload=\[file:a.txt\]&a=2&upload=\[file:b.txt\]&z=3
    }sx,
    'multipart text fields and file metadata');

like(http_post_form('/content_form',
    multipart_form(
        { name => 'upload', filename => 'only.txt', value => 'AAA' },
    )),
    qr{
        200.*null\|\|false\|true\|true\|
        get:only.txt;all:only.txt;each:upload:only.txt\|
        upload=\[file:only.txt\]$
    }sx,
    'file parts expose filename metadata');

like(http_post_form('/content_form',
    multipart_form(
        { name => 'a\\"b', value => '1' },
    )),
    qr/200.*a"b=1/s, 'quoted multipart parameter escapes are unescaped');

like(http_post_form('/content_form',
    multipart_form({ name => 'empty', value => '' })),
    qr/200.*null\|\|false\|false\|false\|\|empty=$/s,
    'empty multipart text field is preserved');

like(http_post_form('/content_form',
    ['multipart/form-data; boundary=X', '--X--']),
    qr/200.*null\|\|false\|false\|false\|\|$/s,
    'empty multipart body returns an empty form');

like(http_post_form('/content_form',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="ows"  ' . CRLF . CRLF
     . '1' . CRLF
     . '--X--']),
    qr/200.*null\|\|false\|false\|false\|\|ows=1/s,
    'multipart header value trailing OWS is skipped');

like(http_post_form('/content_form_no_options', urlencoded_form('a=1&b=2')),
    qr/200.*1\|1\|true\|false\|false\|\|a=1&b=2/s,
    'readRequestForm({}) accepts an empty options object');

my $utf8_filename = "\xe6\x97\xa5\xe6\x9c\xac.txt";
like(http_post_form('/content_form',
    multipart_form(
        { name => 'upload', filename => $utf8_filename, value => 'AAA' },
    )),
    qr{200.*null\|\|false\|true\|true\|
       get:\Q$utf8_filename\E;all:\Q$utf8_filename\E;
       each:upload:\Q$utf8_filename\E\|
       upload=\[file:\Q$utf8_filename\E\]$}sx,
    'multipart filename preserves raw UTF-8 bytes');

my $fake_boundary = multipart_form(
    { name => 'x',
      value => "payload\r\n--FAKE\r\n\r\nContent-Disposition: form-data; "
               . 'name="injected"' . "\r\n\r\nevil" },
);

like(http_post_form('/content_form', $fake_boundary),
    qr/200.*x=payload/s,
    'fake multipart boundary in body is treated as payload');
unlike(http_post_form('/content_form', $fake_boundary),
    qr/injected=evil/s,
    'fake multipart boundary does not restart header parsing');

like(http_post_form('/content_form_error', urlencoded_form('a=%')),
    qr/500.*malformed percent escape/s,
    'urlencoded bare % at end is rejected');

like(http_post_form('/content_form_error', urlencoded_form('a=%4')),
    qr/500.*malformed percent escape/s,
    'urlencoded %X with missing second digit is rejected');

like(http_post_form('/content_form_error', urlencoded_form('a=%gg')),
    qr/500.*malformed percent escape/s,
    'urlencoded non-hex percent escape is rejected');

like(http_post_form('/content_form_error', urlencoded_form('%Z=1')),
    qr/500.*malformed percent escape/s,
    'urlencoded malformed percent escape in name is rejected');

like(http_post_form('/content_form_error', ['text/plain', 'a=1']),
    qr/500.*TypeError:unsupported content type/s,
    'unsupported content type is rejected');

like(http_post_form('/content_form_error', [';boundary=X', '']),
    qr/500.*TypeError:unsupported content type/s,
    'empty content type is rejected');

like(http_post_raw('/content_form_error', 'a=1'),
    qr/500.*TypeError:request content type is required/s,
    'missing content type is rejected');

like(http_post_form('/content_form_error',
    ['application/x-www-form-urlencoded; =x', 'a=1']),
    qr/500.*malformed parameter/s,
    'malformed content type parameter is rejected');

like(http_post_form('/content_form_error', ['multipart/form-data', 'a=1']),
    qr/500.*TypeError:multipart boundary is required/s,
    'multipart boundary is required');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=""', '']),
    qr/500.*(invalid multipart boundary|empty parameter value)/s,
    'empty quoted multipart boundary is rejected');

like(http_post_form('/content_form_error',
    ["multipart/form-data; boundary=" . 'x' x 201, '']),
    qr/500.*invalid multipart boundary/s,
    'multipart boundary over 200 bytes is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X; boundary=Y', '--X--']),
    qr/500.*duplicate boundary parameter/s,
    'duplicate multipart boundary parameter is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X junk', '--X--']),
    qr/500.*(malformed content type|malformed parameter)/s,
    'malformed trailing content type parameter data is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=XXX', '--XXXjunk']),
    qr/500.*malformed multipart boundary/s,
    'multipart opening delimiter without CRLF is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=XXX', '-']),
    qr/500.*malformed multipart body/s,
    'short multipart body without boundary marker is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a"']),
    qr/500.*missing multipart header separator/s,
    'multipart part without header separator is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'X-Large: ' . ('a' x 17000) . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*multipart headers are too large/s,
    'multipart header block size limit is enforced');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'X-Long: ' . ('a' x 4100) . CRLF
     . 'Content-Disposition: form-data; name="a"' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*multipart header line is too long/s,
    'multipart header line size limit is enforced');

my $many_headers = join('', map { "X-$_: v" . CRLF } 1 .. 33);

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . $many_headers
     . 'Content-Disposition: form-data; name="a"' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*too many multipart headers/s,
    'multipart header count limit is enforced');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'X-Other: foo' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*missing Content-Disposition header/s,
    'multipart part without Content-Disposition is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a"' . CRLF
     . 'Content-Disposition: form-data; name="b"' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*duplicate Content-Disposition header/s,
    'duplicate multipart Content-Disposition header is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: attachment; name="a"' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*unsupported disposition type/s,
    'unsupported multipart disposition type is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*multipart field name is required/s,
    'multipart Content-Disposition without name is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a" junk'
     . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*malformed Content-Disposition/s,
    'multipart Content-Disposition trailing data is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a"; name="b"'
     . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*duplicate name parameter/s,
    'duplicate multipart name parameter is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a"; filename="x"; '
     . 'filename="y"' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*duplicate filename parameter/s,
    'duplicate multipart filename parameter is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*malformed parameter/s,
    'multipart parameter without equals is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name=' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*empty parameter value/s,
    'multipart parameter with empty unquoted value is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a"; filename="x\\"'
     . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*unterminated quoted parameter/s,
    'multipart trailing backslash in quoted parameter is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'NoColonHere' . CRLF
     . 'Content-Disposition: form-data; name="a"' . CRLF . CRLF
     . 'data' . CRLF
     . '--X--']),
    qr/500.*malformed multipart header/s,
    'multipart header line without colon is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a"' . CRLF . CRLF
     . 'data' . CRLF
     . '--Xjunk']),
    qr/500.*malformed multipart boundary/s,
    'malformed multipart boundary after part is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=XXX', 'no boundary here at all']),
    qr/500.*malformed multipart body/s,
    'multipart body without boundary marker is rejected');

like(http_post_form('/content_form_error',
    ['multipart/form-data; boundary=X',
     '--X' . CRLF
     . 'Content-Disposition: form-data; name="a"' . CRLF . CRLF
     . 'value with no terminating boundary']),
    qr/500.*truncated multipart body/s,
    'multipart body without closing boundary is rejected');

like(http_post_form('/content_form_limit', urlencoded_form('a=1&b=2')),
    qr/500.*maxKeys limit exceeded/s, 'maxKeys limit breach rejects');

like(http_post_form('/content_form_limit', urlencoded_form('&a=1&&')),
    qr/200.*no_error/s, 'urlencoded empty fields do not count for maxKeys');

like(http_post_form('/content_form_limit',
    multipart_form({ name => 'a', value => '1' },
                   { name => 'b', value => '2' })),
    qr/500.*maxKeys limit exceeded/s,
    'multipart maxKeys limit breach rejects');

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

sub http_post_raw {
    my ($url, $body, %extra) = @_;

    my $r = "POST $url HTTP/1.0" . CRLF
        . "Host: localhost" . CRLF
        . "Content-Length: " . length($body) . CRLF
        . CRLF
        . $body;

    return http($r, %extra);
}

sub urlencoded_form {
    my ($body) = @_;

    return ['application/x-www-form-urlencoded', $body];
}

sub multipart_form {
    my (@parts) = @_;
    my $boundary = '----test-boundary';
    my $body = '';

    for my $part (@parts) {
        $body .= '--' . $boundary . CRLF;
        $body .= 'Content-Disposition: form-data; name="' . $part->{name} . '"';

        if (defined $part->{filename}) {
            $body .= '; filename="' . $part->{filename} . '"';
        }

        $body .= CRLF . CRLF;
        $body .= $part->{value} . CRLF;
    }

    $body .= '--' . $boundary . '--';

    return ["multipart/form-data; boundary=$boundary", $body];
}

###############################################################################
