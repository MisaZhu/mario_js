#ifdef __cplusplus
extern "C" {
#endif

#include "native_Process.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* ====== Node.js `process` global (minimal practical surface) ======
 * A singleton object hung on vm->root as "process". Bundled Node code routinely
 * feature-detects `typeof process !== 'undefined'`, reads process.env.NODE_ENV,
 * checks process.version, schedules process.nextTick, or writes to
 * process.stdout. This provides those without pulling in libuv.
 *
 * Scheduling note: nextTick rides js_dom_add_microtask (the same microtask hook
 * Promise reactions use), drained ahead of every 0-ms macrotask. In the browser
 * build that defers the callback to the engine event loop; in the standalone CLI
 * js_dom_add_microtask returns 0, so we run the callback inline (never lose
 * work) - matching the CLI's inline-reaction model. */

/* Provided by js_dom.c (browser) / host_task.c (CLI). */
int js_dom_add_timer(vm_t* vm, var_t* cb, uint32_t ms, bool repeat);
int js_dom_add_microtask(vm_t* vm, var_t* cb);

extern char** environ;

#if defined(__APPLE__)
#  define PROC_PLATFORM "darwin"
#elif defined(_WIN32)
#  define PROC_PLATFORM "win32"
#elif defined(__linux__)
#  define PROC_PLATFORM "linux"
#elif defined(__FreeBSD__)
#  define PROC_PLATFORM "freebsd"
#else
#  define PROC_PLATFORM "posix"
#endif

/* A Node version string plausible enough for semver gates in bundled deps. */
#define PROC_NODE_VERSION "18.20.0"

/* process.exit([code]): terminate the host process. In the CLI this ends the
 * run; code defaults to 0. */
static var_t* proc_exit(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)data;
    var_t* c = get_obj(env, "code");
    int code = (c != NULL) ? var_get_int(c) : 0;
    fflush(stdout);
    fflush(stderr);
    exit(code);
    return NULL;
}

/* process.cwd(): the current working directory, or "" if unavailable. */
static var_t* proc_cwd(vm_t* vm, var_t* env, void* data) {
    (void)env; (void)data;
    char buf[4096];
    if(getcwd(buf, sizeof(buf)) != NULL)
        return var_new_str(vm, buf);
    return var_new_str(vm, "");
}

/* process.nextTick(cb[, ...args]): run cb after the current operation. Extra
 * args are forwarded (CLI inline path); call_m_func wants them reversed. */
static var_t* proc_nextTick(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* args = get_func_args(env);
    node_t* cn = var_array_get(args, 0);
    if(cn == NULL || cn->var == NULL || !cn->var->is_func)
        return NULL;

    var_t* extra = NULL;
    uint32_t n = var_array_size(args);
    if(n > 1) {
        extra = var_new_array(vm);
        for(uint32_t i = 1; i < n; ++i) {
            node_t* an = var_array_get(args, (int32_t)i);
            var_array_add(extra, (an != NULL && an->var != NULL) ? an->var : var_new(vm));
        }
        var_array_reverse(extra);
    }

    int id = js_dom_add_microtask(vm, cn->var);
    if(id == 0) {   /* CLI (no timer table): never lose the callback - run inline */
        var_t* call_args = (extra != NULL) ? extra : var_new_array(vm);
        var_t* r = call_m_func(vm, NULL, cn->var, call_args);
        if(r != NULL) var_unref(r);
        if(extra == NULL) var_unref(call_args);
    }
    if(extra != NULL) var_unref(extra);
    return NULL;
}

/* process.stdout.write(str) / process.stderr.write(str): route to the single
 * platform output sink (the same one console.log uses). */
static var_t* proc_stream_write(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)data;
    var_t* args = get_func_args(env);
    node_t* sn = var_array_get(args, 0);
    if(sn != NULL && sn->var != NULL) {
        mstr_t* s = mstr_new("");
        var_to_str(sn->var, s);
        _platform_out(MSTR_CSTR(s));
        mstr_free(s);
    }
    return var_new_bool(vm, true);
}

