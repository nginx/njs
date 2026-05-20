# Writing JavaScript for njs (both engines)

This document is for authors of `.js` modules that run inside `js_content`
/ `js_filter` / `js_set` / `js_access` / `js_preread` handlers, or under
the standalone `build/njs` CLI. It covers the common runtime, the nginx
API surface, and the points where the two engines differ.

For per-task orientation see the top-level [AGENTS.md](../../AGENTS.md).
For code that must run on the deprecated **njs** engine, also read
[docs/agent/js-dev-njs.md](js-dev-njs.md).

## Pick an engine

njs ships two interchangeable JavaScript engines:

| Engine | Language baseline | Status | When to pick |
|---|---|---|---|
| **QuickJS** | ES2023 | **Recommended** | All new code. Modern JS works as-is. |
| **njs** | ES5.1 strict + curated ES6+ | Deprecated since 1.0.0 | Only when maintaining existing njs-engine code that you cannot port yet. |

Select per-context with `js_engine` in nginx.conf:

```nginx
http {
    js_engine qjs;            # http-wide default
    js_import http.js;

    server {
        # js_engine inherits, can be overridden per server or location
    }
}
```

The same `js_engine` directive exists for `stream { }`. From the CLI use
`-n njs` or `-n QuickJS` (the latter requires a build with QuickJS
linked in).

## Engine differences at a glance

| Feature | njs engine | QuickJS engine |
|---|---|---|
| `class` | ✗ | ✓ |
| Generators (`function*`, `yield`) | ✗ | ✓ |
| `async` / `await` | ✓ | ✓ |
| Spread in calls / array literals (`f(...a)`, `[...a]`) | ✗ | ✓ |
| Rest parameter (`function f(...rest)`) | ✓ (no destructuring) | ✓ |
| Destructuring (`{a,b} = x`, `[a,b] = x`) | ✗ | ✓ |
| Optional chaining `?.`, nullish `??`, `??=`/`&&=`/`\|\|=` | ✓ (since 0.9.6) | ✓ |
| `Map`, `Set`, `WeakMap`, `WeakSet` | ✗ | ✓ |
| `BigInt`, `Proxy`, `Reflect` | ✗ | ✓ |
| Template literals | ✓ | ✓ |
| `Promise`, full | ✓ | ✓ |
| `Symbol` subset (`for`, `keyFor`) | ✓ | ✓ |
| Module imports (`import`/`export`) | ✓ default only | ✓ |
| Non-default imports (`import {x}`, `import *`, `import "s"`) | ✗ | ✓ |
| `require()` | ✓ | ✗ (use `import`) |
| `njs.dump()`, `console.dump()` | ✓ | ✗ |
| `js_preload_object` | ✓ | ✗ |
| Native modules (`js_load_*_native_module`) | ✗ | ✓ |

