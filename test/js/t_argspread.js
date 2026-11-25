// Regression: `...arguments` must mark the frame for INSTR_FUNCARGU. The
// prescan treated the spread's third '.' as a member-access dot and skipped
// the mark, so the arguments array was never materialized when the FIRST
// reference was a spread: zod's `constructor(){ super(...arguments) }` ran
// the base ctor with zero args (this._def undefined -> "reading 'checks'"),
// killing amap.com/ssr/search hydration at the error boundary.
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}

// --- spread as the FIRST arguments reference (plain function) --------------
function h(a, b) { return a; }
function g() { return h(...arguments); }
eq(g({ y: 3 }).y, 3, "call spread of arguments, first reference");
function g2() { return [...arguments]; }
eq(g2(1, 2).length, 2, "array spread of arguments, first reference");
function g3() { var a = [...arguments]; return a.length; }
eq(g3(1, 2, 3), 3, "var initializer spread of arguments");
function g4() { let a = [...arguments]; return a.length; }
eq(g4(1, 2), 2, "let initializer spread of arguments");

// --- class ctor: super(...arguments) as the first reference ----------------
class A { constructor(d) { this._def = d; } }
class B extends A { constructor() { super(...arguments); this.min = 1; } }
var b = new B({ checks: [7] });
eq(b._def && b._def.checks[0], 7, "super(...arguments) first reference");
eq(b.min, 1, "derived ctor body still runs");
class C extends A { constructor() { var a = [...arguments]; super(a[0]); } }
var c = new C({ checks: [8] });
eq(c._def && c._def.checks[0], 8, "ctor array spread then super");
class D extends A { constructor() { super(...arguments); } }
eq(new D({ checks: [9] })._def.checks[0], 9, "bare super spread ctor");

// --- member access must NOT be confused with spread ------------------------
var obj = { arguments: 42 };
function m() { return obj.arguments; }
eq(m(), 42, "obj.arguments member read");
function m2() { return obj?.arguments; }
eq(m2(), 42, "optional-chain member read");

// --- arrow inherits the enclosing frame's arguments ------------------------
function ar() { var f = () => [...arguments]; return f(); }
eq(ar(5, 6).length, 2, "arrow block body spread of outer arguments");
function ar2() { var f = () => h(...arguments); return f(); }
eq(ar2({ z: 1 }).z, 1, "arrow call spread of outer arguments");

// --- mixed: direct use before spread still fine ----------------------------
function mix() { var n = arguments.length; return h(...arguments) ? n : -1; }
eq(mix({}), 1, "length then spread");

console.log("passed=" + passed + " failed=" + failed);
if (failed === 0) console.log("__ARGSPREAD_OK__");
else console.log("__ARGSPREAD_FAIL__");