#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, fetch method.

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

eval { require JSON::PP; };
plan(skip_all => "JSON::PP not installed") if $@;

my $t = Test::Nginx->new()->has(qw/http/)
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

        js_fetch_max_response_buffer_size 128k;

        location /njs {
            js_content test.njs;
        }

        location /engine {
            js_content test.engine;
        }

        location /broken {
            js_content test.broken;
        }

        location /broken_response {
            js_content test.broken_response;
        }

        location /body {
            js_content test.body;
        }

        location /body_special {
            js_content test.body_special;
        }

        location /chain {
            js_content test.chain;
        }

        location /chunked_ok {
            js_content test.chunked_ok;
        }

        location /chunked_fail {
            js_content test.chunked_fail;
        }

        location /header {
            js_content test.header;
        }

        location /host_header {
            js_content test.host_header;
        }

        location /header_iter {
            js_content test.header_iter;
        }

        location /multi {
            js_content test.multi;
        }

        location /property {
            js_content test.property;
        }

        location /loc {
            js_content test.loc;
        }

        location /json { }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  localhost;

        location /loc {
            js_content test.loc;
        }

        location /host {
            return 200 $http_host;
        }
    }
}

EOF

my $p0 = port(8080);
my $p1 = port(8081);
my $p2 = port(8082);

