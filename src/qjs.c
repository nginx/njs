
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


typedef enum {
    QJS_ENCODING_UTF8,
} qjs_encoding_t;


typedef struct {
    qjs_encoding_t        encoding;
    int                   fatal;
    int                   ignore_bom;

    njs_unicode_decode_t  ctx;
} qjs_text_decoder_t;


typedef struct {
    njs_str_t             name;
    qjs_encoding_t        encoding;
} qjs_encoding_label_t;


extern char  **environ;


static JSValue qjs_njs_getter(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_env(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_kill(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_process_pid(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_process_ppid(JSContext *ctx, JSValueConst this_val);

static int qjs_add_intrinsic_text_decoder(JSContext *cx, JSValueConst global);
static JSValue qjs_text_decoder_decode(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_text_decoder_encoding(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_text_decoder_fatal(JSContext *ctx, JSValueConst this_val);
static JSValue qjs_text_decoder_ignore_bom(JSContext *ctx,
    JSValueConst this_val);
static void qjs_text_decoder_finalizer(JSRuntime *rt, JSValue val);

static int qjs_add_intrinsic_text_encoder(JSContext *cx, JSValueConst global);
static JSValue qjs_text_encoder_encode(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_text_encoder_encode_into(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_text_encoder_encoding(JSContext *ctx, JSValueConst this_val);


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


static qjs_encoding_label_t  qjs_encoding_labels[] =
{
    { njs_str("utf-8"), QJS_ENCODING_UTF8 },
    { njs_str("utf8") , QJS_ENCODING_UTF8 },
    { njs_null_str, 0 }
};


static const JSCFunctionListEntry qjs_global_proto[] = {
    JS_CGETSET_DEF("njs", qjs_njs_getter, NULL),
};

static const JSCFunctionListEntry qjs_text_decoder_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextDecoder",
                       JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("decode", 1, qjs_text_decoder_decode),
    JS_CGETSET_DEF("encoding", qjs_text_decoder_encoding, NULL),
    JS_CGETSET_DEF("fatal", qjs_text_decoder_fatal, NULL),
    JS_CGETSET_DEF("ignoreBOM", qjs_text_decoder_ignore_bom, NULL),
};

static const JSCFunctionListEntry qjs_text_encoder_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextEncoder",
                       JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("encode", 1, qjs_text_encoder_encode),
    JS_CFUNC_DEF("encodeInto", 1, qjs_text_encoder_encode_into),
    JS_CGETSET_DEF("encoding", qjs_text_encoder_encoding, NULL),
};

static const JSCFunctionListEntry qjs_njs_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "njs", JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("version", NJS_VERSION, JS_PROP_C_W_E),
    JS_PROP_INT32_DEF("version_number", NJS_VERSION_NUMBER,
                      JS_PROP_C_W_E),
    JS_PROP_STRING_DEF("engine", "QuickJS", JS_PROP_C_W_E),
};

static const JSCFunctionListEntry qjs_process_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "process", JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("env", qjs_process_env, NULL),
    JS_CFUNC_DEF("kill", 2, qjs_process_kill),
    JS_CGETSET_DEF("pid", qjs_process_pid, NULL),
    JS_CGETSET_DEF("ppid", qjs_process_ppid, NULL),
};


static JSClassDef qjs_text_decoder_class = {
    "TextDecoder",
    .finalizer = qjs_text_decoder_finalizer,
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

    if (qjs_add_intrinsic_text_decoder(ctx, global_obj) < 0) {
        return NULL;
    }

    if (qjs_add_intrinsic_text_encoder(ctx, global_obj) < 0) {
        return NULL;
    }

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

    signo = SIGTERM;

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
    int      i;
    JSValue  obj, str, val;

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

        if (JS_DefinePropertyValueUint32(ctx, val, i, str, JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, val);
            return JS_EXCEPTION;
        }
    }

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }

    JS_SetPropertyFunctionList(ctx, obj, qjs_process_proto,
                               njs_nitems(qjs_process_proto));

    if (JS_SetPropertyStr(ctx, obj, "argv", val) < 0) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }

    return obj;
}


static int
qjs_text_decoder_encoding_arg(JSContext *cx, int argc, JSValueConst *argv,
    qjs_text_decoder_t *td)
{
    njs_str_t             str;
    qjs_encoding_label_t  *label;

    if (argc < 1) {
        td->encoding = QJS_ENCODING_UTF8;
        return 0;
    }

    str.start = (u_char *) JS_ToCStringLen(cx, &str.length, argv[0]);
    if (str.start == NULL) {
        JS_ThrowOutOfMemory(cx);
        return -1;
    }

    for (label = &qjs_encoding_labels[0]; label->name.length != 0; label++) {
        if (njs_strstr_eq(&str, &label->name)) {
            td->encoding = label->encoding;
            JS_FreeCString(cx, (char *) str.start);
            return 0;
        }
    }

    JS_ThrowTypeError(cx, "The \"%.*s\" encoding is not supported",
                     (int) str.length, str.start);
    JS_FreeCString(cx, (char *) str.start);

    return -1;
}


static int
qjs_text_decoder_options(JSContext *cx, int argc, JSValueConst *argv,
    qjs_text_decoder_t *td)
{
    JSValue  val;

    if (argc < 2) {
        td->fatal = 0;
        td->ignore_bom = 0;

        return 0;
    }

    val = JS_GetPropertyStr(cx, argv[1], "fatal");
    if (JS_IsException(val)) {
        return -1;
    }

    td->fatal = JS_ToBool(cx, val);
    JS_FreeValue(cx, val);

    val = JS_GetPropertyStr(cx, argv[1], "ignoreBOM");
    if (JS_IsException(val)) {
        return -1;
    }

    td->ignore_bom = JS_ToBool(cx, val);
    JS_FreeValue(cx, val);

    return 0;
}


static JSValue
qjs_text_decoder_ctor(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue             obj;
    qjs_text_decoder_t  *td;

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_TEXT_DECODER);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    td = js_mallocz(cx, sizeof(qjs_text_decoder_t));
    if (td == NULL) {
        JS_ThrowOutOfMemory(cx);
        JS_FreeValue(cx, obj);
        return JS_EXCEPTION;
    }

    if (qjs_text_decoder_encoding_arg(cx, argc, argv, td) < 0) {
        js_free(cx, td);
        JS_FreeValue(cx, obj);
        return JS_EXCEPTION;
    }

    if (qjs_text_decoder_options(cx, argc, argv, td) < 0) {
        js_free(cx, td);
        JS_FreeValue(cx, obj);
        return JS_EXCEPTION;
    }

    njs_utf8_decode_init(&td->ctx);

    JS_SetOpaque(obj, td);

    return obj;
}


