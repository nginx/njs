#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for stream njs module, crypto module import.
# Regression test for Github issue #1049.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/stream stream_return/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    js_set $hash test.hash;

    server {
        listen  127.0.0.1:8081;
        return  $hash;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    import cr from 'crypto';

    function hash(s) {
        return cr.createHash('sha256').update('abc').digest('hex');
    }

    export default {hash};
EOF

$t->try_run('no njs crypto module')->plan(1);

###############################################################################

like(stream('127.0.0.1:' . port(8081))->io('###'),
	qr/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad/,
	'crypto sha256 digest');

###############################################################################
