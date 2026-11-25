// Regression: Boolean wrapper objects (`new Boolean(false)`,
// Reflect.construct(Boolean, [], fn)) must carry the boxed value. mario's
// Boolean was a plain coercion function, so every wrapper's valueOf() read
// back true (var_get_bool of an object). babel's _isNativeReflectConstruct
// probe `!Boolean.prototype.valueOf.call(Reflect.construct(Boolean,[],fn))`
// then evaluated false, _createSuper fell back to Super.apply(this, arguments)
// and threw "target is not callable" on every ES-class super() call -
// amap.com/ssr/search hydration died there (tinycolor Hsb _inherits wrapper).
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}

// --- new Boolean(x): boxed value, object type -------------------------------
eq(typeof new Boolean(false), "object", "wrapper is an object");
eq(new Boolean().valueOf(), false, "no-arg wrapper is false");
eq(new Boolean(false).valueOf(), false, "new Boolean(false)");
eq(new Boolean(true).valueOf(), true, "new Boolean(true)");
eq(new Boolean(0).valueOf(), false, "ToBoolean(0)");
eq(new Boolean("").valueOf(), false, "ToBoolean('')");
eq(new Boolean(null).valueOf(), false, "ToBoolean(null)");
eq(new Boolean(undefined).valueOf(), false, "ToBoolean(undefined)");
eq(new Boolean("x").valueOf(), true, "ToBoolean('x')");
eq(new Boolean(1).valueOf(), true, "ToBoolean(1)");
eq(new Boolean({}).valueOf(), true, "ToBoolean({})");
eq(new Boolean(false).toString(), "false", "wrapper toString");
eq(new Boolean(true).toString(), "true", "wrapper toString true");
eq(new Boolean(false) instanceof Boolean, true, "instanceof Boolean");
eq(Object.prototype.toString.call(new Boolean(false)), "[object Boolean]", "classof tag");
eq(Object.keys(new Boolean(false)).length, 0, "boxed slot is hidden");

// --- plain Boolean(x) stays a primitive coercion ----------------------------
eq(Boolean(0), false, "plain call false");
eq(Boolean("x"), true, "plain call true");
eq(typeof Boolean(1), "boolean", "plain call returns primitive");

// --- primitives keep their prototype methods --------------------------------
eq(false.toString(), "false", "primitive false.toString");
eq(true.valueOf(), true, "primitive true.valueOf");
eq((false).toLocaleString(), "false", "primitive toLocaleString");

// --- Reflect.construct with a custom newTarget (the babel probe) ------------
var b = Reflect.construct(Boolean, [], function(){});
eq(typeof b, "object", "rc wrapper is an object");
eq(Boolean.prototype.valueOf.call(b), false, "rc no-arg valueOf is false");
eq(!Boolean.prototype.valueOf.call(Reflect.construct(Boolean, [], function(){})),
   true, "babel _isNativeReflectConstruct probe");
eq(Boolean.prototype.valueOf.call(Reflect.construct(Boolean, [true], function(){})),
   true, "rc [true] valueOf");
eq(Boolean.prototype.valueOf.call(Reflect.construct(Boolean, [false])),
   false, "rc default newTarget");

// --- _createSuper shape: derived super() via Reflect.construct --------------
function isNativeReflectConstruct() {
    try {
        var e = !Boolean.prototype.valueOf.call(Reflect.construct(Boolean, [], function(){}));
    } catch (err) {}
    return function() { return !!e; };
}
eq(isNativeReflectConstruct()(), true, "probe helper returns true");
class Base { constructor(v) { this.v = v; } }
class Derived extends Base { constructor() { super(7); } }
eq(new Derived().v, 7, "class super() still works");
function NT() {}
var inst = Reflect.construct(Base, [9], NT);
eq(inst.v, 9, "Reflect.construct class with newTarget runs ctor");
eq(inst instanceof NT, true, "newTarget prototype reparenting");
eq(Object.getPrototypeOf(inst) === NT.prototype, true, "instance proto is newTarget.prototype");

console.log("passed=" + passed + " failed=" + failed);
if (failed === 0) console.log("__BOOLWRAP_OK__");
else console.log("__BOOLWRAP_FAIL__");