static int
qjs_add_intrinsic_text_decoder(JSContext *cx, JSValueConst global)
{
    JSValue  ctor, proto;

    if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_ID_TEXT_DECODER,
                    &qjs_text_decoder_class) < 0)
    {
        return -1;
    }

    proto = JS_NewObject(cx);
    if (JS_IsException(proto)) {
        return -1;
    }

    JS_SetPropertyFunctionList(cx, proto, qjs_text_decoder_proto,
                               njs_nitems(qjs_text_decoder_proto));

    JS_SetClassProto(cx, QJS_CORE_CLASS_ID_TEXT_DECODER, proto);

    ctor = JS_NewCFunction2(cx, qjs_text_decoder_ctor, "TextDecoder", 2,
                              JS_CFUNC_constructor, 0);
    if (JS_IsException(ctor)) {
        return -1;
    }

    JS_SetConstructor(cx, ctor, proto);

    return JS_SetPropertyStr(cx, global, "TextDecoder", ctor);
}


static JSValue
qjs_text_decoder_decode(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int                   stream;
    size_t                size;
    u_char                *dst;
    JSValue               ret;
    ssize_t               length;
    njs_str_t             data;
    const u_char          *end;
    qjs_text_decoder_t    *td;
    njs_unicode_decode_t  ctx;

    td = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_TEXT_DECODER);
    if (td == NULL) {
        return JS_ThrowInternalError(cx, "'this' is not a TextDecoder");
    }

    ret = qjs_typed_array_data(cx, argv[0], &data);
    if (JS_IsException(ret)) {
        return ret;
    }

    stream = 0;

    if (argc > 1) {
        ret = JS_GetPropertyStr(cx, argv[1], "stream");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        stream = JS_ToBool(cx, ret);
        JS_FreeValue(cx, ret);
    }

    ctx = td->ctx;
    end = data.start + data.length;

    if (data.start != NULL && !td->ignore_bom) {
        data.start += njs_utf8_bom(data.start, end);
    }

    length = njs_utf8_stream_length(&ctx, data.start, end - data.start, !stream,
                                    td->fatal, &size);

    if (length == -1) {
        return JS_ThrowTypeError(cx, "The encoded data was not valid");
    }

    dst = js_malloc(cx, size + 1);
    if (dst == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    (void) njs_utf8_stream_encode(&td->ctx, data.start, end, dst, !stream, 0);

    ret = JS_NewStringLen(cx, (const char *) dst, size);
    js_free(cx, dst);

    if (!stream) {
        njs_utf8_decode_init(&td->ctx);
    }

    return ret;
}


