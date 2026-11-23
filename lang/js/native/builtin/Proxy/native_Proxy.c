#ifdef __cplusplus
extern "C" {
#endif

#include "native_Proxy.h"
#include <string.h>

#define CLS_PROXY "Proxy"

/* A proxy is an ordinary V_OBJECT marked @@exotic="proxy" (EXOTIC_PROXY) with
 * three hidden invisable members installed here: @@ptarget (the target object),
 * @@phandler (the handler object) and @@prevoked (V_BOOL, flipped by revoke()).
 * Every property path in the VM checks var_is_proxy() and routes to the matching
 * proxy_<op> trap in mario.c; this file only constructs and revokes proxies. The
 * target and handler must both be objects (spec): a non-object is a TypeError. */

static var_t* proxy_proto(vm_t* vm) {
	node_t* n = vm_load_node(vm, CLS_PROXY, false);
	return (n != NULL && n->var != NULL) ? var_get_prototype(n->var) : NULL;
}

static bool proxy_check_args(vm_t* vm, var_t* target, var_t* handler) {
	if(target == NULL || target->type != V_OBJECT ||
			handler == NULL || handler->type != V_OBJECT) {
		vm_throw_type_native(vm, "TypeError",
				"Cannot create proxy with a non-object as target or handler");
		return false;
	}
	return true;
}

/* Install the exotic marker and the hidden target/handler/revoked members.
 * var_add refs target and handler, so the proxy owns a reference to each. */
static void proxy_setup(vm_t* vm, var_t* obj, var_t* target, var_t* handler) {
	node_t* mn = var_add(obj, EXOTIC_MARKER, var_new_str(vm, EXOTIC_PROXY));
	if(mn != NULL) {
		mn->invisable = 1; mn->be_unenumerable = 1;
	}
	node_t* tn = var_add(obj, PROXY_TARGET, target);
	if(tn != NULL) {
		tn->invisable = 1; tn->be_unenumerable = 1;
	}
	node_t* hn = var_add(obj, PROXY_HANDLER, handler);
	if(hn != NULL) {
		hn->invisable = 1; hn->be_unenumerable = 1;
	}
	node_t* rn = var_add(obj, PROXY_REVOKED, var_new_bool(vm, false));
	if(rn != NULL) {
		rn->invisable = 1; rn->be_unenumerable = 1;
	}
}

/* new Proxy(target, handler): `this` is the freshly allocated instance (with the
 * Proxy prototype); mark it and return it. On a non-object target/handler the
 * TypeError is thrown and the half-built instance is discarded by func_call. */
var_t* native_Proxy_constructor(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* this_v = get_obj(env, THIS);
	var_t* target = get_obj(env, "target");
	var_t* handler = get_obj(env, "handler");
	if(!proxy_check_args(vm, target, handler))
		return this_v;
	proxy_setup(vm, this_v, target, handler);
	return this_v;
}

/* revoke(): the per-revocable closure registered on the proxy itself (data == the
 * proxy var, mirroring Promise's __resolve/__reject). Flips @@prevoked to true so
 * every subsequent trap on the proxy throws. Idempotent, returns undefined. */
static var_t* native_proxy_revoke_cb(vm_t* vm, var_t* env, void* data) {
	(void)vm; (void)env;
	var_t* p = (var_t*)data;
	if(p != NULL) {
		var_t* r = var_find_own_member_var(p, PROXY_REVOKED);
		if(r != NULL && r->type == V_BOOL && r->value != NULL)
			*((int*)r->value) = 1;
	}
	return NULL;
}

/* Proxy.revocable(target, handler): { proxy, revoke }. The revoke closure is an
 * invisable member of the proxy (so its bare data pointer stays valid while the
 * proxy - hence the returned result - is reachable) and is also exposed as
 * result.revoke. */
var_t* native_Proxy_revocable(vm_t* vm, var_t* env, void* data) {
	(void)data;
	var_t* target = get_obj(env, "target");
	var_t* handler = get_obj(env, "handler");
	var_t* result = var_new_obj(vm, NULL, NULL, NULL);
	if(!proxy_check_args(vm, target, handler))
		return result;

	var_t* p = var_new_obj(vm, proxy_proto(vm), NULL, NULL);
	proxy_setup(vm, p, target, handler);

	node_t* rvn = vm_reg_native_on(vm, p, "__revoke()", native_proxy_revoke_cb, p);
	rvn->invisable = 1;
	rvn->be_unenumerable = 1;

	var_add(result, "proxy", p);
	var_add(result, "revoke", rvn->var);
	return result;
}

void reg_native_Proxy(vm_t* vm) {
	var_t* cls = vm_new_class(vm, CLS_PROXY);
	vm_reg_native(vm, cls, "constructor(target, handler)", native_Proxy_constructor, NULL);
	vm_reg_static(vm, cls, "revocable(target, handler)", native_Proxy_revocable, NULL);
	/* Object.prototype.toString shape; a real proxy routes get to its target, so
	 * this only shows for the Proxy constructor object itself. */
	vm_reg_var(vm, cls, SYMKEY_TOSTRINGTAG, var_new_str(vm, "Proxy"), true);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
