#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for js_shared_dict_zone directive.

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

my $t = Test::Nginx->new()->has(qw/http/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    js_shared_dict_zone zone=foo:32k timeout=2s evict;
    js_shared_dict_zone zone=bar:64k type=string;
    js_shared_dict_zone zone=waka:32k timeout=1000s type=number;
    js_shared_dict_zone zone=no_timeout:32k;
    js_shared_dict_zone zone=overflow:32k;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content test.njs;
        }

        location /add {
            js_content test.add;
        }

        location /capacity {
            js_content test.capacity;
        }

        location /chain {
            js_content test.chain;
        }

        location /clear {
            js_content test.clear;
        }

        location /delete {
            js_content test.del;
        }

        location /free_space {
            js_content test.free_space;
        }

        location /get {
            js_content test.get;
        }

        location /has {
            js_content test.has;
        }

        location /incr {
            js_content test.incr;
        }

        location /items {
            js_content test.items;
        }

        location /keys {
            js_content test.keys;
        }

        location /name {
            js_content test.name;
        }

        location /evict_stress {
            js_content test.evict_stress;
        }

        location /overflow {
            js_content test.overflow;
        }

        location /pop {
            js_content test.pop;
        }

        location /replace {
            js_content test.replace;
        }

        location /set {
            js_content test.set;
        }

        location /set_clear {
            js_content test.set_clear;
        }

        location /size {
            js_content test.size;
        }

        location /zones {
            js_content test.zones;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function test_njs(r) {
        r.return(200, njs.version);
    }

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

    function capacity(r) {
        var dict = ngx.shared[r.args.dict];
        r.return(200, dict.capacity);
    }

    function chain(r) {
        var dict = ngx.shared[r.args.dict];
        var val = dict.set(r.args.key, r.args.value).get(r.args.key);
        r.return(200, val);
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

    function free_space(r) {
        var dict = ngx.shared[r.args.dict];
        var free_space = dict.freeSpace();

        r.return(200, free_space >= 0 && free_space <= dict.capacity);
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

    function has(r) {
        var dict = ngx.shared[r.args.dict];
        r.return(200, dict.has(r.args.key));
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

    function keys(r) {
        var ks;

        if (r.args.max) {
            ks = ngx.shared[r.args.dict].keys(parseInt(r.args.max));

        } else {
            ks = ngx.shared[r.args.dict].keys();
        }

        var sorted = ks.toSorted();
        r.return(200, (sorted.length) ? sorted.join(",") : "empty");
    }

    function items(r) {
        var kvs;

        if (r.args.max) {
            kvs = ngx.shared[r.args.dict].items(parseInt(r.args.max));

        } else {
            kvs = ngx.shared[r.args.dict].items();
        }

        r.return(200, kvs.toSorted().join("|"));
    }

    function name(r) {
        r.return(200, ngx.shared[r.args.dict].name);
    }

    function replace(r) {
        var dict = ngx.shared[r.args.dict];
        var value = convertToValue(dict, r.args.value);
        r.return(200, dict.replace(r.args.key, value));
    }

    function evict_stress(r) {
        var dict = ngx.shared.foo;
        dict.clear();

        var v = 'x'.repeat(1024);
        var n = 64;
        var failed = 0;

        for (var i = 0; i < n; i++) {
            try {
                dict.set('stress_' + i, v);
            } catch (e) {
                failed++;
            }
        }

        var last = dict.get('stress_' + (n - 1));
        var first = dict.get('stress_0');

        var last_s = last !== undefined ? 'exists' : 'missing';
        var first_s = first !== undefined ? 'exists' : 'missing';

        r.return(200, `failed:${failed},last:${last_s},first:${first_s}`);
    }

    function overflow(r) {
        var dict = ngx.shared.overflow;

        var v = 'x'.repeat(1024);
        try {
            for (var i = 0; i < 1024; i++) {
                dict.set('key' + i, v);
            }
        } catch (e) {
            if (e instanceof SharedMemoryError) {
                r.return(200, 'SharedMemoryError');
                return;
            }

            r.return(200, e.toString());
            return;
        }

        r.return(200, 'no exception');
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

    function size(r) {
        var dict = ngx.shared[r.args.dict];
        r.return(200, `size: ${dict.size()}`);
    }

    function set_clear(r) {
        var dict = ngx.shared.no_timeout;
        dict.clear();
        dict.set("test", "test value");
        dict.set("test1", "test1 value");
        dict.clear();
        r.return(200, `size: ${dict.size()}`);
    }


    function zones(r) {
        r.return(200, Object.keys(ngx.shared).sort());
    }

    export default { add, capacity, chain, clear, del, evict_stress,
                     free_space, get, has, incr, items, keys, name,
                     njs: test_njs, pop, replace, set, set_clear, size,
                     zones, overflow };
EOF

$t->try_run('no js_shared_dict_zone');

$t->plan(56);

###############################################################################

like(http_get('/zones'), qr/bar,foo/, 'available zones');
like(http_get('/capacity?dict=foo'), qr/32768/, 'foo capacity');
like(http_get('/capacity?dict=bar'), qr/65536/, 'bar capacity');
like(http_get('/free_space?dict=foo'), qr/true/, 'foo free space');
like(http_get('/name?dict=foo'), qr/foo/, 'foo name');
like(http_get('/size?dict=foo'), qr/size: 0/, 'no of items in foo');

like(http_get('/add?dict=foo&key=FOO&value=xxx'), qr/true/, 'add foo.FOO');
like(http_get('/add?dict=foo&key=FOO&value=xxx'), qr/false/,
	'failed add foo.FOO');
like(http_get('/set?dict=foo&key=FOO2&value=yyy'), qr/true/, 'set foo.FOO2');
like(http_get('/set?dict=foo&key=FOO3&value=empty'), qr/true/, 'set foo.FOO3');
like(http_get('/set?dict=bar&key=FOO&value=zzz'), qr/true/, 'set bar.FOO');
like(http_get('/set?dict=waka&key=FOO&value=42'), qr/true/, 'set waka.FOO');
like(http_get('/chain?dict=bar&key=FOO2&value=aaa'), qr/aaa/, 'chain bar.FOO2');

like(http_get('/incr?dict=waka&key=FOO&by=5'), qr/47/, 'incr waka.FOO');
like(http_get('/incr?dict=waka&key=FOO2&by=7777'), qr/7777/, 'incr waka.FOO2');
like(http_get('/incr?dict=waka&key=FOO2&by=2'), qr/7779/, 'incr waka.FOO2');
like(http_get('/incr?dict=waka&key=FOO3&by=3333&def=5'), qr/3338/,
	'incr waka.FOO3');

like(http_get('/has?dict=foo&key=FOO'), qr/true/, 'has foo.FOO');
like(http_get('/has?dict=foo&key=NOT_EXISTING'), qr/false/,
	'failed has foo.NOT_EXISTING');
like(http_get('/has?dict=waka&key=FOO'), qr/true/, 'has waka.FOO');

$t->reload();

like(http_get('/keys?dict=foo'), qr/FOO\,FOO2\,FOO3/, 'foo keys');
like(http_get('/keys?dict=foo&max=2'), qr/FOO\,FOO3/, 'foo keys max 2');
like(http_get('/size?dict=foo'), qr/size: 3/, 'no of items in foo');
like(http_get('/get?dict=foo&key=FOO2'), qr/yyy/, 'get foo.FOO2');
like(http_get('/get?dict=bar&key=FOO'), qr/zzz/, 'get bar.FOO');
like(http_get('/get?dict=foo&key=FOO'), qr/xxx/, 'get foo.FOO');
like(http_get('/get?dict=waka&key=FOO'), qr/47/, 'get waka.FOO');
like(http_get('/delete?dict=foo&key=FOO'), qr/true/, 'delete foo.FOO');
like(http_get('/get?dict=foo&key=FOO'), qr/undefined/, 'get foo.FOO');
like(http_get('/get?dict=foo&key=FOO3'), qr/empty/, 'get foo.FOO3');
like(http_get('/replace?dict=foo&key=FOO2&value=aaa'), qr/true/,
	'replace foo.FOO2');
like(http_get('/replace?dict=foo&key=NOT_EXISTING&value=aaa'), qr/false/,
	'failed replace foo.NOT_EXISTING');

select undef, undef, undef, 2.1;

like(http_get('/get?dict=foo&key=FOO'), qr/undefined/, 'get expired foo.FOO');
like(http_get('/pop?dict=foo&key=FOO'), qr/undefined/, 'pop expired foo.FOO');

TODO: {
local $TODO = 'not yet' unless has_version('0.8.1');

like(http_get('/keys?dict=foo'), qr/empty/, 'foo keys after expire');
like(http_get('/keys?dict=bar'), qr/FOO\,FOO2/, 'bar keys after a delay');
like(http_get('/size?dict=foo'), qr/size: 0/,
	'no of items in foo after expire');
like(http_get('/items?dict=bar'), qr/FOO,zzz|FOO2,aaa/,
	'bar items');
like(http_get('/items?dict=waka'), qr/FOO,47|FOO2,7779|FOO3,3338/,
	'waka items');

}

TODO: {
local $TODO = 'not yet' unless has_version('0.8.5');

http_get('/clear?dict=waka');
like(http_get('/set?dict=waka&key=BAR&value=1&timeout=1'), qr/true/,
	'set waka.BAR');
like(http_get('/add?dict=waka&key=BAR2&value=1&timeout=1'), qr/true/,
	'add waka.BAR2');
like(http_get('/incr?dict=waka&key=BAR3&by=42&timeout=1'), qr/42/,
	'incr waka.BAR3');
like(http_get('/set?dict=waka&key=FOO&value=42&timeout=1000'), qr/true/,
	'set waka.FOO');
like(http_get('/add?dict=waka&key=FOO2&value=42&timeout=1000'), qr/true/,
	'add waka.FOO2');
like(http_get('/incr?dict=waka&key=FOO3&by=42&timeout=1000'), qr/42/,
	'incr waka.FOO3');

like(http_get('/keys?dict=waka'), qr/FOO\,FOO2\,FOO3/, 'waka keys');

}

like(http_get('/pop?dict=bar&key=FOO'), qr/zzz/, 'pop bar.FOO');
like(http_get('/pop?dict=bar&key=FOO'), qr/undefined/, 'pop deleted bar.FOO');
http_get('/set?dict=foo&key=BAR&value=xxx');
like(http_get('/clear?dict=foo'), qr/undefined/, 'clear foo');

like(http_get('/size?dict=foo'), qr/size: 0/, 'no of items in foo after clear');
like(http_get('/set_clear'), qr/size: 0/,
	'no of items in no_timeout after clear');

TODO: {
local $TODO = 'not yet' unless has_version('0.8.8');

like(http_get('/overflow'), qr/SharedMemoryError/, 'overflow exception');

}

my $evict_resp = http_get('/evict_stress');
like($evict_resp, qr/failed:0/, 'evict stress: all writes succeeded');
like($evict_resp, qr/last:exists/, 'evict stress: last write exists');
like($evict_resp, qr/first:missing/, 'evict stress: first write evicted');
unlike($t->read_file('error.log'), qr/no memory in js shared zone "foo"/,
	'evict stress: no shared zone foo errors in error log');

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
