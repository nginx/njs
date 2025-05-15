#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for js_shared_dict_zone directive, state= parameter.

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

    js_shared_dict_zone zone=bar:64k type=string state=bar.json;
    js_shared_dict_zone zone=waka:32k timeout=1000s type=number state=waka.json;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /add {
            js_content test.add;
        }

        location /clear {
            js_content test.clear;
        }

        location /delete {
            js_content test.del;
        }

        location /get {
            js_content test.get;
        }

        location /incr {
            js_content test.incr;
        }

        location /pop {
            js_content test.pop;
        }

        location /set {
            js_content test.set;
        }
    }
}

EOF

$t->write_file('bar.json', <<EOF);
{"waka":{"value":"foo","expire":0},
 "bar": { "value"  :"\\u0061\\u0062\\u0063"},
"FOO \\n": { "value"  :  "BAZ", "unexpected_str": "u\\r" },
 "X": { "valu\\u0065"  :  "\\n" , "unexpected_num": 23.1 }  ,
 "\\u0061\\u0062\\u0063": { "value"  :  "def" } ,
}
EOF

$t->write_file('test.js', <<'EOF');
    function convertToValue(dict, v) {
        if (dict.type == 'number') {
            return parseInt(v);

        } else if (v == 'empty') {
            v = '';
        }

        return v;
    }

    function add(r) {
        var dict = ngx.shared[r.args.dict];
        var value = convertToValue(dict, r.args.value);

        if (r.args.timeout) {
            var timeout = Number(r.args.timeout);
            r.return(200, dict.add(r.args.key, value, timeout));

        } else {
            r.return(200, dict.add(r.args.key, value));
        }
    }

    function clear(r) {
        var dict = ngx.shared[r.args.dict];
        var result = dict.clear();
        r.return(200, result === undefined ? 'undefined' : result);
    }

    function del(r) {
        var dict = ngx.shared[r.args.dict];
        r.return(200, dict.delete(r.args.key));
    }

    function get(r) {
        var dict = ngx.shared[r.args.dict];
        var val = dict.get(r.args.key);

        if (val == '') {
            val = 'empty';

        } else if (val === undefined) {
            val = 'undefined';
        }

        r.return(200, val);
    }

    function incr(r) {
        var dict = ngx.shared[r.args.dict];
        var def = r.args.def ? parseInt(r.args.def) : 0;

        if (r.args.timeout) {
            var timeout = Number(r.args.timeout);
            var val = dict.incr(r.args.key, parseInt(r.args.by), def, timeout);
            r.return(200, val);

        } else {
            var val = dict.incr(r.args.key, parseInt(r.args.by), def);
            r.return(200, val);
        }
    }

    function pop(r) {
        var dict = ngx.shared[r.args.dict];
        var val = dict.pop(r.args.key);
        if (val == '') {
            val = 'empty';

        } else if (val === undefined) {
            val = 'undefined';
        }

        r.return(200, val);
    }

    function set(r) {
        var dict = ngx.shared[r.args.dict];
        var value = convertToValue(dict, r.args.value);

        if (r.args.timeout) {
            var timeout = Number(r.args.timeout);
            r.return(200, dict.set(r.args.key, value, timeout) === dict);

        } else {
            r.return(200, dict.set(r.args.key, value) === dict);
        }
    }

    export default { add, clear, del, get, incr, pop, set };
EOF

$t->try_run('no js_shared_dict_zone with state=')->plan(11);

###############################################################################

like(http_get('/get?dict=bar&key=waka'), qr/foo/, 'get bar.waka');
like(http_get('/get?dict=bar&key=bar'), qr/abc/, 'get bar.bar');
like(http_get('/get?dict=bar&key=FOO%20%0A'), qr/BAZ/, 'get bar["FOO \\n"]');
like(http_get('/get?dict=bar&key=abc'), qr/def/, 'get bar.abc');

http_get('/set?dict=bar&key=waka&value=foo2');
http_get('/delete?dict=bar&key=bar');

http_get('/set?dict=waka&key=foo&value=42');

select undef, undef, undef, 1.1;

$t->reload();

my $bar_state = read_state($t, 'bar.json');
my $waka_state = read_state($t, 'waka.json');

is($bar_state->{waka}->{value}, 'foo2', 'get bar.waka from state');
is($bar_state->{bar}, undef, 'no bar.bar in state');
is($waka_state->{foo}->{value}, '42', 'get waka.foo from state');
like($waka_state->{foo}->{expire}, qr/^\d+$/, 'waka.foo expire');

http_get('/pop?dict=bar&key=FOO%20%0A');

http_get('/incr?dict=waka&key=foo&by=1');

select undef, undef, undef, 1.1;

$bar_state = read_state($t, 'bar.json');
$waka_state = read_state($t, 'waka.json');

is($bar_state->{'FOO \\n'}, undef, 'no bar.FOO \\n in state');
is($waka_state->{foo}->{value}, '43', 'get waka.foo from state');

http_get('/clear?dict=bar');

select undef, undef, undef, 1.1;

$bar_state = read_state($t, 'bar.json');

is($bar_state->{waka}, undef, 'no bar.waka in state');

###############################################################################

sub decode_json {
	my $json;
	eval { $json = JSON::PP::decode_json(shift) };

	if ($@) {
		return "<failed to parse JSON>";
	}

	return $json;
}

sub read_state {
	my ($self, $file) = @_;
	my $json = $self->read_file($file);

	if ($json) {
		$json = decode_json($json);
	}

	return $json;
}

###############################################################################
