// Regression: C.prototype.constructor === C for NATIVE builtin classes. The
// prototype's own "constructor" is the native ctor FUNCTION (vm_reg_native),
// a distinct var from the class wrapper bound globally as C, so
// `({}).constructor === Object` read false and AMap's worker-RPC serialize()
// (`t.constructor.yk` after `register("Object", Object)` did
// defineProperty(Object,"yk",...)) threw "can't serialize object of
// unregistered class" on every plain object / Error - `new AMap.Map()` died
// before drawing anything on amap.com/ssr/search. Fix: vm_new_class tags the
// template with @@proto_owner and do_get synthesizes the class var at READ
// time only (own-member probes / @@ctor dispatch unchanged).
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}

// --- plain objects / Object ------------------------------------------------
eq(({}).constructor === Object, true, "({}).constructor === Object");
eq(Object.prototype.constructor === Object, true, "Object.prototype.constructor === Object");
eq(({}).constructor.prototype === Object.prototype, true, "({}).constructor.prototype === Object.prototype");
eq(({}).constructor.name, "Object", "({}).constructor.name");
eq(Object.create(null).constructor, undefined, "null-proto object has no constructor");

// --- Error / Array / Date / RegExp ------------------------------------------
eq(new Error("x").constructor === Error, true, "new Error().constructor === Error");
eq(Error.prototype.constructor === Error, true, "Error.prototype.constructor === Error");
eq([].constructor === Array, true, "[].constructor === Array");
eq(Array.prototype.constructor === Array, true, "Array.prototype.constructor === Array");
eq(new Date().constructor === Date, true, "new Date().constructor === Date");
eq(/x/.constructor === RegExp, true, "/x/.constructor === RegExp");

// --- AMap register/serialize replica ---------------------------------------
Object.defineProperty(Object, "yk", { value: "Object", writable: false });
Object.defineProperty(Error, "yk", { value: "Error", writable: false });
eq(({}).constructor.yk, "Object", "serialize reads Object.yk via ({}).constructor");
eq(new Error("e").constructor.yk, "Error", "serialize reads Error.yk via instance");
var registry = { Object: Object, Error: Error };
eq(registry[({}).constructor.yk] === Object, true, "registry lookup by constructor.yk");

// --- plain-function path unchanged ------------------------------------------
function F() {}
eq(new F().constructor === F, true, "new F().constructor === F");
eq(F.prototype.constructor === F, true, "F.prototype.constructor === F");
function G() {}
G.prototype = { marker: 1 };
eq(new G().constructor === Object, true, "replaced F.prototype falls back to Object");
eq(new G().marker, 1, "replaced prototype members still inherited");

// --- script classes keep their existing semantics ---------------------------
// (explicit constructors: mario installs prototype.constructor only for those;
// implicit-ctor classes falling through to Object is a pre-existing gap, unchanged here)
class A { constructor() { this.x = 1; } m() { return 7; } }
var a = new A();
eq(a.constructor.prototype === A.prototype, true, "script class inst.constructor.prototype");
eq(a.constructor.prototype.m() , 7, "class methods reachable via inst.constructor.prototype");
class B extends A { constructor() { super(); } }
eq(new B().constructor.prototype === B.prototype, true, "derived class inst.constructor.prototype");
eq(new B().x, 1, "derived construction still runs the base ctor");

// --- script overwrite of prototype.constructor wins over synthesis ----------
var saved = Object.getOwnPropertyDescriptor(RegExp.prototype, "constructor");
function FakeCtor() {}
RegExp.prototype.constructor = FakeCtor;
eq(/x/.constructor === FakeCtor, true, "overwritten prototype.constructor wins");
Object.defineProperty(RegExp.prototype, "constructor", saved);
eq(/x/.constructor === RegExp, true, "restored prototype.constructor synthesizes again");

// --- hidden tag must not leak ------------------------------------------------
var keys = [];
for (var k in {}) keys.push(k);
eq(keys.length, 0, "for-in over {} sees no hidden tag");
eq(Object.prototype.hasOwnProperty.call(Object.prototype, "@@proto_owner"), false,
   "@@proto_owner is not an own enumerable key");
eq(Object.keys(Object.prototype).indexOf("@@proto_owner"), -1, "Object.keys hides the tag");

// --- calling through the synthesized constructor ----------------------------
eq(typeof ({}).constructor(), "object", "({}).constructor() constructs like Object()");
eq(({}).constructor.call(null) !== undefined, true, "constructor.call routes like Object.call");

console.log("passed=" + passed + " failed=" + failed);
if (failed === 0) console.log("__CTORID_OK__");