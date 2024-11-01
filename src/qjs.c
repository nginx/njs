
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>
#include <njs.h> /* NJS_VERSION */

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>


typedef struct {
    njs_str_t       name;
    int             value;
} qjs_signal_entry_t;


extern char  **environ;


static JSValue qjs_njs_getter(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_njs_to_string_tag(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_to_string_tag(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_argv(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_env(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_kill(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_process_pid(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_ppid(JSContext *ctx, JSValueConst this_val);


/* P1990 signals from `man 7 signal` are supported */
static qjs_signal_entry_t qjs_signals_table[] = {
    { njs_str("ABRT"), SIGABRT },
    { njs_str("ALRM"), SIGALRM },
    { njs_str("CHLD"), SIGCHLD },
    { njs_str("CONT"), SIGCONT },
    { njs_str("FPE"),  SIGFPE  },
    { njs_str("HUP"),  SIGHUP  },
    { njs_str("ILL"),  SIGILL  },
    { njs_str("INT"),  SIGINT  },
    { njs_str("KILL"), SIGKILL },
    { njs_str("PIPE"), SIGPIPE },
    { njs_str("QUIT"), SIGQUIT },
    { njs_str("SEGV"), SIGSEGV },
    { njs_str("STOP"), SIGSTOP },
    { njs_str("TSTP"), SIGTSTP },
    { njs_str("TERM"), SIGTERM },
    { njs_str("TTIN"), SIGTTIN },
    { njs_str("TTOU"), SIGTTOU },
    { njs_str("USR1"), SIGUSR1 },
    { njs_str("USR2"), SIGUSR2 },
    { njs_null_str, 0 }
};


static const JSCFunctionListEntry qjs_global_proto[] = {
    JS_CGETSET_DEF("njs", qjs_njs_getter, NULL),
};

static const JSCFunctionListEntry qjs_njs_proto[] = {
    JS_CGETSET_DEF("[Symbol.toStringTag]", qjs_njs_to_string_tag, NULL),
    JS_PROP_STRING_DEF("version", NJS_VERSION, JS_PROP_C_W_E),
    JS_PROP_INT32_DEF("version_number", NJS_VERSION_NUMBER,
                      JS_PROP_C_W_E),
    JS_PROP_STRING_DEF("engine", "QuickJS", JS_PROP_C_W_E),
};

static const JSCFunctionListEntry qjs_process_proto[] = {
    JS_CGETSET_DEF("[Symbol.toStringTag]", qjs_process_to_string_tag, NULL),
    JS_CGETSET_DEF("argv", qjs_process_argv, NULL),
    JS_CGETSET_DEF("env", qjs_process_env, NULL),
    JS_CFUNC_DEF("kill", 2, qjs_process_kill),
    JS_CGETSET_DEF("pid", qjs_process_pid, NULL),
    JS_CGETSET_DEF("ppid", qjs_process_ppid, NULL),
};


JSContext *
qjs_new_context(JSRuntime *rt, qjs_module_t **addons)
{
    int           ret;
    JSAtom        prop;
    JSValue       global_obj;
    JSContext     *ctx;
    qjs_module_t  **module;

    ctx = JS_NewContextRaw(rt);
    if (ctx == NULL) {
        return NULL;
    }

    JS_AddIntrinsicBaseObjects(ctx);
    JS_AddIntrinsicDate(ctx);
    JS_AddIntrinsicRegExp(ctx);
    JS_AddIntrinsicJSON(ctx);
    JS_AddIntrinsicProxy(ctx);
    JS_AddIntrinsicMapSet(ctx);
    JS_AddIntrinsicTypedArrays(ctx);
    JS_AddIntrinsicPromise(ctx);
    JS_AddIntrinsicBigInt(ctx);
    JS_AddIntrinsicEval(ctx);

    for (module = qjs_modules; *module != NULL; module++) {
        if ((*module)->init(ctx, (*module)->name) == NULL) {
            return NULL;
        }
    }

    if (addons != NULL) {
        for (module = addons; *module != NULL; module++) {
            if ((*module)->init(ctx, (*module)->name) == NULL) {
                return NULL;
            }
        }
    }

    global_obj = JS_GetGlobalObject(ctx);

    JS_SetPropertyFunctionList(ctx, global_obj, qjs_global_proto,
                               njs_nitems(qjs_global_proto));

    prop = JS_NewAtom(ctx, "eval");
    if (prop == JS_ATOM_NULL) {
        return NULL;
    }

    ret = JS_DeleteProperty(ctx, global_obj, prop, 0);
    JS_FreeAtom(ctx, prop);
    if (ret < 0) {
        return NULL;
    }

    prop = JS_NewAtom(ctx, "Function");
    if (prop == JS_ATOM_NULL) {
        return NULL;
    }

    ret = JS_DeleteProperty(ctx, global_obj, prop, 0);
    JS_FreeAtom(ctx, prop);
    if (ret < 0) {
        return NULL;
    }

    JS_FreeValue(ctx, global_obj);

    return ctx;
}


static JSValue
qjs_njs_getter(JSContext *ctx, JSValueConst this_val)
{
    JSValue  obj;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_SetPropertyFunctionList(ctx, obj, qjs_njs_proto,
                               njs_nitems(qjs_njs_proto));

    return obj;
}


static JSValue
qjs_njs_to_string_tag(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewString(ctx, "njs");
}


static JSValue
qjs_process_to_string_tag(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewString(ctx, "process");
}


static JSValue
qjs_process_argv(JSContext *ctx, JSValueConst this_val)
{
    int         i, ret, argc;
    JSValue     val, str;
    const char  **argv;

    val = JS_GetPropertyStr(ctx, this_val, "argc");
    if (JS_IsException(val)) {
        return JS_EXCEPTION;
    }

    if (JS_ToInt32(ctx, &argc, val) < 0) {
        return JS_EXCEPTION;
    }

    argv = JS_GetOpaque(this_val, JS_GetClassID(this_val));
    if (argv == NULL) {
        return JS_NewArray(ctx);
    }

    val = JS_NewArray(ctx);
    if (JS_IsException(val)) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < argc; i++) {
        str = JS_NewStringLen(ctx, argv[i], njs_strlen(argv[i]));
        if (JS_IsException(str)) {
            JS_FreeValue(ctx, val);
            return JS_EXCEPTION;
        }

        ret = JS_DefinePropertyValueUint32(ctx, val, i, str, JS_PROP_C_W_E);
        if (ret < 0) {
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, val);
            return JS_EXCEPTION;
        }
    }

    return val;
}


static JSValue
qjs_process_env(JSContext *ctx, JSValueConst this_val)
{
    int         ret;
    char        **ep;
    JSValue     obj;
    JSAtom      atom;
    JSValue     str, name;
    const char  *entry, *value;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    ep = environ;

    while (*ep != NULL) {
        entry = *ep++;

        value = (const char *) njs_strchr(entry, '=');
        if (value == NULL) {
            continue;
        }

        str = JS_UNDEFINED;
        name = JS_NewStringLen(ctx, entry, value - entry);
        if (JS_IsException(name)) {
            goto error;
        }

        value++;

        str = JS_NewStringLen(ctx, value, njs_strlen(value));
        if (JS_IsException(str)) {
            goto error;
        }

        atom = JS_ValueToAtom(ctx, name);
        if (atom == JS_ATOM_NULL) {
            goto error;
        }

        ret = JS_DefinePropertyValue(ctx, obj, atom, str, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
        if (ret < 0) {
error:
            JS_FreeValue(ctx, name);
            JS_FreeValue(ctx, str);
            return JS_EXCEPTION;
        }

        JS_FreeValue(ctx, name);
    }

    return obj;
}


static JSValue
qjs_process_kill(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int                  signo, pid;
    JSValue              val;
    njs_str_t            name;
    const char           *signal;
    qjs_signal_entry_t   *entry;

    if (JS_ToInt32(ctx, &pid, argv[0]) < 0) {
        return JS_EXCEPTION;
    }

    if (JS_IsNumber(argv[1])) {
        if (JS_ToInt32(ctx, &signo, argv[1]) < 0) {
            return JS_EXCEPTION;
        }

        if (signo < 0 || signo >= NSIG) {
            return JS_ThrowTypeError(ctx, "unknown signal: %d", signo);
        }

    } else {
        val = JS_ToString(ctx, argv[1]);
        if (JS_IsException(val)) {
            return JS_EXCEPTION;
        }

        signal = JS_ToCString(ctx, val);
        if (signal == NULL) {
            JS_FreeValue(ctx, val);
            return JS_EXCEPTION;
        }

        if (njs_strlen(signal) < 3 || memcmp(signal, "SIG", 3) != 0) {
            JS_FreeCString(ctx, signal);
            return JS_ThrowTypeError(ctx, "unknown signal: %s", signal);
        }

        name.start = (u_char *) signal + 3;
        name.length = njs_strlen(signal) - 3;

        for (entry = qjs_signals_table; entry->name.length != 0; entry++) {
            if (njs_strstr_eq(&entry->name, &name)) {
                signo = entry->value;
                break;
            }
        }

        JS_FreeCString(ctx, signal);

        if (entry->name.length == 0) {
            return JS_ThrowTypeError(ctx, "unknown signal: %s", signal);
        }
    }

    if (kill(pid, signo) < 0) {
        return JS_ThrowTypeError(ctx, "kill failed with (%d:%s)", errno,
                                 strerror(errno));
    }

    return JS_TRUE;
}


static JSValue
qjs_process_pid(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewInt32(ctx, getpid());
}


static JSValue
qjs_process_ppid(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewInt32(ctx, getppid());
}


JSValue
qjs_process_object(JSContext *ctx, int argc, const char **argv)
{
    JSValue  obj;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_SetPropertyFunctionList(ctx, obj, qjs_process_proto,
                               njs_nitems(qjs_process_proto));

    JS_SetOpaque(obj, argv);

    if (JS_SetPropertyStr(ctx, obj, "argc", JS_NewInt32(ctx, argc)) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    return obj;
}


int
qjs_to_bytes(JSContext *ctx, qjs_bytes_t *bytes, JSValueConst value)
{
    size_t   byte_offset, byte_length;
    JSValue  val;

    if (JS_IsString(value)) {
        goto string;
    }

    val = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length, NULL);
    if (!JS_IsException(val)) {
        bytes->start = JS_GetArrayBuffer(ctx, &bytes->length, val);

        JS_FreeValue(ctx, val);

        if (bytes->start != NULL) {
            bytes->tag = JS_TAG_OBJECT;
            bytes->start += byte_offset;
            bytes->length = byte_length;
            return 0;
        }
    }

    bytes->start = JS_GetArrayBuffer(ctx, &bytes->length, value);
    if (bytes->start != NULL) {
        bytes->tag = JS_TAG_OBJECT;
        return 0;
    }

    if (!JS_IsString(value)) {
        val = JS_ToString(ctx, value);

        bytes->start = (u_char *) JS_ToCStringLen(ctx, &bytes->length, val);

        JS_FreeValue(ctx, val);

        if (bytes->start == NULL) {
            return -1;
        }
    }

string:

    bytes->tag = JS_TAG_STRING;
    bytes->start = (u_char *) JS_ToCStringLen(ctx, &bytes->length, value);

    return (bytes->start != NULL) ? 0 : -1;
}


void
qjs_bytes_free(JSContext *ctx, qjs_bytes_t *bytes)
{
    if (bytes->tag == JS_TAG_STRING) {
        JS_FreeCString(ctx, (char *) bytes->start);
    }
}


JSValue
qjs_typed_array_data(JSContext *ctx, JSValueConst value, njs_str_t *data)
{
    size_t  byte_offset, byte_length;

    value = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length,
                                   NULL);
    if (JS_IsException(value)) {
        return value;
    }

    data->start = JS_GetArrayBuffer(ctx, &data->length, value);

    JS_FreeValue(ctx, value);

    if (data->start == NULL) {
        return JS_EXCEPTION;
    }

    data->start += byte_offset;
    data->length = byte_length;

    return JS_UNDEFINED;
}


JSValue
qjs_string_create_chb(JSContext *cx, njs_chb_t *chain)
{
    JSValue    val;
    njs_int_t  ret;
    njs_str_t  str;

    ret = njs_chb_join(chain, &str);
    if (ret != NJS_OK) {
        return JS_ThrowInternalError(cx, "failed to create string");
    }

    val = JS_NewStringLen(cx, (const char *) str.start, str.length);

    chain->free(cx, str.start);

    return val;
}
