#ifdef __cplusplus
extern "C" {
#endif

#include "native_promise.h"

#define CLS_PROMISE "Promise"

#define PROMISE_STATE_PENDING 0
#define PROMISE_STATE_FULFILLED 1
#define PROMISE_STATE_REJECTED 2

typedef struct {
    int state;
    var_t* value;
    var_t* fulfilled_callbacks;
    var_t* rejected_callbacks;
} promise_data;

static void promise_free(void* p) {
    promise_data* pd = (promise_data*)p;
    if (pd->value) {
        var_unref(pd->value);
    }
    if (pd->fulfilled_callbacks) {
        var_unref(pd->fulfilled_callbacks);
    }
    if (pd->rejected_callbacks) {
        var_unref(pd->rejected_callbacks);
    }
    mario_free(pd);
}

var_t* native_PromiseConstructor(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* thisV = get_obj(env, THIS);
    var_t* executor = get_obj(env, "executor");

    promise_data* pd = (promise_data*)mario_malloc(sizeof(promise_data));
    pd->state = PROMISE_STATE_PENDING;
    pd->value = NULL;
    pd->fulfilled_callbacks = var_new_array(vm);
    pd->rejected_callbacks = var_new_array(vm);

    var_t* obj = var_new_obj_no_proto(vm, pd, promise_free);
    var_instance_from(obj, thisV);

    var_set_prototype(obj, thisV);

    if (executor != NULL) {
        var_t* resolve_args = var_new_array(vm);
        var_array_add(resolve_args, var_new_str(vm, "resolve"));
        var_array_add(resolve_args, var_new_str(vm, "reject"));

        call_m_func(vm, obj, executor, resolve_args);
    }

    return obj;
}

var_t* native_PromiseResolve(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* value = get_obj(env, "value");

    promise_data* pd = (promise_data*)mario_malloc(sizeof(promise_data));
    pd->state = PROMISE_STATE_FULFILLED;
    pd->value = var_ref(value);
    pd->fulfilled_callbacks = var_new_array(vm);
    pd->rejected_callbacks = var_new_array(vm);

    var_t* promise = var_new_obj(vm, get_obj(env, "proto"), pd, promise_free);

    return promise;
}

var_t* native_PromiseReject(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* reason = get_obj(env, "reason");

    promise_data* pd = (promise_data*)mario_malloc(sizeof(promise_data));
    pd->state = PROMISE_STATE_REJECTED;
    pd->value = var_ref(reason);
    pd->fulfilled_callbacks = var_new_array(vm);
    pd->rejected_callbacks = var_new_array(vm);

    var_t* promise = var_new_obj(vm, get_obj(env, "proto"), pd, promise_free);

    return promise;
}

var_t* native_PromiseThen(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promise = get_obj(env, THIS);
    var_t* onFulfilled = get_obj(env, "onFulfilled");
    var_t* onRejected = get_obj(env, "onRejected");

    promise_data* pd = (promise_data*)promise->value;

    if (pd == NULL) {
        pd = (promise_data*)mario_malloc(sizeof(promise_data));
        pd->state = PROMISE_STATE_PENDING;
        pd->value = NULL;
        pd->fulfilled_callbacks = var_new_array(vm);
        pd->rejected_callbacks = var_new_array(vm);
    }

    if (pd->state == PROMISE_STATE_FULFILLED && onFulfilled != NULL) {
        var_t* args = var_new_array(vm);
        var_array_add(args, pd->value);
        var_t* result = call_m_func(vm, promise, onFulfilled, args);
        return native_PromiseResolve(vm, env, NULL);
    }

    if (pd->state == PROMISE_STATE_REJECTED && onRejected != NULL) {
        var_t* args = var_new_array(vm);
        var_array_add(args, pd->value);
        var_t* result = call_m_func(vm, promise, onRejected, args);
        return native_PromiseResolve(vm, env, NULL);
    }

    if (pd->state == PROMISE_STATE_PENDING) {
        if (onFulfilled != NULL) {
            var_array_add(pd->fulfilled_callbacks, onFulfilled);
        }
        if (onRejected != NULL) {
            var_array_add(pd->rejected_callbacks, onRejected);
        }
    }

    promise_data* newPd = (promise_data*)mario_malloc(sizeof(promise_data));
    newPd->state = pd->state;
    newPd->value = pd->value ? var_ref(pd->value) : NULL;
    newPd->fulfilled_callbacks = var_new_array(vm);
    newPd->rejected_callbacks = var_new_array(vm);

    var_t* newPromise = var_new_obj(vm, get_obj(env, "proto"), newPd, promise_free);

    return newPromise;
}

var_t* native_PromiseCatch(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* onRejected = get_obj(env, "onRejected");

    return native_PromiseThen(vm, env, NULL);
}

var_t* native_PromiseFinally(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promise = get_obj(env, THIS);
    var_t* onFinally = get_obj(env, "onFinally");

    return native_PromiseThen(vm, env, NULL);
}

var_t* native_PromiseAll(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promises = get_obj(env, "promises");

    var_t* result = var_new_obj(vm, get_obj(env, "proto"), NULL, NULL);
    return result;
}

var_t* native_PromiseRace(vm_t* vm, var_t* env, void* data) {
    (void)data;
    var_t* promises = get_obj(env, "promises");

    var_t* result = var_new_obj(vm, get_obj(env, "proto"), NULL, NULL);
    return result;
}

void reg_native_promise(vm_t* vm) {
    var_t* cls = vm_new_class(vm, CLS_PROMISE);
    vm_reg_native(vm, cls, "constructor(executor)", native_PromiseConstructor, NULL);
    vm_reg_static(vm, cls, "resolve(value)", native_PromiseResolve, NULL);
    vm_reg_static(vm, cls, "reject(reason)", native_PromiseReject, NULL);
    vm_reg_static(vm, cls, "all(promises)", native_PromiseAll, NULL);
    vm_reg_static(vm, cls, "race(promises)", native_PromiseRace, NULL);
    vm_reg_native(vm, cls, "then(onFulfilled, onRejected)", native_PromiseThen, NULL);
    vm_reg_native(vm, cls, "catch(onRejected)", native_PromiseCatch, NULL);
    vm_reg_native(vm, cls, "finally(onFinally)", native_PromiseFinally, NULL);
}

#ifdef __cplusplus
}
#endif