For the full ECMAScript compatibility list of the njs engine, see the
[compatibility page](https://nginx.org/en/docs/njs/compatibility.html).

## Integration points (where JS runs)

**JS code in njs does not run on its own.** There is no main loop, no
background thread, no startup script. Every JS function executes because
some `nginx.conf` directive bound it to a phase of request processing
and nginx invoked it. You cannot register an event listener from JS,
schedule work outside a directive-driven entry, or keep code running
after the handler returns. The single near-exception is `js_periodic`,
whose trigger is still nginx's timer — nothing self-starts from JS.

Each directive below defines when the handler runs, what context object
is exposed, and how it terminates.

**HTTP (`ngx_http_js_module`)**

| Directive | When | Context | Termination |
|---|---|---|---|
| `js_content module.fn` | content phase, replaces upstream | `r` | `r.return()` or `r.send()` + `r.finish()` |
| `js_access module.fn` | access phase | `r` | `r.return(403\|...)` to deny; otherwise fall through |
| `js_header_filter module.fn` | response header filter | `r` (mutate `headersOut`) | synchronous return |
| `js_body_filter module.fn [buffer_type=string\|buffer]` | response body filter | `r`, plus `(data, flags)` | `r.sendBuffer(out, flags)` |
| `js_set $var module.fn [nocache]` | variable evaluation | `r` | return value (synchronous) |
| `js_periodic module.fn interval=...` | timer, no request | none | implicit return |

**Stream (`ngx_stream_js_module`)**

| Directive | When | Context | Termination |
|---|---|---|---|
| `js_preread module.fn` | before upstream connects | `s` | `s.allow()` / `s.deny()` / `s.done()` |
| `js_filter module.fn` | data filter, both directions | `s` (subscribe with `s.on()`) | `s.done()` |
| `js_access module.fn` | access | `s` | `s.allow()` / `s.deny()` |
| `js_set $var module.fn` | variable evaluation | `s` | return value (synchronous) |
| `js_periodic module.fn interval=...` | timer, no session | none | implicit return |

Notes:

- **Async support is not uniform.** `js_content` and `js_access` accept
  fully async handlers (returning a `Promise` or using `await`). The
  remaining HTTP handlers (`js_header_filter`, `js_body_filter`,
  `js_set`) reject async work with `"async operation inside ... handler"`
  if `await` hits the event loop. `js_set` may still return an
  already-resolved `Promise`.
- **`js_body_filter`** is invoked once per response chunk; the last call
  has `flags.last === true`. Pick the chunk shape with
  `buffer_type=string|buffer`.
- **`js_header_filter` / `js_body_filter`** see the *response*; they
  cannot read the request body via `r.requestText` / `r.readRequest*()`.
- **`js_periodic`** lives in a dedicated `location @name { }` block and
  runs without a client request. Pin to specific workers with
  `worker_affinity`.

## Runtime model (common to both engines)

- **Nginx drives the engine, not the JS.** Every execution begins at a
  directive-bound entry point (see
  [Integration points](#integration-points-where-js-runs)) and ends when
  that handler resolves. There is no top-level long-lived script; work
  that escapes the handler's lifetime is on its own.
- **Per-request isolation.** For each incoming HTTP request or stream
  session, the JS module creates a fresh VM. State you put in module
  scope is visible to subsequent requests on the same worker but cannot
  be used to carry per-request data — it leaks across requests.
- **Worker isolation.** nginx is multi-process. Module scope is not
  shared across workers. To share state across workers, use
  [`ngx.shared`](https://nginx.org/en/docs/njs/reference.html#ngx_shared)
  (shared dictionary). For request-scoped per-worker state, use a `Map`
  / object keyed by some request identifier, but mind eviction.
- **Event loop.** Async work is driven by nginx's event loop;
  `r.subrequest()`, `ngx.fetch()`, and `await` integrate with it.
  `setTimeout` / `clearTimeout` are available in both the CLI and
  inside nginx handlers (HTTP, stream, periodic).
- **Top-level `await`** — QuickJS engine only. The njs engine requires
  `await` to appear inside an `async` function and reports
  `await is only valid in async functions` otherwise.
- **Module imports load once per worker.** Don't perform expensive setup
  in module scope unless it's truly initialization.

## NGINX bindings (common API surface)

The full surface is documented in the
[Reference](https://nginx.org/en/docs/njs/reference.html). The
TypeScript declaration files under [`ts/`](../../ts/) are the
authoritative per-symbol description and apply to both engines.
Highlights:

- **`r` (HTTP request).** Inside `js_content` / `js_filter` /
  `js_access` / `js_set` handlers.
  - Body:
    - `js_content` only: `r.requestText`, `r.requestBuffer` (synchronous
      accessors; require the body to be in memory — set
      `client_max_body_size` and `client_body_buffer_size` accordingly).
    - `js_access` and `js_content`:
      `await r.readRequestText()`, `await r.readRequestArrayBuffer()`,
      `await r.readRequestJSON()`,
      `await r.readRequestForm()` (parses form/multipart). The body
      is read once and cached; subsequent reads resolve from cache.
  - Reply: `r.return(status, [body])`, `r.send(chunk)`, `r.finish()`,
    `r.error(msg)`, `r.warn(msg)`, `r.log(msg)`.
  - Subrequest: `await r.subrequest(uri[, options])`.
  - Headers/vars: `r.headersIn`, `r.headersOut`, `r.variables`,
    `r.rawHeadersIn/Out`.
  - Internal redirect: `r.internalRedirect(uri)`.
- **`s` (Stream session).** Inside `js_preread` / `js_filter` /
  `js_access` for the stream module.
  - I/O: `s.send(data[, options])`, `s.on(event, cb)`,
    `s.allow()` / `s.deny()`, `s.done()`.
- **`ngx.fetch(url[, options])`** — async HTTP client (request body,
  headers, timeouts, TLS). Always set explicit timeouts.
- **`ngx.shared`** — process-wide shared dictionary configured via
  `js_shared_dict_zone`.
- **Built-in modules.** Import with `import`:
  - `crypto`, `buffer`, `fs`, `querystring`, `xml`, `zlib`.
  - `WebCrypto` is available at `crypto.subtle` (since 0.8.10).
  - `TextEncoder` / `TextDecoder` globals (since 0.8.10).
- **`process`** — argv/env (since 0.8.8).

## NGINX configuration (nginx.conf)

What you need to wire up so the JS bindings work. Defaults below match
the current code; see the
[reference](https://nginx.org/en/docs/njs/reference.html) for full
grammar and scope.

### Loading modules

```nginx
http {
    js_path     "/etc/nginx/njs/";        # module search path
    js_import   utils.js;                 # imports default export as `utils`
    js_import   foo from helpers/foo.js;  # explicit local name
    js_engine   qjs;                      # recommended (default: njs)
}
```

`js_import` is in `http` / `stream` scope. `js_engine` is in
`http` / `server` / `location` (HTTP) and `stream` / `server` (Stream).

### Variables

```nginx
js_var $cache_key '';                # writable variable, default empty
js_set $token     auth.gen_token;    # bind a $var to a JS function
```

`js_set` evaluates lazily on first reference and caches the result for
the lifetime of the request; append `nocache` to recompute on every
reference.

### `ngx.shared` — cross-worker dictionary

```nginx
http {
    # zone=name:size [type=string|number] [timeout=t] [evict]
    js_shared_dict_zone zone=cache:1m     timeout=60s evict;
    js_shared_dict_zone zone=counters:32k type=number;
}
```

```js
ngx.shared.cache.set('k', 'v');        // string zone
ngx.shared.counters.incr('hits', 1);   // number zone
```

`type=string` is the default. `evict` lets the LRU drop entries when the
zone is full; without it, `set()` fails once the zone is exhausted.
`timeout=` sets the default TTL (per-key TTL can override it).

### `ngx.fetch()` — outgoing HTTP client

A `resolver` is **required** when fetching by hostname. The `js_fetch_*`
directives sit in `http` / `server` / `location` (and the matching
stream scopes). Defaults shown:

```nginx
http {
    resolver         127.0.0.1 ipv6=off;
    resolver_timeout 5s;

    js_fetch_timeout                  60s;  # total request timeout
    js_fetch_buffer_size              16k;  # per-connection read buffer
    js_fetch_max_response_buffer_size 1m;   # response body cap

    # HTTPS
    js_fetch_trusted_certificate /etc/ssl/ca.pem;
    js_fetch_ciphers             HIGH:!aNull:!MD5;
    js_fetch_protocols           TLSv1.2 TLSv1.3;
    js_fetch_verify              on;        # default on
    js_fetch_verify_depth        1;

    # Connection pool (default: disabled)
    js_fetch_keepalive          32;         # max idle connections / worker
    js_fetch_keepalive_requests 1000;
    js_fetch_keepalive_time     1h;
    js_fetch_keepalive_timeout  60s;

    # Forward proxy for outgoing fetches
    js_fetch_proxy http://user:pass@proxy:3128;
}
```

The same directives exist under `stream { }` for `ngx.fetch()` from
stream handlers.

### `js_periodic` — timer jobs

`js_periodic` lives in its own `location @name { }` block (no client
request reaches it):

```nginx
location @cron {
    js_periodic tasks.tick    interval=10s;
    js_periodic tasks.cleanup interval=1m jitter=5s worker_affinity=all;
}
```

`worker_affinity` accepts `all` (every worker) or a bitmask
(e.g. `0101` runs on workers 0 and 2). `jitter` randomizes start to
spread load across workers.

Engine-specific directives (`js_preload_object`, `js_load_*_native_module`)
are covered in [Engine-specific bindings](#engine-specific-bindings) below.

## Engine-specific bindings

- **`js_preload_object`** — preload an immutable shared object at config
  load. **njs engine only.** See
  [Preloaded objects](https://nginx.org/en/docs/njs/preload_objects.html).
- **Native modules** (`js_load_http_native_module` /
  `js_load_stream_native_module`) — load a shared library as a JS
  module. **QuickJS only.** See
  [Native modules](https://nginx.org/en/docs/njs/native_modules.html).
- **`njs.dump()`, `console.dump()`** — pretty-print with hidden
  properties. njs-engine only.

## How to test

### Standalone

```bash
./build/njs -c 'console.log(typeof Map)'   # under default njs engine
./build/njs -n QuickJS script.js           # under QuickJS (if linked in)
./build/njs -m module.mjs                  # load as ES module
```

### Inside NGINX

```bash
TMPDIR=$(mktemp -d) \
TEST_NGINX_BINARY=<NGINX_BIN> \
    prove -I <TESTS_LIB> nginx/t/<your>.t
```

Run twice, once per engine:

```bash
# njs engine (default)
prove -I <TESTS_LIB> nginx/t/<your>.t

# QuickJS engine
TEST_NGINX_GLOBALS_HTTP='js_engine qjs;' \
    prove -I <TESTS_LIB> nginx/t/<your>.t
# (use TEST_NGINX_GLOBALS_STREAM for stream tests)
```

Examples of well-shaped test files: anything under `nginx/t/js_*.t`.

## Do / Don't

**Do**

- Default to the QuickJS engine for new code.
- Use `import` / `export` (ES modules); never `require()`.
- Use `ngx.shared` for cross-worker state; document the zone's
  `keys`/`value` size in nginx.conf.
- Use `await` in handlers — return a `Promise` (implicit via `async`) or
  call `r.return()` / `r.finish()` to terminate.
- Keep module-scope work to true one-time initialization
  (configuration, schema compilation, etc.).

**Don't**

- Don't keep per-request state in module scope — it leaks across
  requests handled by the same worker.
- Don't assume workers share memory — they don't. Use `ngx.shared`.
- Don't try to outlive the handler. A `Promise` you don't `await`, a
  `setTimeout` you queue after `r.finish()`, an `ngx.fetch()` you fire
  and forget — none of that is guaranteed to complete. Once the
  handler resolves, the request context goes away and pending JS work
  is dropped. If you need recurring work, use `js_periodic`.
- Don't rely on engine-specific extensions in code that should run on
  both engines: `njs.dump()` / `console.dump()`, `js_preload_object`,
  native modules, top-level `await`, non-default imports.

## Resources

- [Reference (full API)](https://nginx.org/en/docs/njs/reference.html)
- [Compatibility (njs engine)](https://nginx.org/en/docs/njs/compatibility.html)
- [Engine selection](https://nginx.org/en/docs/njs/engine.html)
- [Preloaded objects (njs-only)](https://nginx.org/en/docs/njs/preload_objects.html)
- [Native modules (qjs-only)](https://nginx.org/en/docs/njs/native_modules.html)
- [TypeScript type definitions in `ts/`](../../ts/) —
  `ngx_http_js_module.d.ts`, `ngx_stream_js_module.d.ts`, `ngx_core.d.ts`,
  `njs_webapi.d.ts`, `njs_webcrypto.d.ts` (same surface on both engines)
- [njs-examples](https://github.com/nginx/njs-examples/)
