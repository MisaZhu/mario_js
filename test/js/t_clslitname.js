// Regression: ES PropertyName allows string/numeric literals as class member
// names (`"~validate"(e){}`, `0(){}`, `get "~x"(){}`). zod's "~standard"
// protocol ships exactly this shape; mario used to fail the whole chunk with
// "lex got 'STRING' expected '('" (amap.com/ssr/search hydration died there,
// so the AMap loader script was never injected and the map never drew).
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}

// --- string-literal method names -------------------------------------------
class Z {
    "~validate"(e) { return e + 1; }
    "~standard"() { return "std"; }
    "two words"(e) { return e * 2; }
}
var z = new Z();
eq(z["~validate"](1), 2, "string-named method callable");
eq(z["~standard"](), "std", "second string-named method");
eq(z["two words"](3), 6, "string name with spaces");
eq(typeof z["~validate"], "function", "string-named member is a function");

// --- numeric-literal method names canonicalise (ToPropertyKey) --------------
class N {
    0() { return "zero"; }
    1.5() { return "float"; }
    0x10() { return "hex"; }
    529e3() { return "sci"; }
}
var n = new N();
eq(n[0](), "zero", "int method name");
eq(n["1.5"](), "float", "float method name");
eq(n[16](), "hex", "hex method name canonicalises to 16");
eq(n["529000"](), "sci", "scientific method name canonicalises");
eq(n["0x10"], undefined, "raw hex text is not a member key");

// --- string/numeric names on accessors --------------------------------------
class A {
    get "~x"() { return this._x || 7; }
    set "~x"(v) { this._x = v; }
    get 0x10() { return 16; }
}
var a = new A();
eq(a["~x"], 7, "string-named getter");
a["~x"] = 9;
eq(a["~x"], 9, "string-named setter");
eq(a[16], 16, "numeric-named getter canonicalises");

// --- modifiers combine with literal names -----------------------------------
class M {
    static "~s"() { return "static"; }
    async "~a"() { return "async"; }
    *"~g"() { yield 1; yield 2; }
}
eq(M["~s"](), "static", "static string-named method");
eq(typeof M["~a"]().then, "function", "async string-named method returns promise");
var it = new M()["~g"]();
eq(it.next().value, 1, "generator string-named method yields");
eq(it.next().value, 2, "generator second yield");

// --- methods literally named get/set stay plain methods ---------------------
class G {
    get(e) { return "get" + e; }
    set(e) { return "set" + e; }
}
var g = new G();
eq(g.get(1), "get1", "method named get is not an accessor");
eq(g.set(2), "set2", "method named set is not an accessor");

// --- reserved-word method names still work ----------------------------------
class R {
    default(e) { return e + 2; }
    catch(e) { return e + 3; }
}
var r = new R();
eq(r.default(0), 2, "reserved-word method default");
eq(r.catch(0), 3, "reserved-word method catch");

// --- inheritance through a string-named method ------------------------------
class B { "~m"() { return 1; } }
class D extends B { "~m"() { return super["~m"]() + 10; } }
eq(new D()["~m"](), 11, "super call on string-named method");

// --- object-literal string/numeric methods unchanged ------------------------
var o = { "~k"() { return 5; }, 0x10() { return 16; } };
eq(o["~k"](), 5, "object string-named method");
eq(o[16](), 16, "object numeric-named method");

console.log(failed === 0 ? "__CLSLITNAME_OK__" : ("__CLSLITNAME_FAIL__ " + failed));
console.log("class literal member name regression: " + passed + " passed, " + failed + " failed");