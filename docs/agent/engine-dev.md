# Engine and module development (C)

This document covers C development inside the njs repository: the engine
core (`src/`), the external module wrappers (`external/`), the NGINX
modules (`nginx/`), and the build system (`auto/`, `configure`).

For per-task orientation see the top-level [AGENTS.md](../../AGENTS.md).

## Building

njs has no autotools/cmake. The shell-based `configure` script generates
`build/Makefile`. Always run `make clean` before reconfiguring with
different options — the Makefile is not regenerated in place.

### Standalone CLI

```bash
./configure
make -j$(nproc) njs        # build/njs
```

### njs with QuickJS backend

QuickJS is built separately and linked into njs.

```bash
# Build libquickjs.a in the QuickJS source tree
( cd <QUICKJS_SRC> && CFLAGS=-fPIC make libquickjs.a )

# Configure njs to use it
make clean
./configure \
    --cc-opt='-I<QUICKJS_SRC>' \
    --ld-opt='-L<QUICKJS_SRC>'
make -j$(nproc) njs
```

### NGINX module (static / dynamic)

njs builds as an NGINX module from a separate NGINX source tree.

```bash
# Static
cd <NGINX_SRC>
./auto/configure --add-module=<NJS_SRC>/nginx --with-stream --with-debug
make -j$(nproc)

# Dynamic
./auto/configure --add-dynamic-module=<NJS_SRC>/nginx --with-stream
make -j$(nproc) modules
```

Adding `--with-cc-opt='-I<QUICKJS_SRC>'` and
`--with-ld-opt='-L<QUICKJS_SRC>'` enables QuickJS in the NGINX module.

### AddressSanitizer build

```bash
./auto/configure --add-module=<NJS_SRC>/nginx --with-stream --with-debug \
    --with-cc=clang \
    --with-cc-opt='-O0 -fsanitize=address' \
    --with-ld-opt='-fsanitize=address'
make -j$(nproc)
```

The njs `configure` exposes `--address-sanitizer=YES` directly when
building the CLI; prefer `clang` on arm64 (gcc ASan is slow there).

### Configure options (njs)

| Option | Purpose |
|---|---|
| `--cc=FILE` | C compiler (default: gcc) |
| `--cc-opt=OPTIONS` | Additional CFLAGS |
| `--ld-opt=OPTIONS` | Additional LDFLAGS |
| `--debug=YES` | Runtime checks |
| `--debug-memory=YES` | Memory allocation tracing |
| `--debug-opcode=YES` | Per-instruction execution trace |
| `--debug-generator=YES` | Bytecode generator trace |
| `--address-sanitizer=YES` | AddressSanitizer (use with `clang`) |
| `--with-quickjs` | Require QuickJS to be present |
| `--no-openssl` / `--no-libxml2` / `--no-zlib` | Drop optional deps |

Run `./configure --help` for the complete list.

## Testing

```bash
make unit_test     # 5800+ language and API tests
make lib_test      # internal data structures (hash, rbtree, unicode)
make test262       # ECMAScript test262 compliance suite
make test          # shell tests + unit_test + test262
```

NGINX integration tests live under `nginx/t/` and use Perl's `prove`
harness against `Test::Nginx`.

```bash
TMPDIR=$(mktemp -d) \
TEST_NGINX_BINARY=<NGINX_BIN> \
    prove -I <TESTS_LIB> nginx/t/
```

Useful environment variables:

| Variable | Effect |
|---|---|
| `TEST_NGINX_BINARY` | Path to the nginx binary (required) |
| `TEST_NGINX_VERBOSE=1` | Verbose harness output |
| `TEST_NGINX_LEAVE=1` | Keep test artifacts in `$TMPDIR/nginx-test-*` |
| `TEST_NGINX_CATLOG=1` | Dump `error.log` after the run |
| `TEST_NGINX_GLOBALS=<conf>` | Inject global-scope config (e.g. `load_module ...`) |
| `TEST_NGINX_GLOBALS_HTTP='js_engine qjs;'` | Run http tests under QuickJS |
| `TEST_NGINX_GLOBALS_STREAM='js_engine qjs;'` | Same, for stream tests |

Use a per-run `TMPDIR=$(mktemp -d)` to isolate artifacts across concurrent
runs and avoid destructive `rm -fr /tmp/nginx-test*`.

For more on the harness see `<TESTS_LIB>/Test/Nginx.pm`.

## Validation checklist

Before submitting a change:

1. `./configure && make -j$(nproc)` compiles without warnings (`-Werror`).
2. `make unit_test` and `make lib_test` pass.
3. If you touched `src/`, also run `make test262`.
4. If you touched `nginx/`, run `prove -I <TESTS_LIB> nginx/t/`,
   once with the default engine and once with
   `TEST_NGINX_GLOBALS_HTTP='js_engine qjs;'`.
5. New source files: update `auto/sources` (njs core),
   `auto/modules` (njs external modules), or
   `auto/qjs_modules` (QuickJS external modules).
6. Dual-engine: if you added/changed behavior in an `njs_*.c` module,
   mirror it in the corresponding `qjs_*.c` (and vice versa).

## Code style and commits

NGINX coding style:

- 4 spaces, no tabs.
- 80-column line limit.
- No trailing whitespace.
- Newline after closing brace.
- Comments explain *why*, not *what*; avoid em-dashes.
- `-Werror` is on by default — fix all warnings.

Commit messages:

