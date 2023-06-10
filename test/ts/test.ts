import fs from 'fs';
import qs from 'querystring';
import cr from 'crypto';
import xml from 'xml';
import zlib from 'zlib';

async function http_module(r: NginxHTTPRequest) {
    var s: string;
    var vod: void;

    // r.uri

    if (r.uri == '/') {
    }

    // r.args

    s = r.args.x;
    s = r.args[1];
    s = r.args.x + '';

    // r.headersIn

    r.headersIn['Accept'] == 'dddd';

    // r.headersOut

    r.headersOut['Content-Type'] = 'text/plain';
    // Warning: r.headersOut['Content-Type'] = ['a', 'b'];
    r.headersOut['Connection'] = undefined;

    delete r.headersOut['Bar'];

    r.headersOut['Set-Cookie'] = ['aaa', 'bbb'];
    r.headersOut['Foo'] = ['aaa', 'bbb'];

    let values: Array<string> = r.rawHeadersIn.filter(v=>v[0].toLowerCase() == 'foo').map(v=>v[1]);

    // r.log

    r.log(s);
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

        let out: Array<string> = reply.headers.getAll("foo");
        let has: boolean = reply.headers.has("foo");

        reply.headers.append("foo", "xxx");
        reply.headers.delete("xxx");
        reply.headers.forEach((name, value) => { /* do something. */ });

        return reply.text()
    })
    .then(body => r.return(200, body))
    .catch(e => r.return(501, e.message))


    let response = await ngx.fetch('http://nginx.org/');
    let response2 = new Response("xxx", {headers: {"Content-Type": "text/plain"}, status: 404});

    let req = new Request("http://nginx.org", {method: "POST", headers: new Headers(["Foo", "bar"])});
    let response3 = await ngx.fetch(req);

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

function qs_module(str: string) {
    var o;
    var s:string;

    o = qs.parse(str);
    s = qs.stringify(o);
}

function xml_module(str: string) {
    let doc;
    let node;
    let children, selectedChildren;

    doc = xml.parse(str);
    node = doc.$root;

    node.$ns;
    children = node.$tags;
    selectedChildren = node.$tags$xxx;

    node?.$tag$xxx?.$tag$yyy?.$attr$zzz;

    let buf:Buffer = xml.exclusiveC14n(node);
    buf = xml.exclusiveC14n(doc, node.$tag$xxx, false);
    buf = xml.exclusiveC14n(node, null, true, "aa bb");

    node.setText("xxx");
    node.removeText();
    node.setText(null);

    node.addChild(node);
    node.removeChildren('xx');

    node.removeAttribute('xx');
    node.removeAllAttributes();
    node.setAttribute('xx', 'yy');
    node.setAttribute('xx', null);
    node.$tags = [node, node];
}

function zlib_module(str: string) {
    zlib.deflateRawSync(str, {level: zlib.constants.Z_BEST_COMPRESSION, memLevel: 9});
    zlib.deflateSync(str, {strategy: zlib.constants.Z_RLE});

    zlib.inflateRawSync(str, {windowBits: 14});
    zlib.inflateSync(str, {chunkSize: 2048});
}

function crypto_module(str: string) {
    var h;
    var b:Buffer;
    var s:string;

    h = cr.createHash("sha1");
    h = h.update(str).update(Buffer.from([0]));
    h = h.copy();
    b = h.digest();

    s = cr.createHash("sha256").digest("hex");
}

async function crypto_object(keyData: ArrayBuffer, data: ArrayBuffer) {
    let iv = crypto.getRandomValues(new Uint8Array(16));

    let ekey = await crypto.subtle.importKey("pkcs8", keyData,
                                             {name: 'RSA-OAEP', hash: "SHA-256"},
                                             false, ['decrypt']);

    let jkey = await crypto.subtle.importKey("jwk", { kty: "RSA" },
                                             {name: 'RSA-OAEP', hash: "SHA-256"},
                                             true, ['decrypt']);

    let skey = await crypto.subtle.importKey("raw", keyData, 'AES-CBC',
                                             false, ['encrypt']);

    data = await crypto.subtle.decrypt({name: 'RSA-OAEP'}, ekey, data);
    data = await crypto.subtle.encrypt({name: 'AES-CBC', iv:iv}, skey, data);

    let sig = await crypto.subtle.sign({name: 'RSA-PSS', saltLength:32}, skey, data);

    let r:boolean;
    r = await crypto.subtle.verify({name: 'RSA-PSS', saltLength:32}, skey, sig, data);

    let jwk = await crypto.subtle.exportKey('jwk', ekey);

    let pair = await crypto.subtle.generateKey({name: "RSASSA-PKCS1-v1_5",
                                                hash: "SHA-512",
                                                modulusLength: 2048,
                                                publicExponent: new Uint8Array([1, 0, 1])},
                                                true, ['sign', 'verify']);

    pair.privateKey.extractable;
    pair.publicKey.algorithm.name;

    let hkey = await crypto.subtle.generateKey({name: "HMAC",
                                                hash: "SHA-384"},
                                                true, ['sign', 'verify']);
    hkey.algorithm.name;

    let akey = await crypto.subtle.generateKey({name: "AES-GCM",
                                                length: 256},
                                                true, ['encrypt', 'decrypt']);

    akey.type;
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

function text_decoder() {
    let b:boolean;
    let s:string;

    const d = new TextDecoder("utf-8", {fatal: true});

    s = d.encoding;
    b = d.fatal;
    b = d.ignoreBOM;

    s += d.decode(new Uint8Array([1, 2, 3]), {stream: true});
    s += d.decode(new Uint8Array([4, 5, 6]), {stream: true});
    s += d.decode();

    s = new TextDecoder().decode(new Uint8Array([206,177,206,178]));
}

function text_encoder() {
    let n:number;
    let s:string;
    let uint8a:Uint8Array;

    const e = new TextEncoder();

    s = e.encoding;
    uint8a = e.encode("text to encode");

    const res = e.encodeInto("text to encode", uint8a);
    n = res.read;
    n = res.written;
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
    ngx.log(ngx.INFO, ngx.conf_prefix);
    ngx.log(ngx.WARN, Buffer.from(ngx.error_log_path));
    ngx.log(ngx.ERR, ngx.version);
}
