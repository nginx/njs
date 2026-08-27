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
and nginx invoked it. You cannot register an event listener from JS or
schedule work outside a directive-driven entry. Async work started
*inside* a handler is tracked by nginx and may outlive the initial
function call, but not the request. The single near-exception is
`js_periodic`, whose trigger is still nginx's timer — nothing
self-starts from JS.

Each directive below defines when the handler runs, what context object
is exposed, and how it terminates.

**HTTP (`ngx_http_js_module`)**

| Directive | When | Context | Termination |
|---|---|---|---|
| `js_content module.fn` | content phase, replaces upstream | `r` | `r.return()` or `r.send()` + `r.finish()` |
| `js_access module.fn` | access phase | `r` | `r.return(403\|...)` to deny, `r.decline()` to abstain, plain return to allow |
| `js_header_filter module.fn` | response header filter | `r` (mutate `headersOut`) | synchronous return |
| `js_body_filter module.fn [buffer_type=string\|buffer]` | response body filter | `r`, plus `(data, flags)` | `r.sendBuffer(out, flags)` |
| `js_set $var module.fn [nocache]` | variable evaluation | `r` | return value |
| `js_periodic module.fn interval=...` | timer, no client request | periodic session | implicit return |

**Stream (`ngx_stream_js_module`)**

| Directive | When | Context | Termination |
|---|---|---|---|
| `js_preread module.fn` | preread phase, before upstream connects | `s` (subscribe with `s.on()`) | `s.allow()` / `s.deny()` / `s.decline()` / `s.done()` |
| `js_filter module.fn` | stream filter chain, both directions | `s` (subscribe with `s.on()`) | callbacks emit with `s.send()`; `s.done()` / `allow` / `deny` / `decline` throw once filtering started |
| `js_access module.fn` | access phase | `s` | `s.allow()` / `s.deny()` / `s.decline()` |
| `js_set $var module.fn` | variable evaluation | `s` | return value |
| `js_periodic module.fn interval=...` | timer, no client session | periodic session | implicit return |

Notes:

- **Async support is not uniform.** What is rejected is a *pending nginx
  event* left behind, not the `async` keyword: any handler may be `async`
  as long as everything it awaits is already settled.
  - May leave pending events: HTTP `js_content`, `js_access`,
    `js_periodic`; stream `js_access`, `js_preread`, `js_periodic`.
  - May not: HTTP `js_header_filter`, `js_body_filter`, `js_set`, and
    stream `js_set` — they log `"async operation inside ... handler"` and
    fail. `js_filter` must return after registering its `s.on()`
    callbacks; those callbacks may be async.
- **`js_body_filter`** is invoked once per response chunk; the last call
  has `flags.last === true`. Pick the chunk shape with
  `buffer_type=string|buffer`.
- **`js_header_filter` / `js_body_filter`** see the *response*; they
  cannot initiate an asynchronous request-body read. They may use
  `r.requestText` / `r.readRequest*()` if an earlier phase has already
  read the body.