- Past tense subject (`Added X`, `Fixed Y`).
- Subject ≤72 chars, body wrapped to ~80 chars.
- Use a subject prefix only when local history clearly uses one for the
  changed area, such as `HTTP:`, `Stream:`, `QuickJS:`, `Tests:`, or
  `Modules:`. Otherwise use no prefix; do not invent generic prefixes.
- Read recent commit history and write commit logs in the established njs
  style.
- One logical change per commit; rebase/squash before submitting.

## Project layout

```
njs/
├── configure              # build entry point
├── auto/                  # shell-based build system
│   ├── sources            # njs core source list
│   ├── modules            # njs external module list
│   ├── qjs_modules        # QuickJS external module list
│   └── cc, options, ...   # compiler/option detection
├── src/                   # engine core (C)
│   ├── njs_vm.c / njs_vmcode.c    # virtual machine
│   ├── njs_lexer.c                # tokenizer
│   ├── njs_parser.c               # parser
│   ├── njs_generator.c            # bytecode generator
│   ├── njs_object.c / njs_array.c # built-in types
│   ├── njs_promise.c / njs_async.c
│   ├── njs_value.h                # value representation
│   ├── njs.h                      # public C API
│   ├── qjs.c                      # QuickJS engine wrapper
│   └── test/                      # C unit tests
├── external/              # extension modules
│   ├── njs_shell.c        # CLI entry point (main())
│   ├── njs_*_module.c     # njs-engine modules (crypto, fs, ...)
│   └── qjs_*_module.c     # QuickJS-engine counterparts
├── nginx/                 # NGINX module integration
│   ├── ngx_http_js_module.c
│   ├── ngx_stream_js_module.c
│   ├── ngx_js.c           # core nginx-JS bindings
│   ├── config             # NGINX build glue
│   └── t/                 # Perl integration tests
├── test/                  # functional test suite
│   ├── js/                # JS language feature tests
│   ├── harness/           # test framework utilities
│   └── shell_test.exp     # interactive shell tests (Expect)
└── ts/                    # TypeScript type definitions
```

Public C API: `src/njs.h`. VM internals: `src/njs_vm.h`,
`src/njs_value.h`. CLI entry point: `external/njs_shell.c`.

## VM architecture (njs engine)

The QuickJS backend uses upstream QuickJS internals (see
[bellard.org/quickjs](https://bellard.org/quickjs/)). What follows is the
**njs engine** internals only.

### Register-based VM

Each instruction has operands that are immediate values or **indexes**.
An index is encoded as:

```
index | level_type (4 bits) | var_type (4 bits)
```

### Level types (storage location)

```
NJS_LEVEL_LOCAL    = 0   // local variable in current frame
NJS_LEVEL_CLOSURE  = 1   // closure variable from parent frame
NJS_LEVEL_GLOBAL   = 2   // global variable
NJS_LEVEL_STATIC   = 3   // static / absolute scope
```

Values are addressed as `vm->levels[NJS_LEVEL_*][index]`.

### Variable types

```
NJS_VARIABLE_CONST    = 0
NJS_VARIABLE_LET      = 1
NJS_VARIABLE_CATCH    = 2
NJS_VARIABLE_VAR      = 3
NJS_VARIABLE_FUNCTION = 4
```

### Bytecode example

```
$ ./build/njs -d
>> var a = 42; function f(v) { return v + 1 }

shell:main
    1 | 00000 MOVE     0123 0133
    1 | 00024 STOP     0033

shell:f
    1 | 00000 ADD      0203 0103 0233
    1 | 00032 RETURN   0203
```

`MOVE 0123 0133` copies the value at index `0x0133` to `0x0123`.
`ADD a b c` computes `a = b + c`. Indexes are printed in hex and encode
level and variable type.

## Object model (njs engine)

For performance and footprint, a JS object is split into a **local mutable
hash** for the current object and a **shared hash** holding inherited
properties. Built-ins are lazily materialized: the shared definitions stay
shared until first mutation. For functions, the first access copies the
function from the shared hash into the local mutable hash so the
per-object copy can be modified.

Key entry points:

- `njs_value_property()` — top-level property lookup.
- `njs_property_query()` — lookup with descriptor result.
- `njs_object_property_query()` — object-level walk including prototype.
- `njs_prop_private_copy()` — promotion from shared to local on write.

## Debugging

### CLI

```bash
./build/njs -c '<code>'      # one-shot
./build/njs -d               # interactive, with disassembly
./build/njs -d script.js     # dump bytecode for a script
./build/njs -o script.js     # opcode trace
./build/njs -h               # full option list
```

Select the JavaScript engine with `-n <engine>` (case-insensitive; default
is `njs`):

```bash
./build/njs -n njs     -c 'console.log(typeof Map)'   # built-in engine
./build/njs -n QuickJS -c 'console.log(typeof Map)'   # QuickJS backend
```

`-n QuickJS` requires the binary to be built with QuickJS linked in (see
[njs with QuickJS backend](#njs-with-quickjs-backend) above); otherwise
the CLI reports `unknown engine "QuickJS"`.

### Opcode trace

Built with `--debug-opcode=YES`, `./build/njs -o script.js` prints each
instruction as it executes — `ENTER`/`EXIT` for function boundaries,
opcode mnemonics for everything else. Useful for confirming control flow
through bytecode without a debugger.

### Test failures (NGINX)

With `TEST_NGINX_LEAVE=1`, each test leaves
`$TMPDIR/nginx-test-<random>/` containing the generated `nginx.conf`,
`error.log`, and any artifacts. `TEST_NGINX_CATLOG=1` dumps the log to
stdout automatically.
