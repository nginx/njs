#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, fetch method.

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

my $t = Test::Nginx->new()->has(qw/http stream/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content test.njs;
        }

        location /engine {
            js_content test.engine;
        }

        location /validate {
            js_content test.validate;
        }

        location /success {
            return 200;
        }

        location /fail {
            return 403;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    server {
        listen      127.0.0.1:8081;
        js_preread  test.preread_verify;
        proxy_pass  127.0.0.1:8090;
    }

    server {
        listen      127.0.0.1:8082;
        js_filter   test.filter_verify;
        proxy_pass  127.0.0.1:8091;
    }

    server {
        listen      127.0.0.1:8083;
        js_access   test.access_ok;
        proxy_pass  127.0.0.1:8090;
    }

    server {
        listen      127.0.0.1:8084;
        js_access   test.access_nok;
        proxy_pass  127.0.0.1:8090;
    }
}

EOF

my $p = port(8080);

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function engine(r) {
        r.return(200, njs.engine);
    }

    function validate(r) {
        r.return((r.requestText == 'QZ') ? 200 : 403);
    }

    function preread_verify(s) {
        var collect = Buffer.from([]);

        s.on('upstream', async function (data, flags) {
            collect = Buffer.concat([collect, data]);

            if (collect.length >= 4 && collect.readUInt16BE(0) == 0xabcd) {
                s.off('upstream');

                let reply = await ngx.fetch('http://127.0.0.1:$p/validate',
                                            {body: collect.slice(2,4)});

                (reply.status == 200) ? s.done(): s.deny();

            } else if (collect.length) {
                s.deny();
            }
        });
    }

    function filter_verify(s) {
        var collect = Buffer.from([]);

        s.on('upstream', async function (data, flags) {
            collect = Buffer.concat([collect, data]);

            if (collect.length >= 4 && collect.readUInt16BE(0) == 0xabcd) {
                s.off('upstream');

                let reply = await ngx.fetch('http://127.0.0.1:$p/validate',
                                            {body: collect.slice(2,4)});

                if (reply.status == 200) {
                    s.send(collect.slice(4), flags);

                } else {
                    s.send("__CLOSE__", flags);
                }
            }
        });
    }

    async function access_ok(s) {
        let reply = await ngx.fetch('http://127.0.0.1:$p/success');

        (reply.status == 200) ? s.allow(): s.deny();
    }

    async function access_nok(s) {
        let reply = await ngx.fetch('http://127.0.0.1:$p/fail');

        (reply.status == 200) ? s.allow(): s.deny();
    }

    export default {njs: test_njs, validate, preread_verify, filter_verify,
                    access_ok, access_nok, engine};
EOF

$t->try_run('no stream njs available');

plan(skip_all => 'not yet') if http_get('/engine') =~ /QuickJS$/m;

$t->plan(9);

$t->run_daemon(\&stream_daemon, port(8090), port(8091));
$t->waitforsocket('127.0.0.1:' . port(8090));
$t->waitforsocket('127.0.0.1:' . port(8091));

###############################################################################

is(stream('127.0.0.1:' . port(8081))->io('###'), '', 'preread not enough');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQZ##"), "\xAB\xCDQZ##",
	'preread validated');
is(stream('127.0.0.1:' . port(8081))->io("\xAC\xCDQZ##"), '',
	'preread invalid magic');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQQ##"), '',
	'preread validation failed');

TODO: {
todo_skip 'leaves coredump', 3 unless $ENV{TEST_NGINX_UNSAFE}
	or has_version('0.7.7');

my $s = stream('127.0.0.1:' . port(8082));
is($s->io("\xAB\xCDQZ##", read => 1), '##', 'filter validated');
is($s->io("@@", read => 1), '@@', 'filter off');

is(stream('127.0.0.1:' . port(8082))->io("\xAB\xCDQQ##"), '',
	'filter validation failed');

}

is(stream('127.0.0.1:' . port(8083))->io('ABC'), 'ABC', 'access fetch ok');
is(stream('127.0.0.1:' . port(8084))->io('ABC'), '', 'access fetch nok');

###############################################################################

sub has_version {
	my $need = shift;

	http_get('/njs') =~ /^([.0-9]+)$/m;

	my @v = split(/\./, $1);
	my ($n, $v);

	for $n (split(/\./, $need)) {
		$v = shift @v || 0;
		return 0 if $n > $v;
		return 1 if $v > $n;
	}

	return 1;
}

###############################################################################

sub stream_daemon {
	my (@ports) = @_;
	my (@socks, @clients);

	for my $port (@ports) {
		my $server = IO::Socket::INET->new(
			Proto => 'tcp',
			LocalAddr => "127.0.0.1:$port",
			Listen => 5,
			Reuse => 1
		)
			or die "Can't create listening socket: $!\n";
		push @socks, $server;
	}

	my $sel = IO::Select->new(@socks);

	local $SIG{PIPE} = 'IGNORE';

	while (my @ready = $sel->can_read) {
		foreach my $fh (@ready) {
			if (grep $_ == $fh, @socks) {
				my $new = $fh->accept;
				$new->autoflush(1);
				$sel->add($new);

			} elsif (stream_handle_client($fh)
				|| $fh->sockport() == port(8090))
			{
				$sel->remove($fh);
				$fh->close;
			}
		}
	}
}

sub stream_handle_client {
	my ($client) = @_;

	log2c("(new connection $client)");

	$client->sysread(my $buffer, 65536) or return 1;

	log2i("$client $buffer");

	if ($buffer eq "__CLOSE__") {
		return 1;
	}

	log2o("$client $buffer");

	$client->syswrite($buffer);

	return 0;
}

sub log2i { Test::Nginx::log_core('|| <<', @_); }
sub log2o { Test::Nginx::log_core('|| >>', @_); }
sub log2c { Test::Nginx::log_core('||', @_); }

###############################################################################