static JSValue
qjs_text_decoder_encoding(JSContext *ctx, JSValueConst this_val)
{
    qjs_text_decoder_t  *td;

    td = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_TEXT_DECODER);
    if (td == NULL) {
        return JS_ThrowInternalError(ctx, "'this' is not a TextDecoder");
    }

    switch (td->encoding) {
    case QJS_ENCODING_UTF8:
        return JS_NewString(ctx, "utf-8");
    }

    return JS_UNDEFINED;
}


static JSValue
qjs_text_decoder_fatal(JSContext *ctx, JSValueConst this_val)
{
    qjs_text_decoder_t  *td;

    td = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_TEXT_DECODER);
    if (td == NULL) {
        return JS_ThrowInternalError(ctx, "'this' is not a TextDecoder");
    }

    return JS_NewBool(ctx, td->fatal);
}


static JSValue
qjs_text_decoder_ignore_bom(JSContext *ctx, JSValueConst this_val)
{
    qjs_text_decoder_t  *td;

    td = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_TEXT_DECODER);
    if (td == NULL) {
        return JS_ThrowInternalError(ctx, "'this' is not a TextDecoder");
    }

    return JS_NewBool(ctx, td->ignore_bom);
}


static void
qjs_text_decoder_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_text_decoder_t  *td;

    td = JS_GetOpaque(val, QJS_CORE_CLASS_ID_TEXT_DECODER);
    if (td != NULL) {
        js_free_rt(rt, td);
    }
}


static JSValue
qjs_text_encoder_ctor(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue  obj;

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_TEXT_ENCODER);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, (void *) 1);

    return obj;
}


static int
qjs_add_intrinsic_text_encoder(JSContext *cx, JSValueConst global)
{
    JSValue  ctor, proto;

    proto = JS_NewObject(cx);
    if (JS_IsException(proto)) {
        return -1;
    }

    JS_SetPropertyFunctionList(cx, proto, qjs_text_encoder_proto,
                               njs_nitems(qjs_text_encoder_proto));

    JS_SetClassProto(cx, QJS_CORE_CLASS_ID_TEXT_ENCODER, proto);

    ctor = JS_NewCFunction2(cx, qjs_text_encoder_ctor, "TextEncoder", 0,
                              JS_CFUNC_constructor, 0);
    if (JS_IsException(ctor)) {
        return -1;
    }

    JS_SetConstructor(cx, ctor, proto);

    return JS_SetPropertyStr(cx, global, "TextEncoder", ctor);
}