$t->write_file('json', '{"a":[1,2], "b":{"c":"FIELD"}}');

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function engine(r) {
        r.return(200, njs.engine);
    }

    function body(r) {
        var loc = r.args.loc;
        var getter = r.args.getter;

        function query(obj) {
            var path = r.args.path;
            var retval = (getter == 'arrayBuffer') ? Buffer.from(obj).toString()
                                                   : obj;

            if (path) {
                retval = path.split('.').reduce((a, v) => a[v], obj);
            }

            return JSON.stringify(retval);
        }

        ngx.fetch(`http://127.0.0.1:$p0/\${loc}`)
        .then(reply => reply[getter]())
        .then(data => r.return(200, query(data)))
        .catch(e => r.return(501, e.message))
    }

    function property(r) {
        var opts = {headers:{}};

        if (r.args.code) {
            opts.headers.code = r.args.code;
        }

        var p = ngx.fetch('http://127.0.0.1:$p0/loc', opts)

        if (r.args.readBody) {
            p = p.then(rep =>
                 rep.text().then(body => {rep.text = body; return rep;}))
        }

        p.then(reply => r.return(200, reply[r.args.pr]))
        .catch(e => r.return(501, e.message))
    }

    function process_errors(r, tests) {
        var results = [];

        tests.forEach(args => {
            ngx.fetch.apply(r, args)
            .then(reply => {
                r.return(400, '["unexpected then"]');
            })
            .catch(e => {
                results.push(e.message);

                if (results.length == tests.length) {
                    results.sort();
                    r.return(200, JSON.stringify(results));
                }
            })
        })
    }

    function broken(r) {
        var tests = [
            ['http://127.0.0.1:1/loc'],
            ['http://127.0.0.1:80800/loc'],
            [Symbol.toStringTag],
        ];

        return process_errors(r, tests);
    }

    function broken_response(r) {
        var tests = [
            ['http://127.0.0.1:$p2/status_line'],
            ['http://127.0.0.1:$p2/length'],
            ['http://127.0.0.1:$p2/header'],
            ['http://127.0.0.1:$p2/headers'],
            ['http://127.0.0.1:$p2/content_length'],
        ];

        return process_errors(r, tests);
    }

    function chain(r) {
        var results = [];
        var reqs = [
             ['http://127.0.0.1:$p0/loc'],
             ['http://127.0.0.1:$p1/loc'],
           ];

           function next(reply) {
              if (reqs.length == 0) {
                 r.return(200, "SUCCESS");
                 return;
              }

              ngx.fetch.apply(r, reqs.pop())
              .then(next)
              .catch(e => r.return(400, e.message))
           }

           next();
    }

    function chunked_ok(r) {
        var results = [];
        var tests = [
            ['http://127.0.0.1:$p2/big/ok', {max_response_body_size:128000}],
            ['http://127.0.0.1:$p2/chunked/ok'],
            ['http://127.0.0.1:$p2/chunked/big'],
        ];

        function collect(v) {
            results.push(v);

            if (results.length == tests.length) {
                r.return(200);
            }
        }

        tests.forEach(args => {
            ngx.fetch.apply(r, args)
            .then(reply => reply.text())
            .then(body => collect(body.length))
        })
    }

    function chunked_fail(r) {
        var results = [];
        var tests = [
            ['http://127.0.0.1:$p2/big', {max_response_body_size:128000}],
            ['http://127.0.0.1:$p2/chunked'],
            ['http://127.0.0.1:$p2/chunked/big', {max_response_body_size:128}],
        ];

        function collect(v) {
            results.push(v);

            if (results.length == tests.length) {
                r.return(200);
            }
        }

        tests.forEach(args => {
            ngx.fetch.apply(r, args)
            .then(reply => reply.text())
            .catch(e => collect(e.message))
        })
    }

    function header(r) {
        var url = `http://127.0.0.1:$p2/\${r.args.loc}`;
        var method = r.args.method ? r.args.method : 'get';

        var p = ngx.fetch(url)

        if (r.args.readBody) {
            p = p.then(rep =>
                 rep.text().then(body => {rep.text = body; return rep;}))
        }

        p.then(reply => {
            var h = reply.headers[method](r.args.h);
            r.return(200, njs.dump(h));
        })
        .catch(e => r.return(501, e.message))
    }

    async function host_header(r) {
        const reply = await ngx.fetch(`http://127.0.0.1:$p1/host`,
                                      {headers: {Host: r.args.host}});
        const body = await reply.text();
        r.return(200, body);
    }

    async function body_special(r) {
        let opts = {};

        if (r.args.method) {
            opts.method = r.args.method;
        }

        let reply = await ngx.fetch(`http://127.0.0.1:$p2/\${r.args.loc}`,
                                    opts);
        let body = await reply.text();

        r.return(200, body != '' ? body : '<empty>');
    }

    async function header_iter(r) {
        let url = `http://127.0.0.1:$p2/\${r.args.loc}`;

        let response = await ngx.fetch(url);

        let headers = response.headers;
        let out = [];
        for (let key in response.headers) {
            if (key != 'Connection') {
                out.push(`\${key}:\${headers.get(key)}`);
            }
        }

        r.return(200, njs.dump(out));
    }

    function multi(r) {
        var results = [];
        var tests = [
             [
              'http://127.0.0.1:$p0/loc',
               { headers: {Code: 201}},
             ],
             [
              'http://127.0.0.1:$p0/loc',
               { method:'POST', headers: {Code: 401}, body: 'OK'},
             ],
             [
              'http://127.0.0.1:$p1/loc',
               { method:'PATCH',
                 headers: {bar:'xxx'}},
             ],
           ];

        function cmp(a,b) {
            if (a.b > b.b) {return 1;}
            if (a.b < b.b) {return -1;}
            return 0
        }

        tests.forEach(args => {
            ngx.fetch.apply(r, args)
            .then(rep =>
                 rep.text().then(body => {rep.text = body; return rep;}))
            .then(rep => {
                results.push({b:rep.text,
                              c:rep.status,
                              u:rep.url});

                if (results.length == tests.length) {
                    results.sort(cmp);
                    r.return(200, JSON.stringify(results));
                }
            })
            .catch(e => {
                r.return(400, `["\${e.message}"]`);
                throw e;
            })
        })

        if (r.args.throw) {
            throw 'Oops';
        }
    }

    function str(v) { return v ? v : ''};

    function loc(r) {
        var v = r.variables;
        var body = str(r.requestText);
        var bar = str(r.headersIn.bar);
        var c = r.headersIn.code ? Number(r.headersIn.code) : 200;
        r.return(c, `\${v.request_method}:\${bar}:\${body}`);
    }

     export default {njs: test_njs, body, broken, broken_response, body_special,
                     chain, chunked_ok, chunked_fail, header, header_iter,
                     host_header, multi, loc, property, engine};
EOF

$t->try_run('no njs.fetch');

plan(skip_all => 'not yet') if http_get('/engine') =~ /QuickJS$/m;

$t->plan(36);

$t->run_daemon(\&http_daemon, port(8082));
$t->waitforsocket('127.0.0.1:' . port(8082));

###############################################################################

like(http_get('/body?getter=arrayBuffer&loc=loc'), qr/200 OK.*"GET::"$/s,
	'fetch body arrayBuffer');
like(http_get('/body?getter=text&loc=loc'), qr/200 OK.*"GET::"$/s,
	'fetch body text');
like(http_get('/body?getter=json&loc=json&path=b.c'),
	qr/200 OK.*"FIELD"$/s, 'fetch body json');
