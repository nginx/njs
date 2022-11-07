import fs from 'fs';
import qs from 'querystring';
import cr from 'crypto';

async function http_module(r: NginxHTTPRequest) {
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

    // r.log

    r.log(bs);
    r.log(Buffer.from("abc"));
    r.log(r.headersOut['Connection'] ?? '');

    // r.variables

    r.variables.a == 'a';
    r.variables.cookie_a = 'b';

    // r.rawVariables
    r.rawVariables.a?.equals(Buffer.from([1]));

    // r.subrequest
    r.subrequest('/uri', reply => r.return(200, reply.headersOut["Location"] ?? ''));
    r.subrequest('/p/sub1').then(reply => r.return(reply.status));
    r.subrequest('/p/sub2', {method:'POST'}).then(reply => r.return(reply.status));
    vod = r.subrequest('/p/sub3', reply => r.return(reply.status));
    vod = r.subrequest('/p/sub4', {method:'POST'}, reply => r.return(reply.status));
    vod = r.subrequest(Buffer.from('/p/sub5'), {detached:true});
    // Warning: vod = r.subrequest('/p/sub9', {detached:true}, reply => r.return(reply.status));
    r.subrequest('/p/sub6', 'a=1&b=2').then(reply => r.return(reply.status,
                                        JSON.stringify(JSON.parse(reply.responseBody ?? ''))));
    let body = await r.subrequest('/p/sub7');

    // r.requestText
    r.requestText == 'a';
    r.requestText?.startsWith('a');

    // r.requestBuffer
    r.requestBuffer?.equals(Buffer.from([1]));

    // r.responseText
    r.responseText == 'a';
    r.responseText?.startsWith('a');

    // r.responseBuffer
    r.responseBuffer?.equals(Buffer.from([1]));

    ngx.fetch('http://nginx.org/', {method:'POST', headers:{Foo:'bar'}})
    .then(reply => {
        if (reply.headers.get('foo')) {
            throw 'oops'
        };

        return reply.text()
    })
    .then(body => r.return(200, body))
    .catch(e => r.return(501, e.message))

    let response = await ngx.fetch('http://nginx.org/');

    // js_body_filter
    r.sendBuffer(Buffer.from("xxx"), {last:true});
    r.sendBuffer("xxx", {flush: true});
    r.done();
}

async function fs_module() {
    var s:string;

    s = fs.readFileSync('/path', 'utf8');
    s = fs.readFileSync(Buffer.from('/path'), {encoding:'hex'});

    fs.writeFileSync('/path', Buffer.from('abc'));

    let fh = await fs.promises.open('/path', 'r+');

    let bw = await fh.write(Buffer.from('abc'), 0, 1, 3);
    let bytes = bw.bytesWritten;
    bw = await fh.write(Buffer.from('abc'), 2, 2, null);
    let stat = fh.stat();

    let buffer = Buffer.alloc(16);
    let br = await fh.read(buffer, 0, 16, null);
    bytes = br.bytesRead;

    await fh.close();

    let fd = fs.openSync('/path', 'r+');
    let stat2 = fs.fstatSync(fd);

    fs.readSync(fd, buffer, 0, 16, 4);
    buffer[1] += 2;
    fs.writeSync(fd, buffer, 0, 16, 4);
    fs.closeSync(fd);

    fs.mkdirSync('a/b/c', {recursive: true});
    await fs.promises.mkdir('d/e/f', {recursive: false});

    fs.rmdirSync('a/b/c', {recursive: true});
    await fs.promises.rmdir('d/e/f', {recursive: false});
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

    h = cr.createHash("sha1");
    h = h.update(str).update(Buffer.from([0]));
    b = h.digest();

    s = cr.createHash("sha256").digest("hex");
}

async function crypto_object(keyData: ArrayBuffer, data: ArrayBuffer) {
    let iv = crypto.getRandomValues(new Uint8Array(16));

    let ekey = await crypto.subtle.importKey("pkcs8", keyData,
                                             {name: 'RSA-OAEP', hash: "SHA-256"},
                                             false, ['decrypt']);

    let skey = await crypto.subtle.importKey("raw", keyData, 'AES-CBC',
                                             false, ['encrypt']);

    data = await crypto.subtle.decrypt({name: 'RSA-OAEP'}, ekey, data);
    data = await crypto.subtle.encrypt({name: 'AES-CBC', iv:iv}, skey, data);

    let sig = await crypto.subtle.sign({name: 'RSA-PSS', saltLength:32}, skey, data);

    let r:boolean;
    r = await crypto.subtle.verify({name: 'RSA-PSS', saltLength:32}, skey, sig, data);
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

function global_functions() {
    const encodedData = btoa("text to encode");
    const decodedData = atob(encodedData);
}

function njs_object() {
    njs.dump('asdf');
    njs.version != process.argv[1];
    typeof njs.version_number == 'number';
    njs.on('exit', ()=> {});
}

function ngx_object() {
    ngx.log(ngx.INFO, 'asdf');
    ngx.log(ngx.WARN, Buffer.from('asdf'));
    ngx.log(ngx.ERR, 'asdf');
}
