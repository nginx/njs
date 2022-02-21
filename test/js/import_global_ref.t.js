/*---
includes: []
flags: []
paths: [test/js/module/]
---*/

import http from 'http.js';
import jwt  from 'jwt.js';

globalThis.http = http;
globalThis.jwt = jwt;

assert.sameValue(http.check(), "JWT-OK");
