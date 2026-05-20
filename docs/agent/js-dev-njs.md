# Writing JavaScript for the deprecated njs engine

This document is for code that must run under the built-in **njs**
JavaScript engine. The njs engine is deprecated since 1.0.0; the
QuickJS engine is the recommended path for any new code. Read this only
if you maintain an existing njs-engine codebase that you cannot port
yet. For general JS development under njs (both engines) see
[docs/agent/js-dev.md](js-dev.md).

## Language baseline

The njs engine implements **ECMAScript 5.1 (strict mode)** plus a
curated set of ES6+ extensions. The authoritative list lives on the
[compatibility page](https://nginx.org/en/docs/njs/compatibility.html).

What is shipped (highlights):

- Arrow functions, `let`/`const`, template literals.
- `Promise`, full prototype methods. `async` / `await` inside `async`
  functions (no top-level `await`).
- Rest parameters: `function f(...rest)`.
- Optional chaining `?.`, nullish coalescing `??`, logical assignments
  `||=` / `&&=` / `??=` (since 0.9.6).
- ES2016 exponentiation operator `**`.
- `Symbol` subset (`for`, `keyFor`).
- ES modules: **default `import` / default `export` only**. Non-default
  forms (`import { x } from "..."`, `import * as m from "..."`,
  `import "..."`) are rejected with `Non-default import is not supported`.
- `require()` is still supported (deprecated; prefer `import`).

## njs-engine-only features

The notable ones are `js_preload_object`, `njs.dump()` /
`console.dump()`, and `require()`. None of them work on QuickJS. See
[Engine differences](js-dev.md#engine-differences-at-a-glance) and
[Engine-specific bindings](js-dev.md#engine-specific-bindings) in
`js-dev.md` for the full list and links; the section below covers how
to remove each one when porting.

## Migration to QuickJS

When porting an existing njs-engine module:

1. Build NGINX with QuickJS linked in
   (`--with-cc-opt=-I<QUICKJS_SRC> --with-ld-opt=-L<QUICKJS_SRC>`), and
   set `js_engine qjs;` in the relevant `http { }` or `stream { }`
   block.
2. Replace `require()` calls with `import` statements.
3. Remove calls to `njs.dump()` / `console.dump()`; switch to
   `JSON.stringify` or a small helper.
4. If you used `js_preload_object`, fold the data into a regular module
   that the code `import`s. The shared dictionary
   (`ngx.shared` + `js_shared_dict_zone`) is the equivalent of preload
   for cross-worker shared state.
5. Modernize syntax — destructuring, `class`, spread, `Map`/`Set` —
   freely. They are all available under QuickJS.
6. Re-run the test suite under both engines until parity, then drop the
   njs-engine variant:

```bash
# njs (deprecated)
TEST_NGINX_BINARY=<NGINX_BIN> \
    prove -I <TESTS_LIB> nginx/t/<your>.t

# QuickJS
TEST_NGINX_GLOBALS_HTTP='js_engine qjs;' \
    TEST_NGINX_BINARY=<NGINX_BIN> \
    prove -I <TESTS_LIB> nginx/t/<your>.t
```

## Resources

- [Compatibility (full ECMAScript list)](https://nginx.org/en/docs/njs/compatibility.html)
- [Engine selection (deprecation note)](https://nginx.org/en/docs/njs/engine.html)
- [Reference (API surface — same on both engines)](https://nginx.org/en/docs/njs/reference.html)
- [TypeScript type definitions in `ts/`](../../ts/) — authoritative
  per-symbol interface description, identical on both engines
- [General JS development guide (both engines)](js-dev.md)
