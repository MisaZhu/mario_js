// Regression: a member installed on Function.prototype by JS code must resolve on
// a plain function/constructor. Baidu Maps defines `Function.prototype.inherits`
// then calls `SomeCtor.inherits(...)`; mario conflates a callable's `.prototype`
// (instance template) with [[Prototype]], so the member walk missed
// Function.prototype's own members -> "can not find function 'inherits'".
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}

Function.prototype.inherits = function (t, e) { return "inh:" + e; };
function Klass() {}
eq(typeof Klass.inherits, "function", "named get resolves Function.prototype.inherits");
eq(Klass.inherits(null, "Map"), "inh:Map", "call site-installed Function.prototype method");
eq(typeof Klass["inherits"], "function", "computed get resolves it too");

// still resolves the universal methods
eq(typeof Klass.call, "function", "call still resolves");
eq(typeof Klass.apply, "function", "apply still resolves");
eq(typeof Klass.bind, "function", "bind still resolves");

// per-function dynamic values must NOT be shadowed by Function.prototype's own
function Named3(a, b, c) {}
eq(Named3.length, 3, "length stays the declared arity, not Function.prototype.length");
eq(typeof Named3.name, "string", "name stays a string via do_get, not the tail");

// non-function objects are unaffected
var o = {};
eq(typeof o.inherits, "undefined", "plain object does not see Function.prototype members");

// a data (non-function) member on Function.prototype also resolves
Function.prototype.marker = 123;
function K2() {}
eq(K2.marker, 123, "data member on Function.prototype resolves on a function");

console.log(failed === 0 ? "__FNPROTO_OK__" : ("__FNPROTO_FAIL__ " + failed));
console.log("Function.prototype member lookup: " + passed + " passed, " + failed + " failed");
