#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for js_shared_dict_zone directive.

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

my $t = Test::Nginx->new()->has(qw/http rewrite stream/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location / {
            return 200;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    js_shared_dict_zone zone=foo:32k;

    server {
        listen      127.0.0.1:8081;
        js_preread  test.preread_verify;
        proxy_pass  127.0.0.1:8090;
    }

    server {
        listen  127.0.0.1:8082;
        js_preread  test.control_access;
        proxy_pass  127.0.0.1:8080;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    import qs from 'querystring';

    function preread_verify(s) {
        var collect = Buffer.from([]);

        s.on('upstream', function (data, flags) {
            collect = Buffer.concat([collect, data]);

            if (collect.length >= 4 && collect.readUInt16BE(0) == 0xabcd) {
                let id = collect.slice(2,4);

                ngx.shared.foo.get(id) ? s.done(): s.deny();

            } else if (collect.length) {
                s.deny();
            }
        });
    }

    function control_access(s) {
        var req = '';

        s.on('upload', function(data, flags) {
            req += data;

            var n = req.search('\\n');
            if (n != -1) {
                var params = req.substr(0, n).split(' ')[1].split('?')[1];

                var args = qs.parse(params);
                switch (args.action) {
                case 'set':
                    ngx.shared.foo.set(args.key, args.value);
                    break;
                case 'del':
                    ngx.shared.foo.delete(args.key);
                    break;
                }

                s.done();
            }
        });
    }

    export default { preread_verify, control_access };

EOF

$t->try_run('no js_shared_dict_zone')->plan(9);

$t->run_daemon(\&stream_daemon, port(8090));
$t->waitforsocket('127.0.0.1:' . port(8090));

###############################################################################

is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQZ##"), "",
	'access failed, QZ is not in the shared dict');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQQ##"), "",
	'access failed, QQ is not in the shared dict');
like(get('/?action=set&key=QZ&value=1'), qr/200/, 'set foo.QZ');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQZ##"), "\xAB\xCDQZ##",
	'access granted');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQQ##"), "",
	'access failed, QQ is not in the shared dict');
like(get('/?action=del&key=QZ'), qr/200/, 'del foo.QZ');
like(get('/?action=set&key=QQ&value=1'), qr/200/, 'set foo.QQ');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQZ##"), "",
	'access failed, QZ is not in the shared dict');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQQ##"), "\xAB\xCDQQ##",
	'access granted');

###############################################################################

sub stream_daemon {
	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalAddr => '127.0.0.1:' . port(8090),
		Listen => 5,
		Reuse => 1
	)
		or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	while (my $client = $server->accept()) {
		$client->autoflush(1);

		log2c("(new connection $client)");

		$client->sysread(my $buffer, 65536) or next;

		log2i("$client $buffer");

		log2o("$client $buffer");

		$client->syswrite($buffer);

		close $client;
	}
}

sub log2i { Test::Nginx::log_core('|| <<', @_); }
sub log2o { Test::Nginx::log_core('|| >>', @_); }
sub log2c { Test::Nginx::log_core('||', @_); }

sub get {
	my ($url, %extra) = @_;

	my $s = IO::Socket::INET->new(
		Proto => 'tcp',
		PeerAddr => '127.0.0.1:' . port(8082)
	) or die "Can't connect to nginx: $!\n";

	return http_get($url, socket => $s);
}

###############################################################################