- **`js_periodic`** runs without a client request or session: HTTP puts
  it in a dedicated `location @name { }`, stream in `server { }`. Its
  first argument is a
  [periodic session](https://nginx.org/en/docs/njs/reference.html#periodic_session),
  which exposes only `variables` / `rawVariables`. Runs on worker 0
  unless pinned with `worker_affinity`.

## Runtime model (common to both engines)

- **Nginx drives the engine, not the JS.** Every execution begins at a
  directive-bound entry point (see
  [Integration points](#integration-points-where-js-runs)). Registered
  nginx events may keep its context active after the handler returns,
  but no work can outlive the request or session. There is no top-level
  long-lived script.
- **Module bodies follow the context, not the worker.** `js_import` only
  *compiles* at configuration time; imported module bodies run when a
  context is created.
  - njs engine: `ngx_njs_clone()` clones the configuration-time VM and
    calls `njs_vm_start()` per request, so module bodies re-run and
    module state resets every time.
  - QuickJS: `ngx_qjs_clone()` evaluates the bytecode only when it builds
    a context. One popped from the `js_context_reuse` pool (128 by
    default) is neither re-evaluated nor reset, so it still carries what
    the previous request left in module scope —
    `nginx/t/js_context_reuse_redirect.t` asserts a module-level
    `let visits = 0` reaching 4 over four requests.
- **Never keep state in module scope.** It is neither a per-worker cache
  nor a place for per-request data: it is wiped on the njs engine and
  inherited from an unrelated request on QuickJS. Use locals or the `r` /
  `s` object per request, and
  [`ngx.shared`](https://nginx.org/en/docs/njs/reference.html#ngx_shared)
  for anything that must outlive one.
- **Module initialization is not free.** It is paid per new context:
  every request on the njs engine, and on a reuse-pool miss under
  QuickJS. Keep module scope to declarations.
- **Worker isolation.** nginx is multi-process and workers do not share a
  JavaScript heap, so `ngx.shared` — backed by an nginx shared memory
  zone — is the only njs-provided way to coordinate between them.
- **Event loop.** Async work is driven by nginx's event loop;
  `r.subrequest()`, `ngx.fetch()`, and `await` integrate with it.
  `setTimeout` / `clearTimeout` are available in both the CLI and
  inside nginx handlers (HTTP, stream, periodic).
- **Top-level `await`** — QuickJS engine only. The njs engine requires
  `await` to appear inside an `async` function and reports
  `await is only valid in async functions` otherwise.

## NGINX bindings (common API surface)

The full surface is documented in the
[Reference](https://nginx.org/en/docs/njs/reference.html). The
TypeScript declaration files under [`ts/`](../../ts/) are the
authoritative per-symbol description and apply to both engines.
Highlights:

- **`r` (HTTP request).** Inside `js_content` / `js_access` /
  `js_header_filter` / `js_body_filter` / `js_set` handlers.
  - Body:
    - `r.requestText`, `r.requestBuffer`: synchronous accessors that
      require the body to have already been read.
    - `js_access` and `js_content` may initiate a body read with
      `await r.readRequestText()`, `await r.readRequestArrayBuffer()`,
      `await r.readRequestJSON()`,
      `await r.readRequestForm()` (parses form/multipart). The body
      is read once and cached; subsequent reads, including from response
      filters, resolve from the cache.
  - Reply: `r.return(status, [body])`, `r.send(chunk)`, `r.finish()`,
    `r.error(msg)`, `r.warn(msg)`, `r.log(msg)`.
  - Subrequest: `await r.subrequest(uri[, options])`.
  - Headers/vars: `r.headersIn`, `r.headersOut`, `r.variables`,
    `r.rawHeadersIn/Out`.
  - Internal redirect: `r.internalRedirect(uri)`.
- **`s` (Stream session).** Inside `js_access` / `js_preread` /
  `js_filter` / `js_set` for the stream module.
  - I/O: `s.send(data[, options])`, `s.on(event, cb)`, `s.off(event)`.
  - Verdict: `s.allow()` / `s.deny()` / `s.decline()` / `s.done()`.
- **`ngx.fetch(url[, options])`** — async HTTP client (request body,
  headers, timeouts, TLS). Always set explicit timeouts.
- **`ngx.shared`** — cross-worker shared memory dictionary configured via
  `js_shared_dict_zone`. Values are strings or numbers only.
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

`js_periodic` runs with no client request or session behind it. Under
`stream { }` it goes in `server { }`; under `http { }` any location will
do, but a named one keeps it unroutable:

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
- Use `await` only in handlers that may leave pending events; see
  [Async support](#integration-points-where-js-runs).
- Keep module scope to declarations; its body re-runs per context.

**Don't**

- Don't keep state in module scope at all — a module-level `Map` is not
  a per-worker cache. It is wiped on the njs engine and, under QuickJS
  `js_context_reuse`, may instead surface in an unrelated request.
- Don't assume workers share a JavaScript heap — they don't. Use
  `ngx.shared`.
- Don't use request handlers for detached background work. Nginx tracks
  timers, subrequests, and `ngx.fetch()` even when their promises are not
  awaited, so they may delay request completion. Request teardown can
  still cancel them; use `js_periodic` for recurring work.
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