like(http_get('/body?getter=json&loc=loc'), qr/501/s,
	'fetch body json invalid');
like(http_get('/body_special?loc=parted'), qr/200 OK.*X{32000}$/s,
	'fetch body parted');
like(http_get('/property?pr=bodyUsed'), qr/false$/s,
	'fetch bodyUsed false');
like(http_get('/property?pr=bodyUsed&readBody=1'), qr/true$/s,
	'fetch bodyUsed true');
like(http_get('/property?pr=ok'), qr/200 OK.*true$/s,
	'fetch ok true');
like(http_get('/property?pr=ok&code=401'), qr/200 OK.*false$/s,
	'fetch ok false');
like(http_get('/property?pr=redirected'), qr/200 OK.*false$/s,
	'fetch redirected false');
like(http_get('/property?pr=statusText'), qr/200 OK.*OK$/s,
	'fetch statusText OK');
like(http_get('/property?pr=statusText&code=403'), qr/200 OK.*Forbidden$/s,
	'fetch statusText Forbidden');
like(http_get('/property?pr=type'), qr/200 OK.*basic$/s,
	'fetch type');
like(http_get('/header?loc=duplicate_header&h=BAR'), qr/200 OK.*c$/s,
	'fetch header');
like(http_get('/header?loc=duplicate_header&h=BARR'), qr/200 OK.*null$/s,
	'fetch no header');
like(http_get('/header?loc=duplicate_header&h=foo'), qr/200 OK.*a, ?b$/s,
	'fetch header duplicate');
like(http_get('/header?loc=duplicate_header&h=BAR&method=getAll'),
	qr/200 OK.*\['c']$/s, 'fetch getAll header');
like(http_get('/header?loc=duplicate_header&h=BARR&method=getAll'),
	qr/200 OK.*\[]$/s, 'fetch getAll no header');
like(http_get('/header?loc=duplicate_header&h=FOO&method=getAll'),
	qr/200 OK.*\['a','b']$/s, 'fetch getAll duplicate');
like(http_get('/header?loc=duplicate_header&h=bar&method=has'),
	qr/200 OK.*true$/s, 'fetch header has');
like(http_get('/header?loc=duplicate_header&h=buz&method=has'),
	qr/200 OK.*false$/s, 'fetch header does not have');
like(http_get('/header?loc=chunked/big&h=BAR&readBody=1'), qr/200 OK.*xxx$/s,
	'fetch chunked header');
is(get_json('/multi'),
	'[{"b":"GET::","c":201,"u":"http://127.0.0.1:'.$p0.'/loc"},' .
	'{"b":"PATCH:xxx:","c":200,"u":"http://127.0.0.1:'.$p1.'/loc"},' .
	'{"b":"POST::OK","c":401,"u":"http://127.0.0.1:'.$p0.'/loc"}]',
	'fetch multi');
like(http_get('/multi?throw=1'), qr/500/s, 'fetch destructor');
like(http_get('/broken'), qr/200/s, 'fetch broken');
like(http_get('/broken_response'), qr/200/s, 'fetch broken response');
like(http_get('/chunked_ok'), qr/200/s, 'fetch chunked ok');
like(http_get('/chunked_fail'), qr/200/s, 'fetch chunked fail');
like(http_get('/chain'), qr/200 OK.*SUCCESS$/s, 'fetch chain');

like(http_get('/header_iter?loc=duplicate_header_large'),
	qr/\['A:a','B:a','C:a','D:a','E:a','F:a','G:a','H:a','Moo:a, ?b']$/s,
	'fetch header duplicate large');

TODO: {
local $TODO = 'not yet' unless has_version('0.7.7');

like(http_get('/body_special?loc=no_content_length'),
	qr/200 OK.*CONTENT-BODY$/s, 'fetch body without content-length');
like(http_get('/body_special?loc=no_content_length/parted'),
	qr/200 OK.*X{32000}$/s, 'fetch body without content-length parted');

}

TODO: {
local $TODO = 'not yet' unless has_version('0.7.8');

like(http_get('/body_special?loc=head&method=HEAD'),
	qr/200 OK.*<empty>$/s, 'fetch head method');
like(http_get('/body_special?loc=length&method=head'),
	qr/200 OK.*<empty>$/s, 'fetch head method lower case');

}