/* Build a {write, isTTY:false} stream-ish object for stdout/stderr/stdin. */
static var_t* proc_make_stream(vm_t* vm) {
    var_t* s = var_new_obj(vm, NULL, NULL, NULL);
    vm_reg_native_on(vm, s, "write(str)", proc_stream_write, NULL);
    var_add(s, "isTTY", var_new_bool(vm, false));
    return s;
}

/* Snapshot the C `environ` into a plain {KEY: value} object. */
static var_t* proc_make_env(vm_t* vm) {
    var_t* envobj = var_new_obj(vm, NULL, NULL, NULL);
    if(environ != NULL) {
        for(char** e = environ; *e != NULL; ++e) {
            char* eq = strchr(*e, '=');
            if(eq == NULL) continue;
            size_t klen = (size_t)(eq - *e);
            char key[512];
            if(klen >= sizeof(key)) klen = sizeof(key) - 1;
            memcpy(key, *e, klen);
            key[klen] = 0;
            var_add(envobj, key, var_new_str(vm, eq + 1));
        }
    }
    return envobj;
}

void reg_native_Process(vm_t* vm) {
    var_t* proc = var_new_obj(vm, NULL, NULL, NULL);

    /* Identity / platform constants. */
    var_add(proc, "platform",  var_new_str(vm, PROC_PLATFORM));
    var_add(proc, "arch",      var_new_str(vm,
#if defined(__aarch64__) || defined(_M_ARM64)
        "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
        "x64"
#elif defined(__i386__) || defined(_M_IX86)
        "ia32"
#else
        "unknown"
#endif
    ));
    var_add(proc, "version",   var_new_str(vm, "v" PROC_NODE_VERSION));
    var_add(proc, "pid",       var_new_int(vm, (int)getpid()));
    var_add(proc, "ppid",      var_new_int(vm, (int)getppid()));
    var_add(proc, "title",     var_new_str(vm, "mario"));
    var_add(proc, "exitCode",  var_new_int(vm, 0));
    var_add(proc, "argv0",     var_new_str(vm, "mario"));

    /* process.versions = { node: "...", v8: "..." } - v8 left as a placeholder
     * so `process.versions.node` reads work. */
    var_t* versions = var_new_obj(vm, NULL, NULL, NULL);
    var_add(versions, "node", var_new_str(vm, PROC_NODE_VERSION));
    var_add(proc, "versions", versions);

    /* process.env / process.argv / process.execArgv. argv is populated from
     * vm->root's "_args", which main() installs AFTER vm_init builds this
     * object; native_Process_set_argv() refreshes it once _args exists. */
    var_add(proc, "env", proc_make_env(vm));
    var_add(proc, "argv", var_new_array(vm));
    var_add(proc, "execArgv", var_new_array(vm));

    /* I/O streams. */
    var_add(proc, "stdout", proc_make_stream(vm));
    var_add(proc, "stderr", proc_make_stream(vm));
    var_add(proc, "stdin",  proc_make_stream(vm));

    /* Methods. */
    vm_reg_native_on(vm, proc, "exit(code)",     proc_exit,     NULL);
    vm_reg_native_on(vm, proc, "cwd()",          proc_cwd,      NULL);
    vm_reg_native_on(vm, proc, "nextTick(cb)",   proc_nextTick, NULL);

    var_add(vm->root, "process", proc);
}

void native_Process_set_argv(vm_t* vm) {
    if(vm == NULL || vm->root == NULL) return;
    var_t* proc = var_find_own_member_var(vm->root, "process");
    var_t* src_args = var_find_own_member_var(vm->root, "_args");
    if(proc == NULL || src_args == NULL) return;

    var_t* argv = var_new_array(vm);
    uint32_t n = var_array_size(src_args);
    for(uint32_t i = 0; i < n; ++i) {
        node_t* an = var_array_get(src_args, (int32_t)i);
        var_array_add(argv, (an != NULL && an->var != NULL) ? an->var : var_new(vm));
    }
    var_add(proc, "argv", argv);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
