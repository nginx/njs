import fs from 'fs';
import qs from 'querystring';
import crypto from 'crypto';

function http_module(r: NginxHTTPRequest) {
    var bs: NjsByteString;
    var s: string;
    var vod: void;

    // builtin string vs NjsByteString

    s = 'ordinary string';
    bs = String.bytesFrom('000000', 'hex');
    var bs2: NjsByteString | null = s.toBytes();
    bs = s.toUTF8();
    bs.fromBytes(undefined, undefined);

    s = bs + '';

    // r.uri

    if (r.uri == '/') {
    }

    // r.args

    bs = r.args.x;
    bs = r.args[1];
    var s2: string | null = r.args.x.fromUTF8();
    s = r.args.x + '';

    // r.headersIn

    r.headersIn['Accept']?.fromBytes() == 'dddd';

    // r.headersOut

    r.headersOut['Content-Type'] = 'text/plain';
    // Warning: r.headersOut['Content-Type'] = ['a', 'b'];
    r.headersOut['Connection'] = undefined;

    delete r.headersOut['Bar'];

    r.headersOut['Set-Cookie'] = ['aaa', 'bbb'];
    r.headersOut['Foo'] = ['aaa', 'bbb'];

    r.subrequest('/uri', reply => r.return(200, reply.headersOut["Location"] ?? ''));

    // r.log

    r.log(bs);
    r.log(Buffer.from("abc"));
    r.log(r.headersOut['Connection'] ?? '');

    // r.variables

    r.variables.a == 'a';
    r.variables.cookie_a = 'b';

    // r.subrequest
    r.subrequest('/p/sub1').then(reply => r.return(reply.status));
    r.subrequest('/p/sub2', {method:'POST'}).then(reply => r.return(reply.status));
    vod = r.subrequest('/p/sub3', reply => r.return(reply.status));
    vod = r.subrequest('/p/sub4', {method:'POST'}, reply => r.return(reply.status));
    vod = r.subrequest(Buffer.from('/p/sub5'), {detached:true});
    // Warning: vod = r.subrequest('/p/sub9', {detached:true}, reply => r.return(reply.status));
    r.subrequest('/p/sub6', 'a=1&b=2').then(reply => r.return(reply.status,
                                        JSON.stringify(JSON.parse(reply.responseBody ?? ''))));
}

function fs_module() {
    var s:string;

    s = fs.readFileSync('/path', 'utf8');
    s = fs.readFileSync(Buffer.from('/path'), {encoding:'hex'});

    fs.writeFileSync('/path', Buffer.from('abc'));
}

function qs_module(str: NjsByteString) {
    var o;
    var s:string;

    o = qs.parse(str);
    s = qs.stringify(o);
}

function crypto_module(str: NjsByteString) {
    var h;
    var b:Buffer;
    var s:string;

    h = crypto.createHash("sha1");
    h = h.update(str).update(Buffer.from([0]));
    b = h.digest();

    s = crypto.createHash("sha256").digest("hex");
}

function buffer(b: Buffer) {
    var s:string;

    s = b.toString() +  b.toString('utf8') + b.toString('hex');
    b = Buffer.concat([b, Buffer.from([0,1,2])]);

    b.equals(b);
}

function timers() {
    var handle:TimerHandle;

    handle = setTimeout(() => {});
    handle = setTimeout(() => {}, 100);
    handle = setTimeout((a:string, b:number) => {}, 100, 'foo', 42);

    handle = setImmediate(() => {});
    handle = setImmediate((a:string, b:number) => {}, 'foo', 42);

    clearTimeout(handle);
    // Warning: clearTimeout(123);
}

function njs_object() {
    njs.dump('asdf');
    njs.version != process.argv[1];
}

function ngx_object() {
    ngx.log(ngx.INFO, 'asdf');
    ngx.log(ngx.WARN, Buffer.from('asdf'));
    ngx.log(ngx.ERR, 'asdf');
}
