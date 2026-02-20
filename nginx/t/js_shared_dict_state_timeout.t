#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for js_shared_dict_zone directive, state= with timeout= parameters.

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

    js_shared_dict_zone zone=waka:32k timeout=1000s type=number state=waka.json;
    js_shared_dict_zone zone=exp:32k timeout=1000s type=string state=exp.json;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /get {
            js_content test.get;
        }

        location /incr {
            js_content test.incr;
        }

        location /set {
            js_content test.set;
        }
    }
}

EOF

my $now_ms = time() * 1000;
my $past_expire = $now_ms - 3600000;
my $future_expire = $now_ms + 3600000;

$t->write_file('exp.json', <<EOF);
{"past":{"value":"gone","expire":$past_expire},
 "future":{"value":"here","expire":$future_expire},
 "noexp":{"value":"fresh","expire":0}}
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

    export default { get, incr, set };
EOF

$t->try_run('js_shared_dict_zone state with timeout no support on 32-bit')
	->plan(13);

###############################################################################

http_get('/set?dict=waka&key=foo&value=42');

select undef, undef, undef, 1.1;

$t->reload();

my $waka_state = read_state($t, 'waka.json');

is($waka_state->{foo}->{value}, '42', 'get waka.foo from state');
like($waka_state->{foo}->{expire}, qr/^\d+$/, 'waka.foo expire');

http_get('/incr?dict=waka&key=foo&by=1');

select undef, undef, undef, 1.1;

$waka_state = read_state($t, 'waka.json');

is($waka_state->{foo}->{value}, '43', 'get waka.foo from state');

like(http_get('/get?dict=exp&key=past'), qr/undefined/,
	'expired entry cleaned on load');

like(http_get('/get?dict=exp&key=future'), qr/here/,
	'non-expired entry survives load');

like(http_get('/get?dict=exp&key=noexp'), qr/fresh/,
	'expire=0 entry accessible after load');

my $before_set = time_ms();
http_get('/set?dict=waka&key=exp_test&value=99&timeout=30000');
my $after_set = time_ms();

select undef, undef, undef, 1.1;

$waka_state = read_state($t, 'waka.json');
my $exp_val = $waka_state->{exp_test}->{expire};
ok($exp_val >= $before_set + 25000 && $exp_val <= $after_set + 35000,
	'expire field value in correct range for 30s timeout');

my $expire_before = $waka_state->{exp_test}->{expire};

$t->stop();
$t->run();

like(http_get('/get?dict=waka&key=exp_test'), qr/99/,
	'value survives restart with valid expire');
like(http_get('/get?dict=exp&key=future'), qr/here/,
	'non-expired entry survives restart');
like(http_get('/get?dict=exp&key=noexp'), qr/fresh/,
	'expire=0 entry survives restart');

select undef, undef, undef, 1.1;

$waka_state = read_state($t, 'waka.json');
is($waka_state->{exp_test}->{expire}, $expire_before,
	'expire value preserved after restart');

my $exp_state = read_state($t, 'exp.json');
is($exp_state->{past}, undef, 'expired entry removed from state file');

ok(defined $exp_state->{noexp}->{expire}
	&& $exp_state->{noexp}->{expire} > 0,
	'expire=0 entry gets expire assigned in state');

###############################################################################

sub time_ms {
	return time() * 1000;
}

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
