// Regression: namespace objects (Math/JSON/Reflect/Atomics/Intl) are non-callable,
// non-constructible bags of static methods. `new Math()` must throw a catchable
// TypeError instead of silently building an empty object via the inherited
// Object.prototype.constructor. vm_new_namespace clears is_class so
// var_is_constructor() rejects these; new_obj_with_ctor()'s `new` path must
// actually consult var_is_constructor (it used to test only type===V_OBJECT).
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}
function didThrow(fn) { try { fn(); return false; } catch (e) { return true; } }

// namespace objects: `new X()` throws; typeof is "object"; statics still work
eq(didThrow(function () { new Math(); }), true, "new Math() throws");
eq(didThrow(function () { new JSON(); }), true, "new JSON() throws");
eq(didThrow(function () { new Reflect(); }), true, "new Reflect() throws");
eq(typeof Math, "object", "typeof Math");
eq(typeof JSON, "object", "typeof JSON");
eq(typeof Reflect, "object", "typeof Reflect");
eq(Math.sqrt(9), 3, "Math.sqrt static works");
eq(JSON.stringify({ a: 1 }), '{"a":1}', "JSON.stringify static works");

// real constructors are unaffected
eq(didThrow(function () { new Date(); }), false, "new Date() ok");
eq(didThrow(function () { new Array(3); }), false, "new Array() ok");
eq(new Array(3).length, 3, "new Array(3).length");
eq(didThrow(function () { new Map(); }), false, "new Map() ok");
eq(didThrow(function () { new Set(); }), false, "new Set() ok");
eq(didThrow(function () { new Error("x"); }), false, "new Error() ok");
function F() {}
eq(didThrow(function () { new F(); }), false, "new function ok");
class C {}
eq(didThrow(function () { new C(); }), false, "new script-class ok");

// a plain object literal has no [[Construct]] either
var plainObj = {};
eq(didThrow(function () { new plainObj(); }), true, "new plainObj throws");

console.log(failed === 0 ? "__NS_CTOR_OK__" : ("__NS_CTOR_FAIL__ " + failed));
console.log("namespace ctor regression: " + passed + " passed, " + failed + " failed");
if (failed > 0) throw new Error("t_ns_ctor: " + failed + " failed");
