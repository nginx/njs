# njs — agent instructions

njs is a JavaScript engine integrated with NGINX. It ships as:

- a standalone CLI (`build/njs`) for testing and scripting,
- two NGINX modules: `ngx_http_js_module` and `ngx_stream_js_module`,
- two interchangeable JS engines selectable per location/server via the
  `js_engine` directive:
  - **njs** — the built-in engine, deprecated since 1.0.0.
  - **QuickJS** — recommended. Set `js_engine qjs;` in nginx.conf.

This file is the index. Detailed instructions live under [`docs/agent/`](docs/agent/).

## Pick your task

| If you are doing... | Read |
|---|---|
| Editing C in `src/`, `external/`, `nginx/` — engine, modules, build system | [docs/agent/engine-dev.md](docs/agent/engine-dev.md) |
| Writing JavaScript that runs in njs (CLI or NGINX), targeting either engine | [docs/agent/js-dev.md](docs/agent/js-dev.md) |
| Writing JavaScript that must run on the deprecated njs engine | [docs/agent/js-dev-njs.md](docs/agent/js-dev-njs.md) |

## 1. Engine and module development (C)

You are extending or fixing the engine, the QuickJS integration, or the
nginx modules.

Quick facts:

- **Build (CLI):** `./configure && make njs` → `build/njs`. Rebuild is fast.
- **Build (NGINX):** configure NGINX with `--add-module=<njs>/nginx` (static)
  or `--add-dynamic-module=<njs>/nginx` (dynamic) in a separate NGINX tree.
- **Dual engine = dual code.** Most external modules ship both an `njs_*.c`
  and a `qjs_*.c` implementation. If you change behavior on one side, change
  it on the other.
- **Tests:** `make unit_test`, `make lib_test`, `make test262`. NGINX
  integration tests under `nginx/t/` run with
  `prove -I <tests-lib> nginx/t/`.
- **Code style:** NGINX conventions — 4 spaces (no tabs), 80-column limit,
  no trailing whitespace, newline after closing brace, `-Werror` build.
- **Commit subjects:** past tense, prefixed
  (`HTTP:`, `Stream:`, `Core:`, `QuickJS:`, `Tests:`, …), ≤67 characters.

Full details, sanitizer builds, VM architecture, and object model:
[docs/agent/engine-dev.md](docs/agent/engine-dev.md).

## 2. Writing JavaScript for njs (CLI or NGINX)

You are writing `.js` modules that run inside `js_content` / `js_filter` /
`js_set` / `js_access` / `js_preread` handlers, or under the standalone CLI.

Orientation:

- **Default to the QuickJS engine** (`js_engine qjs;`). The built-in njs
  engine is deprecated since 1.0.0; write new code for QuickJS.
- **Language baseline.** QuickJS is ES2023; the njs engine is ES5.1 strict
  with a curated ES6+ subset. See the
  [compatibility page](https://nginx.org/en/docs/njs/compatibility.html).
- **Nginx drives the engine, not the JS.** Code only runs from
  directive-bound entry points (HTTP: `js_content`, `js_access`,
  `js_header_filter`, `js_body_filter`, `js_set`, `js_periodic`).
- **Quick test (CLI):** `./build/njs -c '<code>'` or `./build/njs file.js`.
- **Test inside NGINX:** `prove -I <tests-lib> nginx/t/<your>.t` with
  `TEST_NGINX_GLOBALS_HTTP='js_engine qjs;'` (and the same for `_STREAM`).

Everything else — full integration-point semantics, `nginx.conf` wiring
(`js_shared_dict_zone`, `resolver` + `js_fetch_*`, `js_import` /
`js_path` / `js_engine`), bindings (`r`, `s`, `ngx.fetch`, `ngx.shared`,
`crypto`, …), engine-only features, do/don't recipes:
[docs/agent/js-dev.md](docs/agent/js-dev.md). For code that must run on
the deprecated njs engine, also see
[docs/agent/js-dev-njs.md](docs/agent/js-dev-njs.md).

## Resources

- [njs official documentation](https://nginx.org/en/docs/njs/)
- [Reference (API surface)](https://nginx.org/en/docs/njs/reference.html)
- [Compatibility](https://nginx.org/en/docs/njs/compatibility.html)
- [Engine selection](https://nginx.org/en/docs/njs/engine.html)
- [njs-examples repo](https://github.com/nginx/njs-examples/)