TODO: {
local $TODO = 'not yet' unless has_version('0.8.0');

like(http_get('/host_header?host=FOOBAR'), qr/200 OK.*FOOBAR$/s,
	'fetch host header');
}

TODO: {
local $TODO = 'not yet' unless has_version('0.8.2');

like(http_get('/body_special?loc=head/large&method=HEAD'),
	qr/200 OK.*<empty>$/s, 'fetch head method large content-length');
}

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

sub recode {
	my $json;
	eval { $json = JSON::PP::decode_json(shift) };

	if ($@) {
		return "<failed to parse JSON>";
	}

	JSON::PP->new()->canonical()->encode($json);
}

sub get_json {
	http_get(shift) =~ /\x0d\x0a?\x0d\x0a?(.*)/ms;
	recode($1);
}

###############################################################################

sub http_daemon {
	my $port = shift;

	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalAddr => '127.0.0.1:' . $port,
		Listen => 5,
		Reuse => 1
	) or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	while (my $client = $server->accept()) {
		$client->autoflush(1);

		my $headers = '';
		my $uri = '';

		while (<$client>) {
			$headers .= $_;
			last if (/^\x0d?\x0a?$/);
		}

		$uri = $1 if $headers =~ /^\S+\s+([^ ]+)\s+HTTP/i;

		if ($uri eq '/status_line') {
			print $client
				"HTTP/1.1 2A";

		} elsif ($uri eq '/content_length') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Content-Length: " . CRLF .
				"Connection: close" . CRLF .
				CRLF;

		} elsif ($uri eq '/header') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"@#" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

		} elsif ($uri eq '/duplicate_header') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Foo: a" . CRLF .
				"bar: c" . CRLF .
				"Foo: b" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

		} elsif ($uri eq '/duplicate_header_large') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"A: a" . CRLF .
				"B: a" . CRLF .
				"C: a" . CRLF .
				"D: a" . CRLF .
				"E: a" . CRLF .
				"F: a" . CRLF .
				"G: a" . CRLF .
				"H: a" . CRLF .
				"Moo: a" . CRLF .
				"Moo: b" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

		} elsif ($uri eq '/headers') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Connection: close" . CRLF;

		} elsif ($uri eq '/length') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Content-Length: 100" . CRLF .
				"Connection: close" . CRLF .
				CRLF .
				"unfinished" . CRLF;

		} elsif ($uri eq '/head') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Content-Length: 100" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

		} elsif ($uri eq '/head/large') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Content-Length: 1000000" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

		} elsif ($uri eq '/parted') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Content-Length: 32000" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

			for (1 .. 4) {
				select undef, undef, undef, 0.01;
				print $client "X" x 8000;
			}

		} elsif ($uri eq '/no_content_length') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Connection: close" . CRLF .
				CRLF .
				"CONTENT-BODY";

		} elsif ($uri eq '/no_content_length/parted') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

			for (1 .. 4) {
				select undef, undef, undef, 0.01;
				print $client "X" x 8000;
			}

		} elsif ($uri eq '/big') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Content-Length: 100100" . CRLF .
				"Connection: close" . CRLF .
				CRLF;
			for (1 .. 1000) {
				print $client ("X" x 98) . CRLF;
			}
			print $client "unfinished" . CRLF;

		} elsif ($uri eq '/big/ok') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Content-Length: 100010" . CRLF .
				"Connection: close" . CRLF .
				CRLF;
			for (1 .. 1000) {
				print $client ("X" x 98) . CRLF;
			}
			print $client "finished" . CRLF;

		} elsif ($uri eq '/chunked') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Transfer-Encoding: chunked" . CRLF .
				"Connection: close" . CRLF .
				CRLF .
				"ff" . CRLF .
				"unfinished" . CRLF;

		} elsif ($uri eq '/chunked/ok') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Transfer-Encoding: chunked" . CRLF .
				"Connection: close" . CRLF .
				CRLF .
				"a" . CRLF .
				"finished" . CRLF .
				CRLF . "0" . CRLF . CRLF;
		} elsif ($uri eq '/chunked/big') {
			print $client
				"HTTP/1.1 200 OK" . CRLF .
				"Transfer-Encoding: chunked" . CRLF .
				"Bar: xxx" . CRLF .
				"Connection: close" . CRLF .
				CRLF;

			for (1 .. 100) {
				print $client "ff" . CRLF . ("X" x 255) . CRLF;
			}

		    print $client  "0" . CRLF . CRLF;
		}
	}
}

###############################################################################
