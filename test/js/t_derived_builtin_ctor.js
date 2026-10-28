// Regression: derived classes of a builtin base (Date) must adopt the object the
// base constructor builds. ES replaces the derived constructor's `this` with the
// value super() returns and a constructor falling off the end returns `this`, so
// `new (class extends Date){}(iso)` must carry the timestamp slot. A previous
// engine kept the bare pre-super placeholder: every formatter printed
// "Invalid Date" (github's code-view date column) and DOM subclasses lost their
// prototype methods.
var pass = 0, fail = 0;
function ok(cond, msg) {
    if (cond) { pass++; }
    else { fail++; console.log("FAIL: " + msg); }
}

var ISO = "2026-09-05T19:14:39.000+08:00";
var EXPECT = 1788606879000;

// implicit default constructor
var D = class extends Date {};
var d = new D(ISO);
ok(d.getTime() === EXPECT, "implicit ctor getTime");
ok(d.toString().indexOf("Invalid") < 0, "implicit ctor toString not Invalid Date");
ok(d instanceof Date, "implicit ctor instanceof Date");
ok(d.toISOString() === "2026-09-05T11:14:39.000Z", "implicit ctor toISOString");

// explicit constructor calling super()
var E = class extends Date {
    constructor(s) { super(s); }
};
var e = new E(ISO);
ok(e.getTime() === EXPECT, "explicit ctor getTime");
ok(e.toString().indexOf("Invalid") < 0, "explicit ctor toString not Invalid Date");

// fields assigned after super() land on the adopted instance
var F = class extends Date {
    constructor(s) { super(s); this.tag = "x"; }
};
var f = new F(ISO);
ok(f.getTime() === EXPECT, "post-super field: getTime");
ok(f.tag === "x", "post-super field visible on result");

// `this` inside the body is already the adopted instance
var seen = null;
var G = class extends Date {
    constructor(s) { super(s); seen = this.getTime(); }
};
new G(ISO);
ok(seen === EXPECT, "this rebound inside constructor body");

// methods on the derived prototype see the timestamp
var H = class extends Date {
    ms() { return this.getTime(); }
};
ok(new H(ISO).ms() === EXPECT, "derived prototype method sees @@t");

// a plain (non-derived) class still returns undefined implicitly -> fresh object
var P = class { constructor() { this.a = 1; } };
ok(new P().a === 1, "plain class unaffected");

// bare `return;` in a derived ctor still yields the adopted instance
var R = class extends Date {
    constructor(s) { super(s); return; }
};
ok(new R(ISO).getTime() === EXPECT, "bare return in derived ctor");

// non-constructor functions keep the undefined implicit return
function plain() { }
ok(plain() === undefined, "plain function implicit return undefined");

console.log("derived-builtin ctor regression: " + pass + " passed, " + fail + " failed");
if (fail === 0) console.log("__DERIVED_CTOR_OK__");