static JSValue
qjs_text_encoder_encoding(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewString(ctx, "utf-8");
}


static JSValue
qjs_text_encoder_encode(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    void      *te;
    JSValue    len, ta, ret;
    njs_str_t  utf8, dst;

    te = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_TEXT_ENCODER);
    if (te == NULL) {
        return JS_ThrowInternalError(cx, "'this' is not a TextEncoder");
    }

    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(cx, "The input argument must be a string");
    }

    utf8.start = (u_char *) JS_ToCStringLen(cx, &utf8.length, argv[0]);
    if (utf8.start == NULL) {
        return JS_EXCEPTION;
    }

    len = JS_NewInt64(cx, utf8.length);

    ta = qjs_new_uint8_array(cx, 1, &len);
    if (JS_IsException(ta)) {
        JS_FreeCString(cx, (char *) utf8.start);
        return ta;
    }

    ret = qjs_typed_array_data(cx, ta, &dst);
    if (JS_IsException(ret)) {
        JS_FreeCString(cx, (char *) utf8.start);
        return ret;
    }

    memcpy(dst.start, utf8.start, utf8.length);
    JS_FreeCString(cx, (char *) utf8.start);

    return ta;
}


static int
qjs_is_uint8_array(JSContext *cx, JSValueConst value)
{
    int      ret;
    JSValue  ctor, global;

    global = JS_GetGlobalObject(cx);

    ctor = JS_GetPropertyStr(cx, global, "Uint8Array");
    if (JS_IsException(ctor)) {
        JS_FreeValue(cx, global);
        return -1;
    }

    ret = JS_IsInstanceOf(cx, value, ctor);
    JS_FreeValue(cx, ctor);
    JS_FreeValue(cx, global);

    return ret;
}


static JSValue
qjs_text_encoder_encode_into(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int                   read, written;
    void                  *te;
    size_t                size;
    u_char                *to, *to_end;
    JSValue               ret;
    uint32_t              cp;
    njs_str_t             utf8, dst;
    const u_char          *start, *end;
    njs_unicode_decode_t  ctx;

    te = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_TEXT_ENCODER);
    if (te == NULL) {
        return JS_ThrowInternalError(cx, "'this' is not a TextEncoder");
    }

    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(cx, "The input argument must be a string");
    }

    ret = qjs_typed_array_data(cx, argv[1], &dst);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (!qjs_is_uint8_array(cx, argv[1])) {
        return JS_ThrowTypeError(cx, "The output argument must be a"
                                 " Uint8Array");
    }

    utf8.start = (u_char *) JS_ToCStringLen(cx, &utf8.length, argv[0]);
    if (utf8.start == NULL) {
        return JS_EXCEPTION;
    }

    read = 0;
    written = 0;

    start = utf8.start;
    end = start + utf8.length;

    to = dst.start;
    to_end = to + dst.length;

    njs_utf8_decode_init(&ctx);

    while (start < end) {
        cp = njs_utf8_decode(&ctx, &start, end);

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            cp = NJS_UNICODE_REPLACEMENT;
        }

        size = njs_utf8_size(cp);

        if (to + size > to_end) {
            break;
        }

        read += (cp > 0xFFFF) ? 2 : 1;
        written += size;

        to = njs_utf8_encode(to, cp);
    }

    JS_FreeCString(cx, (char *) utf8.start);

    ret = JS_NewObject(cx);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (JS_DefinePropertyValueStr(cx, ret, "read", JS_NewInt32(cx, read),
                                  JS_PROP_C_W_E) < 0)
    {
        JS_FreeValue(cx, ret);
        return JS_EXCEPTION;
    }

    if (JS_DefinePropertyValueStr(cx, ret, "written", JS_NewInt32(cx, written),
                                  JS_PROP_C_W_E) < 0)
    {
        JS_FreeValue(cx, ret);
        return JS_EXCEPTION;
    }

    return ret;
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
    size_t   byte_offset, byte_length;
    JSValue  ab;

    /* TODO: DataView. */

    ab = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length,
                                   NULL);
    if (JS_IsException(ab)) {
        data->start = JS_GetArrayBuffer(ctx, &data->length, value);
        if (data->start == NULL) {
            return JS_EXCEPTION;
        }

        return JS_UNDEFINED;
    }

    data->start = JS_GetArrayBuffer(ctx, &data->length, ab);

    JS_FreeValue(ctx, ab);

    if (data->start == NULL) {
        return JS_EXCEPTION;
    }

    data->start += byte_offset;
    data->length = byte_length;

    return JS_UNDEFINED;
}


