// Regression: a fused member call `proxy.method(args)` must route the Proxy `get`
// trap to resolve the method, exactly like a plain `proxy.method` read does.
//
// Root cause this guards: find_func() (the INSTR_CALLO dispatch) walked the raw
// own/prototype member chain and never invoked the get trap, so a method that only
// existed behind the trap mis-resolved to "can not find function 'method'". This was
// fatal on github.com: react-router wraps its router object in a Proxy and
// `e.subscribe(b)` inside RouterProvider's useLayoutEffect threw, bubbling to HostRoot
// as an uncaught error that installed the element=null root recovery update and
// unmounted the whole tree (commit columns stayed grey skeletons forever).
//
// Second bug this guards: the get-trap result is a BASELINE var (no owned ref). The
// fix must var_ref() it before the call and var_unref() after; without the var_ref the
// post-call unref underflowed and freed the target's method after the FIRST call, so
// every later proxy.method() read dangling/recycled memory. The repeated-call asserts
// below catch that regression.
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}

var calls = 0;
var target = {
    subscribe: function (fn) { calls++; return "sub:" + typeof fn; },
    enableScrollRestoration: function (v) { return "esr:" + !!v; }
};
var p = new Proxy(target, { get: function (t, k, r) { return Reflect.get(t, k, r); } });
var b = function () {};

// 1. direct fused call routes the get trap
eq(p.subscribe(b), "sub:function", "proxy.method(arg) invokes through get trap");
// 2. a SECOND call on the same proxy still works (refcount-underflow guard)
eq(p.subscribe(b), "sub:function", "repeated proxy.method(arg) stable");
eq(p.enableScrollRestoration(true), "esr:true", "second proxy method resolves");
eq(calls, 2, "target method actually invoked each time");

// 3. deferred closure capturing the proxy (the exact react-router shape:
//    `useLayoutEffect(() => e.subscribe(b), [e, b])`)
var deferred = (function (e) { return function () { return e.subscribe(b); }; })(p);
eq(deferred(), "sub:function", "deferred closure over proxy");
eq(deferred(), "sub:function", "deferred closure over proxy (repeat)");

// 4. immediate concise + block arrows
eq((() => p.subscribe(b))(), "sub:function", "immediate concise arrow");
eq((() => { return p.subscribe(b); })(), "sub:function", "immediate block arrow");

// 5. proxy aliased to a local, then called
var loc = p;
eq(loc.subscribe(b), "sub:function", "local alias to proxy");

// 6. spread-argument fused call: proxy.method(...args)
eq(p.subscribe(...[b]), "sub:function", "proxy.method(...args) spread call");

// 7. read still works alongside calls (typeof / extract)
eq(typeof p.subscribe, "function", "typeof proxy.method still function");
var f = p.subscribe;
eq(f.call(p, b), "sub:function", "extracted method .call");

// 8. a method that does NOT exist behind the trap must still throw (not silently pass)
var threw = false;
try { p.nope(1); } catch (e) { threw = true; }
eq(threw, true, "missing proxy method still throws");

console.log(failed === 0 ? "__PROXY_METHOD_CALL_OK__" : ("__PROXY_METHOD_CALL_FAIL__ " + failed));
console.log("proxy method-call regression: " + passed + " passed, " + failed + " failed");
