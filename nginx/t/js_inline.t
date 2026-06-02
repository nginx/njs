#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, js_set inline expressions.

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

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(13);

use constant TEMPLATE_CONF => <<'EOF';

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

	# oridinary variable set (function reference)
    js_set $test_var     test.variable;

    js_set $var          'r.var.test_var.toUpperCase()';
    js_set $header       'r.headersIn["Foo"] || "none"';
    js_set $template     `/p${r.uri}/post`;
    js_set $expression   '1 + 2';
    js_set $force_expr   (r.var.uri);

    js_set $runtime_err  '(o.a.a)';

    %%EXTRA_CONF%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /inline {
            return 200 "uri=$template foo=$header sum=$expression";
        }

        location /var {
            return 200 "var=$var";
        }

        location /mixed {
            return 200 "test_var=$test_var uri=$template";
        }

        location /force_expr {
            return 200 "uri=$force_expr";
        }

        location /runtime_err {
            return 200 "err=$runtime_err";
        }
    }
}

EOF

###############################################################################

$t->write_file('test.js', <<EOF);
    function variable(r) {
        return 'from_func';
    }

    export default {variable};

EOF

write_conf($t, '');

$t->try_run('no njs');

###############################################################################

like(http_get('/inline'), qr/uri=\/p\/inline\/post/, 'template inline');
like(http_get('/inline'), qr/foo=none/, 'inline headerIn');
like(http(
	'GET /inline HTTP/1.0' . CRLF
	. 'Foo: bar' . CRLF
	. 'Host: localhost' . CRLF . CRLF
), qr/foo=bar/, 'inline headerIn with header');

like(http_get('/inline'), qr/sum=3/, 'inline sum');
like(http_get('/var'), qr/var=FROM_FUNC/, 'inline var func ref');
like(http_get('/mixed'), qr/test_var=from_func/, 'mixed func ref');
like(http_get('/mixed'), qr/uri=\/p\/mixed\/post/, 'mixed inline');

like(http_get('/force_expr'), qr/uri=\/force_expr/, 'forced expression syntax');

http_get('/runtime_err');
like($t->read_file('error.log'), qr/included at.*nginx\.conf:/s,
	'runtime error location');

$t->stop();

###############################################################################

like(check($t, "js_set \$bad 'return 1';"),
	qr/SyntaxError.*included at.*nginx\.conf:/s,
	'inline syntax error location');

like(check($t, "js_set \$bad '1 +';"),
	qr/SyntaxError.*included at.*nginx\.conf:/s,
	'inline syntax error unexpected end');

$t->write_file('bad.js', 'export default {INVALID SYNTAX');

like(check($t, 'js_import bad.js;'),
	qr/\[emerg\].*SyntaxError/s,
	'file syntax error');

unlike(check($t, 'js_import bad.js;'),
	qr/included at/s,
	'file syntax error no inline location');

open my $fh, '>', $t->testdir() . '/error.log';
close $fh;

###############################################################################

sub write_conf {
	my ($t, $extra) = @_;

	$t->write_file_expand('nginx.conf',
		TEMPLATE_CONF =~ s/%%EXTRA_CONF%%/$extra/r);
}

sub check {
	my ($t, $extra) = @_;

	$t->stop();
	unlink $t->testdir() . '/error.log';

	write_conf($t, $extra);

	eval {
		open OLDERR, ">&", \*STDERR; close STDERR;
		$t->run();
		open STDERR, ">&", \*OLDERR;
	};

	return unless $@;

	my $log = $t->read_file('error.log');

	if ($ENV{TEST_NGINX_VERBOSE}) {
		map { Test::Nginx::log_core($_) } split(/^/m, $log);
	}

	return $log;
}

###############################################################################