static void
js_array_buffer_free(JSRuntime *rt, void *opaque, void *ptr)
{
    js_free_rt(rt, ptr);
}


JSValue
qjs_new_array_buffer(JSContext *cx, uint8_t *src, size_t len)
{
    return JS_NewArrayBuffer(cx, src, len, js_array_buffer_free, NULL, 0);
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


JSValue
qjs_string_hex(JSContext *cx, const njs_str_t *src)
{
    JSValue    ret;
    njs_str_t  dst;
    u_char     buf[1024];

    if (src->length == 0) {
        return JS_NewStringLen(cx, "", 0);
    }

    dst.start = buf;
    dst.length = qjs_hex_encode_length(cx, src);

    if (dst.length <= sizeof(buf)) {
        qjs_hex_encode(cx, src, &dst);
        ret = JS_NewStringLen(cx, (const char *) dst.start, dst.length);

    } else {
        dst.start = js_malloc(cx, dst.length);
        if (dst.start == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        qjs_hex_encode(cx, src, &dst);
        ret = JS_NewStringLen(cx, (const char *) dst.start, dst.length);
        js_free(cx, dst.start);
    }

    return ret;
}


JSValue
qjs_string_base64(JSContext *cx, const njs_str_t *src)
{
    JSValue    ret;
    njs_str_t  dst;
    u_char     buf[1024];

    if (src->length == 0) {
        return JS_NewStringLen(cx, "", 0);
    }

    dst.start = buf;
    dst.length = qjs_base64_encode_length(cx, src);

    if (dst.length <= sizeof(buf)) {
        qjs_base64_encode(cx, src, &dst);
        ret = JS_NewStringLen(cx, (const char *) dst.start, dst.length);

    } else {
        dst.start = js_malloc(cx, dst.length);
        if (dst.start == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        qjs_base64_encode(cx, src, &dst);
        ret = JS_NewStringLen(cx, (const char *) dst.start, dst.length);
        js_free(cx, dst.start);
    }

    return ret;
}


JSValue
qjs_string_base64url(JSContext *cx, const njs_str_t *src)
{
    size_t     padding;
    JSValue    ret;
    njs_str_t  dst;
    u_char     buf[1024];

    if (src->length == 0) {
        return JS_NewStringLen(cx, "", 0);
    }

    padding = src->length % 3;
    padding = (4 >> padding) & 0x03;

    dst.start = buf;
    dst.length = qjs_base64_encode_length(cx, src) - padding;

    if (dst.length <= sizeof(buf)) {
        qjs_base64url_encode(cx, src, &dst);
        ret = JS_NewStringLen(cx, (const char *) dst.start, dst.length);

    } else {
        dst.start = js_malloc(cx, dst.length);
        if (dst.start == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        qjs_base64url_encode(cx, src, &dst);
        ret = JS_NewStringLen(cx, (const char *) dst.start, dst.length);
        js_free(cx, dst.start);
    }

    return ret;
}
