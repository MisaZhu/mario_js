// Regression: Annex B legacy accessors (__defineGetter__/__defineSetter__/
// __lookupGetter__/__lookupSetter__) must exist on Object.prototype and behave
// like the spec. Baidu Maps calls __defineSetter__ on the Event constructor
// during bootstrap; without it mario threw "can not find function
// '__defineSetter__'" and aborted the whole page script (the map never drew).
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}

// --- basic getter / setter -------------------------------------------------
var o = {};
o.__defineGetter__("x", function () { return 42; });
eq(o.x, 42, "defineGetter installs a working getter");

o.__defineSetter__("y", function (v) { this._y = v * 2; });
o.y = 5;
eq(o._y, 10, "defineSetter installs a working setter");

// --- merge: a setter must not drop a pre-existing getter -------------------
o.__defineSetter__("x", function (v) { this._x = v; });
o.x = 7;
eq(o._x, 7, "defineSetter on getter-only prop installs the setter");
eq(o.x, 42, "defineSetter preserves the pre-existing getter");

// --- lookup ----------------------------------------------------------------
eq(typeof o.__lookupGetter__("x"), "function", "lookupGetter returns the getter");
eq(typeof o.__lookupSetter__("x"), "function", "lookupSetter returns the merged setter");
eq(typeof o.__lookupSetter__("y"), "function", "lookupSetter returns a setter");
eq(typeof o.__lookupGetter__("nope"), "undefined", "lookupGetter of absent key is undefined");

// lookup walks the prototype chain
var proto = {};
proto.__defineGetter__("g", function () { return 1; });
var child = Object.create(proto);
eq(typeof child.__lookupGetter__("g"), "function", "lookupGetter walks the proto chain");

// --- inherited on constructors (the Baidu Maps / Event case) ---------------
function E() {}
E.__defineSetter__("foo", function (v) { this._foo = v; });
eq(typeof E.__lookupSetter__("foo"), "function", "ctor inherits __defineSetter__");
eq(typeof Object.prototype.__defineGetter__, "function", "__defineGetter__ on Object.prototype");
eq(typeof Object.prototype.__defineSetter__, "function", "__defineSetter__ on Object.prototype");

// --- return value is undefined per spec ------------------------------------
var r = {};
eq(r.__defineGetter__("z", function () { return 1; }), undefined, "defineGetter returns undefined");

console.log(failed === 0 ? "__DEFACC_OK__" : ("__DEFACC_FAIL__ " + failed));
console.log("legacy accessor regression: " + passed + " passed, " + failed + " failed");
