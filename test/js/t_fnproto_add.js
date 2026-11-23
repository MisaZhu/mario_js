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

// Function.prototype precedes Object.prototype for callable static lookup, while
// class-owned statics and the function's own prototype members still win first.
var nativeApply = Function.prototype.apply;
Object.prototype.apply = function () {};
Object.prototype.marker = 456;
function ApplyBase() {
    this._config = { enabled: this.constructor.Default.enableItemValueList };
}
ApplyBase.prototype.apply = function () {};
function ApplySub() {
    return ApplyBase.apply(this, arguments);
}
ApplySub.prototype = Object.create(ApplyBase.prototype, {
    constructor: { value: ApplySub, writable: true, configurable: true }
});
Object.setPrototypeOf(ApplySub, ApplyBase);
Object.defineProperty(ApplySub, "Default", {
    get: function () { return { enableItemValueList: ["item", "tmall"] }; }
});
eq(ApplyBase.apply, nativeApply, "Object.prototype.apply does not shadow Function.prototype.apply");
eq(ApplyBase["apply"], nativeApply, "computed lookup uses the same Function-before-Object order");
var applyValue = new ApplySub();
eq(applyValue._config.enabled.join(","), "item,tmall", "Babel super fallback executes the base initializer");
eq(K2.marker, 123, "Function.prototype data member also precedes Object.prototype");
class StaticApply { static apply() { return "class-own"; } }
eq(StaticApply.apply(), "class-own", "class-owned static apply still wins over Function.prototype");
delete Object.prototype.apply;
delete Object.prototype.marker;

console.log(failed === 0 ? "__FNPROTO_OK__" : ("__FNPROTO_FAIL__ " + failed));
console.log("Function.prototype member lookup: " + passed + " passed, " + failed + " failed");
