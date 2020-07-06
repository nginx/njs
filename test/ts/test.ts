/// <reference path="../../build/ts/ngx_http_js_module.d.ts" />

function handler(r: NginxHTTPRequest) {
    var bs: NjsByteString;
    var s: string;

    // builtin string vs NjsByteString

    s = 'ordinary string';
    bs = String.bytesFrom('000000', 'hex');
    bs = s.toBytes();
    bs = s.toUTF8();
    bs.fromBytes(null, null);

    s = bs + '';

    // r.uri

    if (r.uri == '/') {
    }

    // r.args

    bs = r.args.x;
    bs = r.args[1];
    s = r.args.x.fromUTF8();
    s = r.args.x + '';

    // r.headersIn

    r.headersIn['Accept'].fromBytes() == 'dddd';

    // r.headersOut

    r.headersOut['Content-Type'] = 'text/plain';
    // Warning: r.headersOut['Content-Type'] = ['a', 'b'];
    r.headersOut['Connection'] = undefined;
    r.headersOut['Connection'] = null;

    r.headersOut['Set-Cookie'] = ['aaa', 'bbb'];
    r.headersOut['Foo'] = ['aaa', 'bbb'];

    r.subrequest('/uri', reply => r.return(200, reply.headersOut["Location"]));

    // r.log

    r.log(bs);
    r.log(r.headersOut['Connection']);

    // r.variables

    r.variables.a == 'a';
    r.variables.cookie_a = 'b';

    // r.subrequest
    r.subrequest('/p/sub1').then(reply => r.return(reply.status));
    r.subrequest('/p/sub2', reply => r.return(reply.status));
    r.subrequest('/p/sub3', {detached:true});
    r.subrequest('/p/sub4', 'a=1&b=2').then(reply => r.return(reply.status,
                                        JSON.stringify(JSON.parse(reply.responseBody))));

    // builtin objects

    njs.dump('asdf');
    njs.version != process.argv[1];
}
