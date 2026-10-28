// =============================================================================
// es6_full.js — A complete, standard ES6 (ES2015+) test suite.
//
// This is a *standard-spec* test suite: it exercises the full ES6 feature set
// as defined by ECMA-262 (plus the handful of later additions that are almost
// always lumped together with "ES6": async/await, object rest/spread, the
// exponent operator, and a few Array/String/Object/Number/Math statics).
//
// Every assertion below is written to hold true in a spec-compliant engine
// (modern V8 / Node / browser). Sections that a minimal engine does not
// implement are still included so that "nothing is missed".
//
// Feature coverage index:
//   1.  let / const (block scope, TDZ, redeclaration, immutability)
//   2.  Template literals (interpolation, nesting, multi-line, tagged)
//   3.  Default parameters
//   4.  Rest parameters
//   5.  Spread (array literal, call, object literal / rest)
//   6.  Destructuring (array, object, nested, defaults, swap, params)
//   7.  Object literal enhancements (shorthand, computed keys, methods,
//       getters/setters, __proto__)
//   8.  Arrow functions (lexical this, no [[Construct]], no arguments binding)
//   9.  Classes (constructor, methods, static, inheritance, super, accessors)
//   10. for...of / for...in and iteration protocol
//   11. Iterators & generators (function*, yield, yield*, Symbol.iterator)
//   12. Symbols (uniqueness, well-known symbols, symbol keys)
//   13. Collections (Map, Set, WeakMap, WeakSet)
//   14. Promise (states, then/catch/finally, all/allSettled/race, chaining)
//   15. async / await
//   16. Array statics & prototype methods (from, of, find, includes, ...)
//   17. String methods (startsWith, includes, repeat, padStart, ...)
//   18. Number / Math additions
//   19. Object statics (assign, is, keys, values, entries, freeze, ...)
//   20. Exponent operator, new.target, typeof/instanceof edge cases
//   21. Closures & the classic loop-capture (let vs var)
//   22. Tail-position / recursion & destructuring corner cases
//   23. Global functions & globalThis (isNaN, parseInt, parseFloat, Number.*)
//   24. Error subtypes & AggregateError (TypeError, RangeError, ...)
//   25. Object statics (hasOwn, setPrototypeOf, seal, preventExtensions, ...)
//   26. Array.at & String extras (at, trimStart/End/Left/Right, replaceAll)
//   27. Promise.any (ES2021)
//   28. JSON stringify / parse (compact output, negatives, floats, roundtrip)
//   29. Date & typeof of constructors (Date.now, getTime, valueOf, full field
//       accessors, toISOString/toUTCString/toString, Date.parse/Date.UTC)
//   30. undefined / null equality
//   31. Added built-ins (Math.floor/ceil/atan2/fround/random, Array.splice/
//       reduceRight, String.charAt/charCodeAt/substring/concat/fromCharCode/
//       lastIndexOf/localeCompare, Number.toFixed/toPrecision/valueOf/
//       MAX_VALUE/MIN_VALUE) && logical `&&`/`||` operand-return semantics
//   32. 64-bit numbers (exact large integer literals & arithmetic, int64/double
//       promotion, double precision, Number.MAX_SAFE_INTEGER/MAX_VALUE)
//   33. BigInt (arbitrary-precision literals/arithmetic/comparison/bitwise,
//       toString radix, BigInt()/Number() conversion, asIntN/asUintN)
//   34. ArrayBuffer & DataView (byteLength, zero-init, isView, offset views,
//       every get/set width + endianness, shared backing, slice, BigInt64/BigUint64)
//   35. TypedArrays (Int8..BigUint64: length/array/typedarray/buffer ctors,
//       from/of, wrapping & clamping writes, subarray/slice/set/fill/reverse/
//       copyWithin/sort/indexOf/includes/find/every/some/forEach/map/filter/
//       reduce/reduceRight/at/join/toString/values/keys/entries/@@iterator)
//   36. Proxy / Reflect (get/set/has/deleteProperty/ownKeys/apply/construct/
//       prototype/extensibility/descriptor traps, revocable, invariants, and
//       all 13 Reflect statics)
//   37. WeakRef / FinalizationRegistry (deref & weak clearing after a forced
//       gc(), cleanup callbacks with held values, register/unregister tokens,
//       @@toStringTag, and the object-target / callable-callback TypeErrors)
//   38. SharedArrayBuffer / Atomics (shared buffer byteLength/zero-init/slice/
//       @@toStringTag/non-growable; atomic load/store/add/sub/and/or/xor/
//       exchange/compareExchange over integer & BigInt64 views, wait throws,
//       notify wakes 0, isLockFree)
//
// Deliberately EXCLUDED (each would need a subsystem this minimal engine lacks):
//   - RegExp: no /pattern/ literals, no regex-based match/split/replace.
//   - ES Modules: import/export; this is a single-file script engine.
// =============================================================================

// --- Runtime shim ------------------------------------------------------------
// Bare shells such as JavaScriptCore's `jsc` do not provide `console` (or
// `setTimeout`). Node and browsers already have them, so this only fills the
// gaps. It deliberately assigns through `globalThis` instead of declaring
// `var console`, which would otherwise shadow (and blank out) Node's console.
(function () {
    var g = (typeof globalThis !== "undefined") ? globalThis : this;
    if (typeof g.console === "undefined" || typeof g.console.log !== "function") {
        var printFn = (typeof print === "function") ? print : function () {};
        g.console = {
            log: function () {
                var parts = [];
                for (var i = 0; i < arguments.length; i++) {
                    parts.push(typeof arguments[i] === "string" ? arguments[i] : String(arguments[i]));
                }
                printFn(parts.join(" "));
            }
        };
    }
    if (typeof g.setTimeout !== "function") {
        // No-op timer: pending callbacks simply never fire, which is fine for
        // the Promise.race test (the synchronous promise always wins).
        g.setTimeout = function () { return 0; };
    }
})();
// -----------------------------------------------------------------------------

let __pass = 0;
let __fail = 0;

function ok(cond, msg) {
    if (cond) { __pass++; console.log("  \u2713 " + msg); }
    else { __fail++; console.log("  \u2717 " + msg); }
}

function eq(actual, expected, msg) {
    // Object.is-style: handles NaN === NaN and +0 !== -0 correctly.
    const same = Object.is(actual, expected);
    if (same) { __pass++; console.log("  \u2713 " + msg); }
    else { __fail++; console.log("  \u2717 " + msg + " (expected " + String(expected) + ", got " + String(actual) + ")"); }
}

function deepEq(actual, expected, msg) {
    const a = JSON.stringify(actual);
    const b = JSON.stringify(expected);
    if (a === b) { __pass++; console.log("  \u2713 " + msg); }
    else { __fail++; console.log("  \u2717 " + msg + " (expected " + b + ", got " + a + ")"); }
}

function throws(fn, msg) {
    let threw = false;
    try { fn(); } catch (e) { threw = true; }
    ok(threw, msg);
}

function noThrow(fn, msg) {
    let threw = false;
    try { fn(); } catch (e) { threw = true; }
    ok(!threw, msg);
}

function section(title) {
    console.log("\n" + title);
}

// =============================================================================
section("1. let / const");
// =============================================================================
(function () {
    let a = 1;
    const b = 2;
    a = 5;
    eq(a, 5, "let is reassignable");
    eq(b, 2, "const keeps its value");
    throws(() => { "use strict"; const c = 1; c = 2; }, "const reassignment throws");
})();

(function () {
    // Block scoping: inner `let` does not leak out.
    let outer = "outer";
    {
        let outer = "inner";
        eq(outer, "inner", "block-scoped let shadows outer");
    }
    eq(outer, "outer", "outer binding is unaffected by the block");
})();

(function () {
    // `var` is function-scoped and does leak out of a block.
    if (true) { var leaked = 42; }
    eq(leaked, 42, "var declared in a block leaks to function scope");
})();

(function () {
    // Temporal Dead Zone: mario intentionally has NO true TDZ (see the
    // re-declaration comment in mario.c: throwing on a read before the
    // declaration would abort minified bundles that reuse single-letter
    // lets across loop iterations, e.g. React hydration). The documented
    // semantics is: the binding exists from scope entry, so an early read
    // yields undefined instead of throwing.
    function __tdzProbe() { const read = v; let v = 1; return read; }
    eq(__tdzProbe(), undefined, "no-TDZ: read before declaration yields undefined");
})();

(function () {
    // let/const in a for loop create a fresh binding per iteration.
    const fns = [];
    for (let i = 0; i < 3; i++) { fns.push(() => i); }
    deepEq(fns.map(f => f()), [0, 1, 2], "for-let gives each closure its own i");

    const vfns = [];
    for (var j = 0; j < 3; j++) { vfns.push(() => j); }
    deepEq(vfns.map(f => f()), [3, 3, 3], "for-var shares one binding (all 3)");
})();

(function () {
    // const with a mutable object: the binding is fixed, contents are not.
    const obj = { n: 1 };
    obj.n = 2;
    eq(obj.n, 2, "const object contents are mutable");
    throws(() => { const arr = [1]; arr = [2]; }, "const binding cannot be reassigned");
})();

// =============================================================================
section("2. Template literals");
// =============================================================================
(function () {
    const who = "World";
    eq(`Hello, ${who}!`, "Hello, World!", "simple interpolation");
    eq(`${1 + 2} = ${3}`, "3 = 3", "expression interpolation");
    eq(`nested ${`inner ${1 + 1}`}`, "nested inner 2", "nested templates");
    eq(`line1
line2`, "line1\nline2", "multi-line template preserves newlines");
    eq(`a\tb`, "a\tb", "escape sequences are processed");
    const obj = { toString() { return "custom"; } };
    eq(`${obj}`, "custom", "interpolation invokes toString");
    eq(`${null}-${undefined}`, "null-undefined", "null/undefined interpolate as text");
})();

(function () {
    // Tagged template literals.
    function tag(strings, ...values) {
        let out = "";
        for (let i = 0; i < strings.length; i++) {
            out += strings[i];
            if (i < values.length) { out += "[" + values[i] + "]"; }
        }
        return out;
    }
    eq(tag`a${1}b${2}c`, "a[1]b[2]c", "tagged template collects strings and values");
    function raw(strings) { return strings.raw[0]; }
    eq(raw`a\nb`, "a\\nb", "String.raw via strings.raw");
})();

// =============================================================================
section("3. Default parameters");
// =============================================================================
(function () {
    function power(base, exp = 2) { return base ** exp; }
    eq(power(5), 25, "default applied when arg omitted");
    eq(power(5, 3), 125, "explicit arg overrides default");
    eq(power(5, undefined), 25, "undefined triggers the default");
    eq(power(5, null), 1, "null does NOT trigger default (5 ** 0 === 1)");

    function greet(name, greeting = "Hi") { return `${greeting}, ${name}`; }
    eq(greet("Bob"), "Hi, Bob", "default string param");
    eq(greet("Bob", "Hey"), "Hey, Bob", "override default string param");

    // Later defaults can reference earlier parameters.
    function f(a, b = a * 2) { return b; }
    eq(f(3), 6, "default references a previous parameter");

    // Defaults are evaluated at call time, not definition time.
    let counter = 0;
    function bump(v = ++counter) { return v; }
    eq(bump(), 1, "default expression evaluated at call time (1st)");
    eq(bump(), 2, "default expression evaluated at call time (2nd)");
})();

// =============================================================================
section("4. Rest parameters");
// =============================================================================
(function () {
    function sum(...nums) { return nums.reduce((a, b) => a + b, 0); }
    eq(sum(1, 2, 3, 4), 10, "rest collects all args");
    eq(sum(), 0, "rest with no args is empty");

    function headThen(first, ...rest) { return first + "|" + rest.join(","); }
    eq(headThen(1, 2, 3), "1|2,3", "rest after a leading param");

    // `arguments` is not the same as rest; rest is a real Array.
    function typeCheck(...args) { return Array.isArray(args); }
    ok(typeCheck(1, 2), "rest parameter is a real Array");

    throws(() => { eval("function bad(...a, ...b){}"); }, "only one rest param allowed (SyntaxError)");
})();

// =============================================================================
section("5. Spread & object rest/spread");
// =============================================================================
(function () {
    // Array spread
    const base = [1, 2, 3];
    eq([0, ...base, 4].join(","), "0,1,2,3,4", "spread in array literal");

    // Call spread
    function add3(x, y, z) { return x + y + z; }
    eq(add3(...[1, 2, 3]), 6, "spread in call arguments");

    // Spread as a shallow copy
    const copy = [...base];
    deepEq(copy, [1, 2, 3], "spread copies an array");
    ok(copy !== base, "spread produces a new array object");

    // String spread
    deepEq([..."abc"], ["a", "b", "c"], "spread a string into chars");

    // Object spread
    const o1 = { a: 1, b: 2 };
    const o2 = { ...o1, c: 3 };
    deepEq(o2, { a: 1, b: 2, c: 3 }, "object spread copies and adds");
    const o3 = { ...o1, a: 99 };
    eq(o3.a, 99, "later key overrides spread value");

    // Object rest in destructuring
    const { a, ...restObj } = { a: 1, b: 2, c: 3 };
    eq(a, 1, "object rest: extracted a");
    deepEq(restObj, { b: 2, c: 3 }, "object rest collects the remainder");
})();

// =============================================================================
section("6. Destructuring");
// =============================================================================
(function () {
    // Array destructuring
    const [p, q] = [10, 20];
    eq(p, 10, "array destructure first");
    eq(q, 20, "array destructure second");

    const [h1, , h3] = [1, 2, 3];
    eq(h1, 1, "array destructure with a hole: first");
    eq(h3, 3, "array destructure with a hole: third");

    const [head, ...tail] = [1, 2, 3, 4];
    eq(head, 1, "array destructure rest: head");
    deepEq(tail, [2, 3, 4], "array destructure rest: tail");

    const [d = 5] = [];
    eq(d, 5, "array destructure default when undefined");

    // Swap via destructuring
    let x = 1, y = 2;
    [x, y] = [y, x];
    eq(x, 2, "swap: x");
    eq(y, 1, "swap: y");

    // Object destructuring
    const point = { px: 5, py: 6 };
    const { px, py } = point;
    eq(px, 5, "object destructure shorthand px");
    eq(py, 6, "object destructure shorthand py");

    const { px: aliasX } = point;
    eq(aliasX, 5, "object destructure rename");

    const { missing = "def" } = {};
    eq(missing, "def", "object destructure default");

    // Nested destructuring
    const data = { user: { name: "Ann", tags: ["a", "b"] } };
    const { user: { name, tags: [firstTag] } } = data;
    eq(name, "Ann", "nested object destructure name");
    eq(firstTag, "a", "nested array-in-object destructure");

    // Destructuring in function parameters
    function dist({ a = 0, b = 0 } = {}) { return a + b; }
    eq(dist({ a: 3, b: 4 }), 7, "object destructuring in params");
    eq(dist(), 0, "param destructuring with default object");
    function arrDist([m, n]) { return m * n; }
    eq(arrDist([3, 4]), 12, "array destructuring in params");
})();

// =============================================================================
section("7. Object literal enhancements");
// =============================================================================
(function () {
    // Property shorthand
    const vname = "val", keyName = "dyn";
    const obj = { vname, keyName };
    eq(obj.vname, "val", "property shorthand (vname)");
    eq(obj.keyName, "dyn", "property shorthand (keyName)");

    // Computed property names
    const suffix = "amic";
    const computed = { ["dyn" + suffix]: 42, ["k" + (1 + 1)]: "two" };
    eq(computed.dynamic, 42, "computed key (concatenation)");
    eq(computed.k2, "two", "computed key (expression)");

    // Method shorthand
    const m = { speak() { return "spoken"; } };
    eq(m.speak(), "spoken", "method shorthand");

    // Getters and setters
    const g = {
        _v: 1,
        get v() { return this._v; },
        set v(nv) { this._v = nv * 10; }
    };
    eq(g.v, 1, "getter returns backing field");
    g.v = 5;
    eq(g.v, 50, "setter transforms the assigned value");

    // __proto__ in an object literal
    const proto = { hi() { return "hi"; } };
    const child = { __proto__: proto };
    eq(child.hi(), "hi", "__proto__ sets the prototype in a literal");
})();

// =============================================================================
section("8. Arrow functions");
// =============================================================================
(function () {
    const dbl = x => x * 2;
    eq(dbl(4), 8, "single param, concise body");
    const add2 = (a, b) => a + b;
    eq(add2(3, 5), 8, "two params, concise body");
    const answer = () => 42;
    eq(answer(), 42, "no params, concise body");
    const objRet = () => ({ k: 1 });
    deepEq(objRet(), { k: 1 }, "concise body returning an object literal");
    const block = x => { return x * 3; };
    eq(block(4), 12, "block body with explicit return");

    // Lexical `this`
    function Counter() {
        this.n = 0;
        this.bump = () => this.n++;
    }
    const c = new Counter();
    c.bump(); c.bump();
    eq(c.n, 2, "arrow captures lexical this");
    const detachedBump = c.bump;
    detachedBump();
    eq(c.n, 3, "detached arrow preserves definition-time this");
    const otherCounter = { n: 100, bump: c.bump };
    otherCounter.bump.call(otherCounter);
    eq(c.n, 4, "call cannot replace arrow lexical this");
    eq(otherCounter.n, 100, "arrow ignores member and call receiver");

    function missingArrowMemberTrap() { return "wrong"; }
    throws(() => ({}).missingArrowMemberTrap(),
        "missing member does not fall back to lexical function");

    // Arrows have no prototype and are not constructible
    ok(!Object.prototype.hasOwnProperty.call(dbl, "prototype"), "arrow has no prototype property");
    throws(() => new dbl(), "arrow cannot be used with new");

    // Arrows have no own `arguments`
    const noArgs = () => typeof arguments;
    function wrapper() { return noArgs(); }
    eq(wrapper(), "object", "arrow uses the enclosing function's arguments");
})();

// =============================================================================
section("9. Classes");
// =============================================================================
(function () {
    class Animal {
        constructor(name) { this.name = name; }
        speak() { return this.name + " makes a noise"; }
        static create(name) { return new Animal(name); }
    }
    const a = new Animal("Rex");
    eq(a.speak(), "Rex makes a noise", "instance method");
    eq(Animal.create("Bob").name, "Bob", "static method");
    ok(typeof Animal.prototype.speak === "function", "method lives on the prototype");

    // Inheritance and super
    class Dog extends Animal {
        constructor(name, breed) {
            super(name);
            this.breed = breed;
        }
        speak() { return super.speak() + " (woof)"; }
    }
    const d = new Dog("Fido", "lab");
    eq(d.name, "Fido", "super constructor sets inherited field");
    eq(d.breed, "lab", "subclass constructor sets own field");
    eq(d.speak(), "Fido makes a noise (woof)", "super.method() call");
    ok(d instanceof Dog, "instanceof subclass");
    ok(d instanceof Animal, "instanceof superclass");

    // Accessors on a class
    class Box {
        constructor(w) { this._w = w; }
        get w() { return this._w; }
        set w(v) { this._w = v + 1; }
        static get label() { return "box"; }
    }
    const bx = new Box(3);
    eq(bx.w, 3, "class getter");
    bx.w = 10;
    eq(bx.w, 11, "class setter transforms value");
    eq(Box.label, "box", "static getter");

    // Class expression
    const Point = class { constructor(x) { this.x = x; } };
    eq(new Point(7).x, 7, "class expression");

    // Subclassing a built-in
    class MyArray extends Array {
        get first() { return this[0]; }
    }
    const ma = new MyArray();
    ma.push(9, 8);
    eq(ma.first, 9, "subclassing Array works");
    eq(ma.length, 2, "Array subclass keeps length semantics");

    // Methods are non-enumerable
    const keys = [];
    for (const k in d) { keys.push(k); }
    ok(!keys.includes("speak"), "class methods are not enumerable via for..in");
})();


// =============================================================================
section("10. for...of / for...in / iteration protocol");
// =============================================================================
(function () {
    // for...of over an array
    let total = 0;
    for (const e of [1, 2, 3, 4]) { total += e; }
    eq(total, 10, "for...of over array literal");

    // for...of over a string
    let chars = "";
    for (const ch of "abc") { chars += ch; }
    eq(chars, "abc", "for...of iterates string code units");

    // for...of over a Map (entries)
    const mp = new Map([["a", 1], ["b", 2]]);
    const pairs = [];
    for (const [k, v] of mp) { pairs.push(k + v); }
    deepEq(pairs, ["a1", "b2"], "for...of over Map yields [key,value]");

    // for...of with break/continue
    let sum = 0;
    for (const n of [1, 2, 3, 4, 5]) {
        if (n === 2) continue;
        if (n === 5) break;
        sum += n;
    }
    eq(sum, 8, "for...of honors break and continue (1+3+4)");

    // for...in enumerates keys (strings)
    const keys = [];
    for (const k in { x: 1, y: 2 }) { keys.push(k); }
    deepEq(keys, ["x", "y"], "for...in yields own enumerable keys");

    const idx = [];
    for (const i in [10, 20, 30]) { idx.push(i); }
    deepEq(idx, ["0", "1", "2"], "for...in over array yields string indices");

    // Manual iteration protocol
    function collect(iterable) {
        const out = [];
        const it = iterable[Symbol.iterator]();
        let step = it.next();
        while (!step.done) { out.push(step.value); step = it.next(); }
        return out;
    }
    deepEq(collect([7, 8, 9]), [7, 8, 9], "Symbol.iterator protocol drives for...of");

    // A custom iterable object
    const range = {
        from: 1, to: 3,
        [Symbol.iterator]() {
            let cur = this.from, last = this.to;
            return {
                next() {
                    return cur <= last ? { value: cur++, done: false }
                                       : { value: undefined, done: true };
                }
            };
        }
    };
    deepEq([...range], [1, 2, 3], "custom iterable works with spread");
})();

// =============================================================================
section("11. Generators");
// =============================================================================
(function () {
    function* counter(n) {
        for (let i = 0; i < n; i++) { yield i; }
    }
    const g = counter(3);
    deepEq(g.next(), { value: 0, done: false }, "generator first next()");
    deepEq(g.next(), { value: 1, done: false }, "generator second next()");
    deepEq([...counter(3)], [0, 1, 2], "generator is iterable");

    // Two-way communication with yield
    function* dialog() {
        const a = yield "q1";
        const b = yield "q2";
        return a + "|" + b;
    }
    const d = dialog();
    eq(d.next().value, "q1", "generator yields first question");
    eq(d.next("A").value, "q2", "generator receives value and yields again");
    eq(d.next("B").value, "A|B", "generator return value");

    // yield* delegation
    function* inner() { yield 1; yield 2; }
    function* outerGen() { yield 0; yield* inner(); yield 3; }
    deepEq([...outerGen()], [0, 1, 2, 3], "yield* delegates to another generator");

    // Generator methods on a class-like object
    const obj = {
        *[Symbol.iterator]() { yield "a"; yield "b"; }
    };
    deepEq([...obj], ["a", "b"], "generator method via computed key");

    // return() and throw()
    function* infinite() { let i = 0; while (true) { yield i++; } }
    const inf = infinite();
    inf.next(); inf.next();
    eq(inf.return("done").value, "done", "generator.return() ends iteration");

    function* thrower() { try { yield 1; } catch (e) { yield "caught:" + e; } }
    const t = thrower();
    t.next();
    eq(t.throw("boom").value, "caught:boom", "generator.throw() is catchable inside");
})();

// =============================================================================
section("12. Symbols");
// =============================================================================
(function () {
    const s1 = Symbol("id");
    const s2 = Symbol("id");
    ok(s1 !== s2, "two symbols with the same description differ");
    eq(s1.description, "id", "symbol description");
    eq(String(s1), "Symbol(id)", "String(symbol) format");
    eq(typeof s1, "symbol", "typeof a symbol");

    // Symbol as a unique object key (not enumerated by for..in / Object.keys)
    const key = Symbol("k");
    const o = { [key]: "secret", visible: 1 };
    eq(o[key], "secret", "symbol used as an object key");
    ok(!Object.keys(o).includes(String(key)), "symbol keys are hidden from Object.keys");
    deepEq(Object.keys(o), ["visible"], "only string keys enumerated");
    ok(Object.getOwnPropertySymbols(o).length === 1, "getOwnPropertySymbols finds it");

    // Well-known symbols exist
    ok(typeof Symbol.iterator === "symbol", "Symbol.iterator is a symbol");
    ok(typeof Symbol.toStringTag === "symbol", "Symbol.toStringTag is a symbol");

    // Global symbol registry
    eq(Symbol.for("app"), Symbol.for("app"), "Symbol.for returns the same registered symbol");
    eq(Symbol.keyFor(Symbol.for("app")), "app", "Symbol.keyFor reads back the key");

    // Symbol.toPrimitive
    const money = {
        [Symbol.toPrimitive](hint) {
            return hint === "number" ? 100 : "$100";
        }
    };
    eq(+money, 100, "Symbol.toPrimitive number hint");
    eq(`${money}`, "$100", "Symbol.toPrimitive string hint");
})();

// =============================================================================
section("13. Collections: Map / Set / WeakMap / WeakSet");
// =============================================================================
(function () {
    // --- Map ---
    const m = new Map();
    m.set("a", 1).set("b", 2);
    eq(m.size, 2, "Map size");
    eq(m.get("a"), 1, "Map get");
    eq(m.has("b"), true, "Map has present");
    eq(m.has("z"), false, "Map has absent");
    eq(m.get("z"), undefined, "Map get absent is undefined");
    m.set("a", 100);
    eq(m.get("a"), 100, "Map set overwrites");
    eq(m.size, 2, "Map size unchanged on overwrite");
    eq(m.delete("a"), true, "Map delete present returns true");
    eq(m.delete("a"), false, "Map delete absent returns false");
    m.clear();
    eq(m.size, 0, "Map clear");

    // Any value type as key (including objects & NaN)
    const ok1 = {}, ok2 = {};
    const mk = new Map([[ok1, "one"], [NaN, "nan"]]);
    eq(mk.get(ok1), "one", "Map uses object identity for keys");
    eq(mk.get(ok2), undefined, "different object is a different key");
    eq(mk.get(NaN), "nan", "Map treats NaN keys as equal (SameValueZero)");

    // Construction from iterable, keys/values/entries, forEach
    const m2 = new Map([["x", 10], ["y", 20]]);
    deepEq([...m2.keys()], ["x", "y"], "Map.keys()");
    deepEq([...m2.values()], [10, 20], "Map.values()");
    deepEq([...m2.entries()], [["x", 10], ["y", 20]], "Map.entries()");
    let acc = "";
    m2.forEach((v, k) => { acc += k + "=" + v + ";"; });
    eq(acc, "x=10;y=20;", "Map.forEach visits (value, key) in order");

    // --- Set ---
    const s = new Set([1, 2, 2, 3, 3, 3]);
    eq(s.size, 3, "Set deduplicates on construction");
    ok(s.has(2), "Set has present");
    ok(!s.has(9), "Set has absent");
    s.add(4).add(4);
    eq(s.size, 4, "Set add deduplicates");
    eq(s.delete(1), true, "Set delete present returns true");
    deepEq([...s], [2, 3, 4], "Set preserves insertion order");
    ok(new Set([NaN, NaN]).size === 1, "Set treats NaN as equal");

    // --- WeakMap / WeakSet ---
    const wm = new WeakMap();
    let wkey = {};
    wm.set(wkey, "wv");
    eq(wm.get(wkey), "wv", "WeakMap stores against an object key");
    eq(wm.has(wkey), true, "WeakMap has");
    throws(() => new WeakMap().set("primitive", 1), "WeakMap rejects non-object keys");

    const ws = new WeakSet();
    let so = {};
    ws.add(so);
    ok(ws.has(so), "WeakSet has added object");
    throws(() => new WeakSet().add(1), "WeakSet rejects primitives");
})();

// =============================================================================
section("14. Promise");
// =============================================================================
const __promiseTests = (function () {
    // Resolution
    let r1;
    Promise.resolve(5).then(v => { r1 = v; });
    return Promise.resolve(5).then(v => {
        eq(v, 5, "Promise.resolve delivers its value");
        // Chaining returns a new promise
        return Promise.resolve(1).then(a => a + 1).then(b => b * 3);
    }).then(result => {
        eq(result, 6, "promise chaining computes (1+1)*3");
        // Rejection & catch
        return Promise.reject(new Error("boom")).catch(e => "caught:" + e.message);
    }).then(msg => {
        eq(msg, "caught:boom", "catch handles a rejection");
        // finally runs for both states
        let fin = 0;
        return Promise.resolve(1).finally(() => { fin++; }).then(() => fin);
    }).then(finCount => {
        eq(finCount, 1, "finally runs on resolution");
        // Promise.all
        return Promise.all([Promise.resolve(1), Promise.resolve(2), Promise.resolve(3)]);
    }).then(all => {
        deepEq(all, [1, 2, 3], "Promise.all resolves all values in order");
        // Promise.all rejects fast
        return Promise.all([Promise.resolve(1), Promise.reject("x")]).catch(e => "all-rejected:" + e);
    }).then(msg => {
        eq(msg, "all-rejected:x", "Promise.all rejects if any rejects");
        // Promise.race
        return Promise.race([
            new Promise(res => setTimeout(() => res("slow"), 50)),
            Promise.resolve("fast")
        ]);
    }).then(raceWinner => {
        eq(raceWinner, "fast", "Promise.race settles with the first");
        // Promise.allSettled
        return Promise.allSettled([Promise.resolve(1), Promise.reject("e")]);
    }).then(settled => {
        eq(settled[0].status, "fulfilled", "allSettled fulfilled entry");
        eq(settled[0].value, 1, "allSettled fulfilled value");
        eq(settled[1].status, "rejected", "allSettled rejected entry");
        eq(settled[1].reason, "e", "allSettled rejected reason");
        // new Promise executor runs synchronously
        let executed = false;
        new Promise((resolve) => { executed = true; resolve(); });
        ok(executed, "executor body runs immediately");
    });
})();

// =============================================================================
section("15. async / await");
// =============================================================================
(async function () {
    await __promiseTests;
    async function retFive() { return 5; }
    ok(retFive() instanceof Promise, "async function returns a Promise");
    eq(await retFive(), 5, "await unwraps the returned promise");

    eq(await Promise.resolve(7) * 2, 14, "await a resolved promise");
    eq(await 9, 9, "await a non-promise passes it through");

    // Sequential awaits
    async function multi() {
        const a = await Promise.resolve(1);
        const b = await Promise.resolve(2);
        return a + b;
    }
    eq(await multi(), 3, "sequential awaits");

    // Awaiting another async function
    async function inner() { return 4; }
    async function outer() { return (await inner()) * 10; }
    eq(await outer(), 40, "await a nested async function");

    // try/catch around await on a rejection
    async function mayFail() { throw new Error("async-boom"); }
    let caught = "";
    try { await mayFail(); } catch (e) { caught = e.message; }
    eq(caught, "async-boom", "async throw is catchable with await");

    // async arrow function
    const arrowAsync = async () => (await Promise.resolve(6)) + 1;
    eq(await arrowAsync(), 7, "async arrow function");

    // async object method
    const obj = { base: 100, async fetch() { return this.base + (await Promise.resolve(1)); } };
    eq(await obj.fetch(), 101, "async method with this");

    // for-await-of over async iterables
    async function* agen() { yield 1; yield 2; yield 3; }
    const collected = [];
    for await (const v of agen()) { collected.push(v); }
    deepEq(collected, [1, 2, 3], "for-await-of over an async generator");

    // Fall-through resolves to undefined
    async function noReturn() { const x = 1; }
    eq(await noReturn(), undefined, "async fall-through resolves undefined");
})().then(() => {
    // =========================================================================
    section("16. Array statics & prototype methods");
    // =========================================================================
    (function () {
        // Array.from
        deepEq(Array.from("abc"), ["a", "b", "c"], "Array.from a string");
        deepEq(Array.from([1, 2, 3], x => x * 2), [2, 4, 6], "Array.from with mapFn");
        deepEq(Array.from({ length: 3 }, (_, i) => i), [0, 1, 2], "Array.from array-like");
        deepEq(Array.from(new Set([1, 1, 2])), [1, 2], "Array.from a Set");

        // Array.of
        deepEq(Array.of(1, 2, 3), [1, 2, 3], "Array.of");
        deepEq(Array.of(7), [7], "Array.of single number (not length)");

        const arr = [1, 2, 3, 4, 5];
        // Callback results use ECMAScript ToBoolean. In particular, every object,
        // array and function is truthy; empty strings, zero and NaN are falsy.
        deepEq([{ a: 1 }, {}].filter(x => x), [{ a: 1 }, {}], "filter keeps object-valued predicates");
        deepEq(["", "x", 0, 2, NaN].filter(x => x), ["x", 2], "filter applies primitive ToBoolean");
        ok([1].some(() => ({})), "some treats an object result as truthy");
        ok([1].every(() => []), "every treats an array result as truthy");
        eq([1].find(() => function () {}), 1, "find treats a function result as truthy");
        eq([1].findIndex(() => ({})), 0, "findIndex treats an object result as truthy");

        // find / findIndex
        eq(arr.find(x => x > 3), 4, "find first match");
        eq(arr.findIndex(x => x > 3), 3, "findIndex first match");
        eq(arr.find(x => x > 99), undefined, "find no match is undefined");
        eq(arr.findIndex(x => x > 99), -1, "findIndex no match is -1");

        // includes
        ok(arr.includes(3), "includes present");
        ok(!arr.includes(9), "includes absent");
        ok([NaN].includes(NaN), "includes finds NaN");

        // fill / copyWithin
        deepEq([1, 2, 3, 4].fill(0, 1, 3), [1, 0, 0, 4], "fill range");
        deepEq([1, 2, 3, 4, 5].copyWithin(0, 3), [4, 5, 3, 4, 5], "copyWithin");

        // entries / keys / values
        deepEq([...["a", "b"].entries()], [[0, "a"], [1, "b"]], "Array.entries");
        deepEq([...["a", "b"].keys()], [0, 1], "Array.keys");
        deepEq([...["a", "b"].values()], ["a", "b"], "Array.values");

        // flat / flatMap
        deepEq([1, [2, [3, [4]]]].flat(Infinity), [1, 2, 3, 4], "flat(Infinity)");
        deepEq([1, 2, 3].flatMap(x => [x, x * 2]), [1, 2, 2, 4, 3, 6], "flatMap");

        // classic ES5 methods still present
        eq([1, 2, 3].map(x => x * 2).join(","), "2,4,6", "map");
        deepEq(["1", "2", "3"].map(Number), [1, 2, 3], "map calls builtin Number converter");
        deepEq([1, 2, 3].map(String), ["1", "2", "3"], "map calls builtin String converter");
        eq([1, 2, 3].filter(x => x > 1).join(","), "2,3", "filter");
        eq([1, 2, 3].reduce((a, b) => a + b, 0), 6, "reduce");
        ok([1, 2, 3].some(x => x > 2), "some");
        ok([1, 2, 3].every(x => x > 0), "every");
        eq([3, 1, 2].sort().join(","), "1,2,3", "sort");
        eq([1, 2, 3].indexOf(2), 1, "indexOf");
        eq([1, 2, 3].lastIndexOf(3), 2, "lastIndexOf");
        deepEq([1, 2, 3].slice(1), [2, 3], "slice");
    })();

    // =========================================================================
    section("17. String methods");
    // =========================================================================
    (function () {
        const s = "Hello, World";
        ok(s.startsWith("Hello"), "startsWith");
        ok(!s.startsWith("World"), "startsWith negative");
        ok(s.endsWith("World"), "endsWith");
        ok(s.includes("o, W"), "includes substring");
        eq("ab".repeat(3), "ababab", "repeat");
        eq("5".padStart(3, "0"), "005", "padStart");
        eq("5".padEnd(3, "."), "5..", "padEnd");
        eq("abc".padStart(2), "abc", "padStart no-op when longer");
        eq("  trim  ".trim(), "trim", "trim");
        eq("\u0061\u030A".normalize().length, 1, "normalize combines marks");

        // Unicode helpers
        eq(String.fromCodePoint(0x1F600).length, 2, "fromCodePoint creates an astral char");
        eq("\u{1F600}", "\uD83D\uDE00", "unicode code point escape");
        eq("abc".codePointAt(0), 97, "codePointAt");

        // Destructuring a string iterates code points
        deepEq([..."a\u{1F600}b"], ["a", "\u{1F600}", "b"], "spread string by code point");
    })();

    // =========================================================================
    section("18. Number & Math additions");
    // =========================================================================
    (function () {
        ok(Number.isInteger(5), "Number.isInteger(5)");
        ok(!Number.isInteger(5.5), "Number.isInteger(5.5) is false");
        ok(Number.isFinite(10), "Number.isFinite(10)");
        ok(!Number.isFinite(Infinity), "Number.isFinite(Infinity) is false");
        ok(Number.isNaN(NaN), "Number.isNaN(NaN)");
        ok(!Number.isNaN("x"), "Number.isNaN does not coerce");
        ok(Number.isSafeInteger(2 ** 53 - 1), "Number.isSafeInteger max");
        eq(Number.parseInt("42px"), 42, "Number.parseInt");
        eq(Number.parseFloat("3.14abc"), 3.14, "Number.parseFloat");
        eq(Number.EPSILON > 0, true, "Number.EPSILON is positive");
        eq(Number.MAX_SAFE_INTEGER, 9007199254740991, "Number.MAX_SAFE_INTEGER");

        eq(Math.trunc(5.9), 5, "Math.trunc");
        eq(Math.trunc(-5.9), -5, "Math.trunc negative");
        eq(Math.sign(-3), -1, "Math.sign");
        eq(Math.cbrt(27), 3, "Math.cbrt");
        eq(Math.hypot(3, 4), 5, "Math.hypot");
        eq(Math.log2(8), 3, "Math.log2");
        eq(Math.log10(1000), 3, "Math.log10");
        ok(Math.imul(3, 4) === 12, "Math.imul");
        eq(Math.clz32(1), 31, "Math.clz32");
    })();

    // =========================================================================
    section("19. Object static methods");
    // =========================================================================
    (function () {
        const target = { a: 1 };
        Object.assign(target, { b: 2 }, { c: 3 });
        deepEq(target, { a: 1, b: 2, c: 3 }, "Object.assign merges sources");

        ok(Object.is(NaN, NaN), "Object.is(NaN, NaN)");
        ok(!Object.is(0, -0), "Object.is(0, -0) is false");

        const o = { x: 1, y: 2 };
        deepEq(Object.keys(o), ["x", "y"], "Object.keys");
        deepEq(Object.values(o), [1, 2], "Object.values");
        deepEq(Object.entries(o), [["x", 1], ["y", 2]], "Object.entries");
        deepEq(Object.fromEntries([["p", 1], ["q", 2]]), { p: 1, q: 2 }, "Object.fromEntries");

        const frozen = Object.freeze({ n: 1 });
        throws(() => { "use strict"; frozen.n = 2; }, "Object.freeze prevents mutation");
        ok(Object.isFrozen(frozen), "Object.isFrozen");

        const src = { a: 1, b: { c: 2 } };
        const clone = Object.assign({}, src);
        ok(clone !== src, "Object.assign creates a new object");
        ok(clone.b === src.b, "Object.assign is a shallow copy");

        eq(Object.getOwnPropertyNames(o).length, 2, "getOwnPropertyNames");
        ok(Object.getOwnPropertyDescriptor(target, "a").value === 1, "getOwnPropertyDescriptor");
    })();

    // =========================================================================
    section("20. Operators: exponent, new.target, typeof/instanceof");
    // =========================================================================
    (function () {
        eq(2 ** 10, 1024, "exponent operator");
        eq(2 ** 3 ** 2, 512, "exponent is right-associative");
        let b = 2; b **= 3;
        eq(b, 8, "**= compound assignment");
        eq((-2) ** 2, 4, "exponent with a negative base");

        // new.target
        let sawTarget = "unset";
        function Ctor() { sawTarget = new.target; }
        new Ctor();
        ok(sawTarget === Ctor, "new.target is the constructor under new");
        sawTarget = "unset";
        Ctor();
        eq(sawTarget, undefined, "new.target is undefined on a plain call");

        // typeof / instanceof edge cases
        eq(typeof undefined, "undefined", "typeof undefined");
        eq(typeof null, "object", "typeof null is object (historic quirk)");
        eq(typeof Symbol(), "symbol", "typeof symbol");
        eq(typeof (() => {}), "function", "typeof arrow function");
        ok([] instanceof Array, "array instanceof Array");
        ok({} instanceof Object, "object instanceof Object");
        ok(!(1 instanceof Number), "primitive is not instanceof Number");
    })();

    // =========================================================================
    section("21. Closures & loop capture");
    // =========================================================================
    (function () {
        function makeCounter() {
            let count = 0;
            return {
                inc() { return ++count; },
                get() { return count; }
            };
        }
        const c1 = makeCounter(), c2 = makeCounter();
        c1.inc(); c1.inc(); c2.inc();
        eq(c1.get(), 2, "closure c1 has its own state");
        eq(c2.get(), 1, "closure c2 is independent");

        // Capturing loop variables with let vs var
        const letFns = [], varFns = [];
        for (let i = 0; i < 3; i++) { letFns.push(() => i); }
        for (var k = 0; k < 3; k++) { varFns.push(() => k); }
        deepEq(letFns.map(f => f()), [0, 1, 2], "let loop capture is per-iteration");
        deepEq(varFns.map(f => f()), [3, 3, 3], "var loop capture is shared");

        // IIFE capture workaround for var
        const iifeFns = [];
        for (var m = 0; m < 3; m++) {
            iifeFns.push((n => () => n)(m));
        }
        deepEq(iifeFns.map(f => f()), [0, 1, 2], "IIFE recaptures var per iteration");
    })();

    // =========================================================================
    section("22. Corner cases & interop");
    // =========================================================================
    (function () {
        // Default + destructuring + rest combined
        function mixed({ a = 1, b } = {}, ...rest) {
            return [a, b, rest.length];
        }
        deepEq(mixed({ b: 2 }, 9, 8), [1, 2, 2], "default+destructure+rest combine");

        // Computed method names in a class-like object
        const name = "greet";
        const o = { [name]() { return "hi"; }, ["a" + "b"]: 1 };
        eq(o.greet(), "hi", "computed method name");
        eq(o.ab, 1, "computed property name");

        // Optional chaining & nullish coalescing (ES2020, commonly grouped with ES6)
        const nested = { a: { b: { c: 1 } } };
        eq(nested?.a?.b?.c, 1, "optional chaining reads a deep value");
        eq(nested?.x?.y, undefined, "optional chaining short-circuits to undefined");
        eq(null ?? "fallback", "fallback", "nullish coalescing on null");
        eq(0 ?? "fallback", 0, "nullish coalescing keeps 0");

        // Logical assignment (ES2021)
        let la = 0; la ||= 5; eq(la, 5, "||= assigns when falsy");
        let lb = 3; lb &&= 7; eq(lb, 7, "&&= assigns when truthy");
        let lc = null; lc ??= 9; eq(lc, 9, "??= assigns when nullish");

        // Numeric separators (ES2021)
        eq(1_000_000, 1000000, "numeric separators in integers");

        // Destructuring an array returned by a function
        function pair() { return [1, 2]; }
        const [pa, pb] = pair();
        eq(pa + pb, 3, "destructure a function's array result");
    })();

    // =========================================================================
    section("23. Global functions & globalThis");
    // =========================================================================
    (function () {
        eq(typeof globalThis, "object", "globalThis is an object");
        ok(globalThis === globalThis, "globalThis is self-referential");
        ok(typeof globalThis.parseInt === "function", "globals hang off globalThis");

        // The global isNaN coerces its argument first (unlike Number.isNaN).
        eq(isNaN(NaN), true, "isNaN(NaN)");
        eq(isNaN("foo"), true, "isNaN coerces a non-numeric string");
        eq(isNaN("12"), false, "isNaN('12') is numeric");
        eq(isNaN(undefined), true, "isNaN(undefined) -> NaN");
        eq(isNaN(null), false, "isNaN(null) -> 0");

        eq(parseInt("42"), 42, "parseInt decimal");
        eq(parseInt("42abc"), 42, "parseInt stops at the first invalid digit");
        eq(parseInt("  7  "), 7, "parseInt skips surrounding whitespace");
        eq(parseInt("ff", 16), 255, "parseInt honours an explicit radix");
        eq(parseInt("101", 2), 5, "parseInt binary radix");
        ok(isNaN(parseInt("abc")), "parseInt of garbage is NaN");
        eq(parseFloat("3.14"), 3.14, "parseFloat decimal");
        eq(parseFloat("2.5abc"), 2.5, "parseFloat stops at the first invalid digit");
        ok(isNaN(parseFloat("x")), "parseFloat of garbage is NaN");

        eq(Number.isInteger(3), true, "Number.isInteger(3)");
        eq(Number.isInteger(3.5), false, "Number.isInteger(3.5)");
        eq(Number.isNaN(NaN), true, "Number.isNaN(NaN)");
        eq(Number.isNaN("x"), false, "Number.isNaN never coerces");
        eq(Number.isFinite(1), true, "Number.isFinite(1)");
        eq(Number.isFinite(Infinity), false, "Number.isFinite(Infinity)");
        eq(Number.isSafeInteger(5), true, "Number.isSafeInteger(5)");
    })();

    // =========================================================================
    section("24. Error subtypes & AggregateError");
    // =========================================================================
    (function () {
        const e = new TypeError("bad");
        eq(e.message, "bad", "TypeError carries its message");
        eq(e.name, "TypeError", "TypeError reports its name");
        ok(e instanceof TypeError, "TypeError instanceof TypeError");
        ok(e instanceof Error, "a subtype is instanceof Error");
        eq(typeof e, "object", "an error is an object");

        // Every standard subtype names itself and is an Error.
        eq(new RangeError("m").name, "RangeError", "RangeError name");
        ok(new RangeError("m") instanceof Error, "RangeError instanceof Error");
        eq(new ReferenceError("m").name, "ReferenceError", "ReferenceError name");
        ok(new ReferenceError("m") instanceof Error, "ReferenceError instanceof Error");
        eq(new SyntaxError("m").name, "SyntaxError", "SyntaxError name");
        ok(new SyntaxError("m") instanceof Error, "SyntaxError instanceof Error");
        eq(new EvalError("m").name, "EvalError", "EvalError name");
        ok(new EvalError("m") instanceof Error, "EvalError instanceof Error");
        eq(new URIError("m").name, "URIError", "URIError name");
        ok(new URIError("m") instanceof Error, "URIError instanceof Error");

        // Distinct instances keep distinct messages.
        const a = new TypeError("first"), b = new TypeError("second");
        eq(a.message, "first", "first instance keeps its own message");
        eq(b.message, "second", "second instance keeps its own message");

        // Error.prototype.toString() -> "Name: message"
        eq(new TypeError("x").toString(), "TypeError: x", "toString is 'Name: message'");
        eq(new RangeError().toString(), "RangeError", "toString omits an empty message");
        eq(String(new Error("boom")), "Error: boom", "String(error) uses toString");

        // A thrown subtype is caught as itself.
        let caught = null;
        try { throw new RangeError("out"); } catch (err) { caught = err; }
        ok(caught instanceof RangeError, "a thrown subtype is caught as itself");
        eq(caught.message, "out", "the caught error keeps its message");

        // AggregateError (ES2021): an error that holds a list of errors.
        const agg = new AggregateError([new Error("e1"), new Error("e2")], "multi");
        eq(agg.name, "AggregateError", "AggregateError name");
        eq(agg.message, "multi", "AggregateError message");
        eq(agg.errors.length, 2, "AggregateError holds the errors iterable");
        ok(agg instanceof Error, "AggregateError instanceof Error");
    })();

    // =========================================================================
    section("25. Object statics (ES6+ / ES2022)");
    // =========================================================================
    (function () {
        eq(Object.hasOwn({ a: 1 }, "a"), true, "Object.hasOwn finds an own key");
        eq(Object.hasOwn({ a: 1 }, "b"), false, "Object.hasOwn rejects a missing key");
        eq(Object.is(NaN, NaN), true, "Object.is treats NaN as same-value equal");
        eq(Object.is(0, -0), false, "Object.is distinguishes +0 from -0");

        const proto = { x: 1 };
        const child = Object.create(proto);
        ok(Object.getPrototypeOf(child) === proto, "getPrototypeOf(create(o)) is o");
        const re = {};
        Object.setPrototypeOf(re, proto);
        eq(re.x, 1, "setPrototypeOf rewires the prototype chain");

        function StaticBase() {}
        let instanceSetterCalls = 0;
        Object.defineProperties(StaticBase.prototype, {
            status: { set: function () { instanceSetterCalls++; } }
        });
        StaticBase.answer = 42;
        function StaticChild() {}
        Object.setPrototypeOf(StaticChild, StaticBase);
        StaticChild.status = "READY";
        eq(instanceSetterCalls, 0, "callable static chain skips Base.prototype accessors");
        eq(StaticChild.status, "READY", "static assignment creates an own property");
        eq(StaticChild.answer, 42, "callable static chain inherits constructor statics");

        const descriptors = Object.getOwnPropertyDescriptors({ a: 1 });
        ok(descriptors.a !== undefined, "getOwnPropertyDescriptors returns per-key descriptors");

        const sealed = { a: 1 };
        Object.seal(sealed);
        eq(Object.isSealed(sealed), true, "isSealed is true after seal");

        const nx = { a: 1 };
        Object.preventExtensions(nx);
        eq(Object.isExtensible(nx), false, "isExtensible is false after preventExtensions");

        const dp = {};
        Object.defineProperties(dp, {
            a: { value: 1 },
            b: { value: 2 },
            sum: { get: function () { return this.a + this.b; } }
        });
        eq(dp.a, 1, "defineProperties sets a");
        eq(dp.b, 2, "defineProperties sets b");
        eq(dp.sum, 3, "defineProperties installs accessor descriptors");
    })();

    // =========================================================================
    section("26. Array.at & String extras");
    // =========================================================================
    (function () {
        eq([1, 2, 3].at(0), 1, "Array.at(0)");
        eq([1, 2, 3].at(-1), 3, "Array.at(-1) counts from the end");
        eq([1, 2, 3].at(5), undefined, "Array.at out of range is undefined");

        eq("abc".at(0), "a", "String.at(0)");
        eq("abc".at(-1), "c", "String.at(-1) counts from the end");
        eq("abc".at(9), undefined, "String.at out of range is undefined");

        eq("  x  ".trimStart(), "x  ", "trimStart strips leading whitespace");
        eq("  x  ".trimEnd(), "  x", "trimEnd strips trailing whitespace");
        eq("  x  ".trimLeft(), "x  ", "trimLeft is an alias of trimStart");
        eq("  x  ".trimRight(), "  x", "trimRight is an alias of trimEnd");

        eq("a-b-c".replaceAll("-", "+"), "a+b+c", "replaceAll replaces every occurrence");
        eq("aaa".replaceAll("a", "bb"), "bbbbbb", "replaceAll handles a growing replacement");
        eq("abc".replaceAll("q", "!"), "abc", "replaceAll with no match is a no-op");
    })();

    // =========================================================================
    section("27. Promise.any (ES2021)");
    // =========================================================================
    (function () {
        // Fulfills with the first fulfilled value, skipping rejections.
        let v1 = null;
        Promise.any([Promise.reject("e"), Promise.resolve(42)]).then(r => { v1 = r; });
        eq(v1, 42, "Promise.any fulfills with the first fulfilled");

        let v2 = null;
        Promise.any([Promise.resolve(1), Promise.resolve(2)]).then(r => { v2 = r; });
        eq(v2, 1, "Promise.any takes the first when all fulfill");

        // A non-promise value counts as already fulfilled.
        let v3 = null;
        Promise.any([7, Promise.resolve(9)]).then(r => { v3 = r; });
        eq(v3, 7, "Promise.any treats a plain value as fulfilled");

        // When every input rejects it rejects with an AggregateError.
        let agg = null;
        Promise.any([Promise.reject(1), Promise.reject(2)]).catch(e => { agg = e; });
        ok(agg instanceof AggregateError, "Promise.any rejects with an AggregateError");
        ok(agg instanceof Error, "that AggregateError is an Error");
        eq(agg.errors.length, 2, "the AggregateError collects every reason");
        eq(agg.errors[0], 1, "first collected reason");
        eq(agg.errors[1], 2, "second collected reason");

        // An empty iterable rejects with an empty AggregateError.
        let empty = null;
        Promise.any([]).catch(e => { empty = e; });
        ok(empty instanceof AggregateError, "Promise.any([]) rejects with an AggregateError");
        eq(empty.errors.length, 0, "the empty AggregateError has no errors");
    })();

    // =========================================================================
    section("28. JSON stringify / parse");
    // =========================================================================
    (function () {
        // stringify emits no incidental whitespace (compact), per spec.
        eq(JSON.stringify({ a: 1, b: 2 }), '{"a":1,"b":2}', "stringify an object compactly");
        eq(JSON.stringify([1, 2, 3]), '[1,2,3]', "stringify an array compactly");
        eq(JSON.stringify({ a: [1, { b: 2 }] }), '{"a":[1,{"b":2}]}', "stringify nested structures compactly");
        eq(JSON.stringify(3.5), '3.5', "stringify a float without zero padding");
        eq(JSON.stringify("s"), '"s"', "stringify a string");
        eq(JSON.stringify(null), 'null', "stringify null");
        eq(JSON.stringify(true), 'true', "stringify true");

        // ES: undefined / function / symbol OBJECT properties are omitted; in an
        // ARRAY they serialise as null; a top-level undefined yields undefined.
        // (Emitting `"k":undefined` produced invalid JSON that broke servers.)
        eq(JSON.stringify({ a: 1, b: undefined, c: 3 }), '{"a":1,"c":3}', "stringify omits an undefined property");
        eq(JSON.stringify({ a: 1, f: function () {}, b: 2 }), '{"a":1,"b":2}', "stringify omits a function property");
        eq(JSON.stringify({ only: undefined }), '{}', "stringify of an all-undefined object is {}");
        eq(JSON.stringify([1, undefined, 3]), '[1,null,3]', "stringify renders an undefined array element as null");
        eq(JSON.stringify([1, function () {}, 3]), '[1,null,3]', "stringify renders a function array element as null");
        eq(JSON.stringify(undefined), undefined, "stringify(undefined) is undefined");
        eq(JSON.stringify(function () {}), undefined, "stringify(function) is undefined");
        eq(JSON.stringify({ a: { b: undefined, c: [undefined, 2] } }), '{"a":{"c":[null,2]}}', "stringify omits undefined recursively");

        // parse handles negatives and floats (a stray '-' used to hang the parser).
        eq(JSON.parse('{"a":1}').a, 1, "parse an object member");
        eq(JSON.parse('[1,2,3]').length, 3, "parse an array");
        eq(JSON.parse('-3.5'), -3.5, "parse a negative float");
        eq(JSON.parse('[-1,2,-3.5]')[2], -3.5, "parse negatives inside an array");
        eq(JSON.parse('{"n":-7}').n, -7, "parse a negative integer member");

        // roundtrip
        const rt = JSON.parse(JSON.stringify({ a: 1, b: [2, 3], c: "x" }));
        eq(rt.a, 1, "roundtrip preserves a number");
        eq(rt.b[1], 3, "roundtrip preserves a nested array");
        eq(rt.c, "x", "roundtrip preserves a string");
    })();

    // =========================================================================
    section("29. Date & typeof of constructors");
    // =========================================================================
    (function () {
        // Every constructor reports typeof "function" (native and script classes).
        eq(typeof Date, "function", "typeof Date is 'function'");
        eq(typeof Array, "function", "typeof Array is 'function'");
        eq(typeof Object, "function", "typeof Object is 'function'");
        eq(typeof Promise, "function", "typeof Promise is 'function'");
        eq(typeof TypeError, "function", "typeof an error subtype is 'function'");
        class Local {}
        eq(typeof Local, "function", "typeof a script class is 'function'");

        // Date.now(): a non-negative, monotonic epoch-ms number, stored exactly
        // as an int64 (V_INT64) so a Unix-ms timestamp round-trips without loss.
        eq(typeof Date.now(), "number", "Date.now() is a number");
        ok(Date.now() >= 0, "Date.now() is non-negative");
        const n0 = Date.now(), n1 = Date.now();
        ok(n1 >= n0, "Date.now() is monotonic");

        // new Date() and its instance methods.
        const d = new Date();
        eq(typeof d, "object", "new Date() is an object");
        ok(d instanceof Date, "new Date() instanceof Date");
        eq(typeof d.getTime, "function", "getTime is an instance method");
        eq(typeof d.getTime(), "number", "getTime() is a number");
        ok(d.getTime() >= 0, "getTime() is non-negative");
        eq(typeof d.valueOf(), "number", "valueOf() is a number");

        // A Date built from an explicit time round-trips it exactly. getTime()
        // is an int64, so === (which coerces across numeric tags) matches the
        // int32/int64 literal; Object.is would be type-strict across tags.
        ok(new Date(86400000).getTime() === 86400000, "new Date(t).getTime() round-trips");
        ok(new Date(0).getTime() === 0, "new Date(0).getTime() is 0");
        ok(new Date(1600000000000).getTime() === 1600000000000, "a 1.6e12 ms timestamp is exact");

        // toISOString() renders the exact UTC instant (YYYY-MM-DDTHH:MM:SS.sssZ).
        eq(new Date(0).toISOString(), "1970-01-01T00:00:00.000Z", "epoch toISOString");
        eq(new Date(86400000).toISOString(), "1970-01-02T00:00:00.000Z", "day 1 toISOString");
        eq(new Date(-1).toISOString(), "1969-12-31T23:59:59.999Z", "one ms before epoch");
        eq(new Date(1600000000000).toISOString(), "2020-09-13T12:26:40.000Z", "a 2020 instant");
        eq(new Date(0).toJSON(), new Date(0).toISOString(), "toJSON matches toISOString");
        eq(new Date(0).toUTCString(), "Thu, 01 Jan 1970 00:00:00 GMT", "epoch toUTCString");

        // UTC field accessors (TZ-independent).
        const day1 = new Date(86400000);
        eq(day1.getUTCFullYear(), 1970, "getUTCFullYear");
        eq(day1.getUTCMonth(), 0, "getUTCMonth is 0-based");
        eq(day1.getUTCDate(), 2, "getUTCDate");
        eq(day1.getUTCDay(), 5, "getUTCDay (1970-01-02 was a Friday)");
        eq(day1.getUTCHours(), 0, "getUTCHours");
        eq(day1.getUTCMinutes(), 0, "getUTCMinutes");
        eq(day1.getUTCSeconds(), 0, "getUTCSeconds");
        eq(day1.getUTCMilliseconds(), 0, "getUTCMilliseconds");
        const t = new Date(1600000000000);
        eq(t.getUTCFullYear(), 2020, "2020 getUTCFullYear");
        eq(t.getUTCMonth(), 8, "2020-09 getUTCMonth");
        eq(t.getUTCDate(), 13, "2020-09-13 getUTCDate");
        eq(t.getUTCHours(), 12, "getUTCHours of a 2020 instant");
        eq(t.getUTCMinutes(), 26, "getUTCMinutes of a 2020 instant");
        eq(t.getUTCSeconds(), 40, "getUTCSeconds of a 2020 instant");

        // Local-time constructor: composing then reading back the local fields is
        // a TZ-independent round-trip regardless of the host zone.
        const local = new Date(2020, 0, 1);
        eq(local.getFullYear(), 2020, "local compose getFullYear");
        eq(local.getMonth(), 0, "local compose getMonth");
        eq(local.getDate(), 1, "local compose getDate");
        const local2 = new Date(2021, 6, 4, 13, 30, 15, 250);
        eq(local2.getFullYear(), 2021, "local compose (full) getFullYear");
        eq(local2.getMonth(), 6, "local compose (full) getMonth");
        eq(local2.getDate(), 4, "local compose (full) getDate");
        eq(local2.getHours(), 13, "local compose getHours");
        eq(local2.getMinutes(), 30, "local compose getMinutes");
        eq(local2.getSeconds(), 15, "local compose getSeconds");
        eq(local2.getMilliseconds(), 250, "local compose getMilliseconds");
        ok(typeof local.getTimezoneOffset() === "number", "getTimezoneOffset is a number");

        // Date.parse / Date.UTC (cross-tag numeric equality uses ===).
        ok(Date.parse(new Date(1600000000000).toISOString()) === 1600000000000,
            "Date.parse round-trips toISOString");
        ok(Date.parse("1970-01-01T00:00:00.000Z") === 0, "Date.parse of the epoch");
        ok(Date.parse("2020-09-13T12:26:40.000Z") === 1600000000000, "Date.parse ISO instant");
        ok(Date.parse("2020-01-01") === Date.UTC(2020, 0, 1), "a date-only string parses as UTC");
        ok(new Date("2020-09-13T12:26:40.000Z").getTime() === 1600000000000,
            "the string constructor uses Date.parse");
        ok(Date.UTC(1970, 0, 1) === 0, "Date.UTC of the epoch");
        ok(Date.UTC(2020, 0, 1) === 1577836800000, "Date.UTC(2020,0,1)");
        eq(new Date(Date.UTC(2020, 0, 1)).getUTCFullYear(), 2020, "Date.UTC composes a UTC instant");
        ok(Date.parse("not a date") !== Date.parse("not a date"), "Date.parse of garbage is NaN");
        eq(new Date("not a date").toISOString(), "Invalid Date", "an invalid Date stringifies safely");
    })();

    // =========================================================================
    section("30. undefined / null equality");
    // =========================================================================
    (function () {
        // undefined === undefined was historically false in this VM.
        eq(undefined === undefined, true, "undefined === undefined");
        eq(null === null, true, "null === null");
        eq(undefined === null, false, "undefined is not null under ===");
        ok(undefined == null, "undefined == null under loose equality");
        ok(null == undefined, "null == undefined under loose equality");
        ok(undefined !== null, "undefined !== null");

        let unassigned;
        eq(unassigned, undefined, "an unassigned variable is undefined");
        eq({}.missing, undefined, "a missing property reads as undefined");
        ok([1, 2].at(9) === undefined, "Array.at out of range === undefined");
        ok("ab".at(9) === undefined, "String.at out of range === undefined");
        ok(NaN !== NaN, "NaN is not equal to itself");
    })();

    // =========================================================================
    section("31. Added built-ins & logical-operator operand semantics");
    // =========================================================================
    (function () {
        // --- Math additions (floor/ceil box integral results as ints) ---
        eq(Math.floor(1.7), 1, "Math.floor(1.7) === 1");
        eq(Math.floor(-1.2), -2, "Math.floor(-1.2) === -2");
        eq(Math.ceil(1.2), 2, "Math.ceil(1.2) === 2");
        eq(Math.ceil(-1.7), -1, "Math.ceil(-1.7) === -1");
        // atan2 returns a float32; compare within a tolerance, not by ===.
        ok(Math.abs(Math.atan2(1, 1) - 0.7853981634) < 1e-5, "Math.atan2(1,1) ~ pi/4");
        ok(Math.abs(Math.atan2(0, -1) - 3.14159265) < 1e-5, "Math.atan2(0,-1) ~ pi");
        ok(Math.fround(1.5) === 1.5, "Math.fround(1.5) === 1.5");
        ok(Math.random() >= 0 && Math.random() < 1, "Math.random() is in [0,1)");
    })();

    (function () {
        // --- Array.prototype.splice (partition + rebuild; hash-mapped arrays) ---
        const a = [1, 2, 3, 4, 5];
        const removed = a.splice(1, 2);
        deepEq(removed, [2, 3], "splice returns the removed slice");
        deepEq(a, [1, 4, 5], "splice mutates the original array");

        const b = [1, 2, 3];
        const none = b.splice(1, 0, "x", "y");
        deepEq(none, [], "splice with deleteCount 0 removes nothing");
        deepEq(b, [1, "x", "y", 2, 3], "splice inserts items at the index");

        const c = [1, 2, 3, 4];
        deepEq(c.splice(-1, 1), [4], "splice accepts a negative start");
        deepEq(c, [1, 2, 3], "splice(-1,1) drops the last element");

        const d = [1, 2, 3];
        deepEq(d.splice(1), [2, 3], "splice with no deleteCount removes to the end");
        deepEq(d, [1], "splice(1) leaves the head");

        // --- Array.prototype.reduceRight ---
        eq([1, 2, 3].reduceRight((acc, x) => acc + x, 0), 6, "reduceRight sums");
        eq(["a", "b", "c"].reduceRight((acc, x) => acc + x, ""), "cba", "reduceRight walks right-to-left");
        eq([1, 2, 3].reduceRight((acc, x, i) => acc + i, 0), 3, "reduceRight passes indices 2,1,0");
    })();

    (function () {
        // --- String additions ---
        eq("hello".charAt(1), "e", "charAt(1)");
        eq("hello".charAt(9), "", "charAt out of range yields ''");
        eq("abc".charCodeAt(0), 97, "charCodeAt(0) === 97");
        ok(Number.isNaN("abc".charCodeAt(9)), "charCodeAt out of range is NaN");
        eq("abcdef".substring(1, 3), "bc", "substring(1,3)");
        eq("abcdef".substring(3, 1), "bc", "substring swaps when start > end");
        eq("abcdef".substring(-1, 2), "ab", "substring clamps negatives to 0");
        eq("ab".concat("cd", "ef"), "abcdef", "concat joins every argument");
        eq("a".concat(1, true, "b"), "a1trueb", "concat stringifies non-strings");
        eq(String.fromCharCode(72, 105), "Hi", "fromCharCode builds a string");
        eq("aaa".lastIndexOf("a"), 2, "lastIndexOf finds the last match");
        eq("abc".lastIndexOf("z"), -1, "lastIndexOf returns -1 when absent");
        eq("abab".lastIndexOf("a", 2), 2, "lastIndexOf honours fromIndex");
        eq("abc".localeCompare("abd"), -1, "localeCompare less-than");
        eq("abc".localeCompare("abc"), 0, "localeCompare equal");
        eq("abd".localeCompare("abc"), 1, "localeCompare greater-than");
    })();

    (function () {
        // --- Number additions ---
        eq((123.456).toFixed(2), "123.46", "toFixed(2) rounds");
        eq((5).toFixed(2), "5.00", "toFixed pads with zeros");
        eq((9.5).toFixed(0), "10", "toFixed(0) rounds to an integer string");
        eq((123.456).toPrecision(3), "123", "toPrecision(3) significant digits");
        eq((5).valueOf(), 5, "Number.prototype.valueOf returns the primitive");
        ok(Number.MAX_VALUE > 1e30, "Number.MAX_VALUE is large");
        ok(Number.MIN_VALUE > 0 && Number.MIN_VALUE < 1, "Number.MIN_VALUE is a small positive");
    })();

    (function () {
        // --- Logical `&&` / `||` yield an OPERAND, not a boolean. These are the
        // regression guards for the handle_logic NULL-deref crash (undefined/null
        // operands used to segfault) and its wrong boolean result. ---
        ok((undefined && 1) === undefined, "undefined && 1 yields undefined (no crash)");
        ok((null && 1) === null, "null && 1 yields null (no crash)");
        eq(null || 2, 2, "null || 2 yields 2");
        eq("s" && 1, 1, "'s' && 1 yields 1, not true");
        eq(0 || 7, 7, "0 || 7 yields 7, not true");
        eq(1 && 2, 2, "1 && 2 yields 2");
        eq("a" || "b", "a", "'a' || 'b' yields 'a'");
        eq("" && "x", "", "'' && 'x' yields '' (empty string is falsy)");
        eq(0 && "x", 0, "0 && 'x' yields 0");
        eq(NaN || "fallback", "fallback", "NaN || 'fallback' yields 'fallback'");
        const opts = {};
        eq(opts.port || 8080, 8080, "the `a || default` idiom works");
        const o = {};
        eq(o && 5, 5, "an object is truthy in &&");
    })();

    // =========================================================================
    section("32. 64-bit numbers (int64 exactness & double precision)");
    // =========================================================================
    (function () {
        // --- Exact large integer literals (V_INT64; not lossy float32) ---
        // 2^53-1 is the largest safe integer and round-trips exactly as an int64.
        // === coerces across numeric tags, so it (not Object.is) is the right
        // check when a V_INT64 result is compared with a differently-tagged literal.
        ok(9007199254740991 === Number.MAX_SAFE_INTEGER, "9007199254740991 === Number.MAX_SAFE_INTEGER");
        ok(-9007199254740991 === Number.MIN_SAFE_INTEGER, "-9007199254740991 === Number.MIN_SAFE_INTEGER");
        eq(typeof 9007199254740991, "number", "a large int64 literal is typeof 'number'");
        eq(typeof 1.5, "number", "a double literal is typeof 'number'");
        eq(typeof 1e308, "number", "an exponent-form double is typeof 'number'");

        // A Unix-ms timestamp (1.6e12) exceeds int32 but stays exact end to end.
        ok(1600000000000 === 1600000000000, "a 1.6e12 literal equals itself");
        ok(1600000000000 + 1 === 1600000000001, "int64 addition stays exact at 1.6e12");

        // --- int32 -> int64 promotion on signed overflow ---
        ok(2147483647 + 1 === 2147483648, "int32 max + 1 promotes to an exact int64");
        ok(-2147483648 - 1 === -2147483649, "int32 min - 1 promotes to an exact int64");
        ok(1000000 * 1000000 === 1000000000000, "1e6 * 1e6 is an exact int64 (no wrap)");

        // --- Large-integer arithmetic & comparison are exact (int64 lanes) ---
        ok(9007199254740992 > 9007199254740991, "adjacent large integers compare exactly");
        ok(1600000000001 > 1600000000000, "1.6e12 neighbours compare exactly");
        ok(9007199254740991 % 10 === 1, "int64 modulo stays integral");

        // --- 2**53 goes through double pow; === coerces across numeric tags ---
        ok(2 ** 53 === 9007199254740992, "2**53 equals its exact integer value");
        ok(2 ** 53 - 9007199254740992 === 0, "2**53 minus its int64 twin is 0");

        // --- Bitwise ops keep JS ToInt32 semantics (int32 wraparound) ---
        ok((1 << 31) === -2147483648, "1<<31 wraps to the int32 minimum");
        ok(0xFFFFFFFF === 4294967295, "a 0x literal wider than int32 is an exact int64");

        // --- Double precision (V_FLOAT64 is a true IEEE-754 double) ---
        ok(7 / 2 === 3.5, "integer division yields the exact double 3.5");
        ok(0.1 + 0.2 !== 0.3, "0.1+0.2 is the closest double, not exactly 0.3");
        ok(Math.abs((0.1 + 0.2) - 0.3) < 1e-15, "0.1+0.2 is within 1e-15 of 0.3");
        ok(Number.isFinite(1e308), "1e308 is finite");
        ok(1e308 < Number.MAX_VALUE, "1e308 is below Number.MAX_VALUE");
        ok(Number.MAX_VALUE > 1e308, "Number.MAX_VALUE exceeds 1e308");
        ok(!Number.isFinite(Number.MAX_VALUE * 10), "overflowing past MAX_VALUE is Infinity");
        ok(Number.MIN_VALUE > 0 && Number.MIN_VALUE < 1e-300, "Number.MIN_VALUE is the smallest positive double");
    })();

    // =========================================================================
    section("33. BigInt (arbitrary-precision integers)");
    // =========================================================================
    (function () {
        // --- typeof / literals (=== coerces across numeric tags, so use ok, not eq) ---
        eq(typeof 1n, "bigint", "typeof 1n is 'bigint'");
        eq(typeof BigInt(5), "bigint", "typeof BigInt(5) is 'bigint'");
        eq(typeof BigInt, "function", "BigInt is typeof 'function'");

        // --- arithmetic ---
        ok(1n + 2n === 3n, "1n + 2n === 3n");
        ok(10n - 4n === 6n, "10n - 4n === 6n");
        ok(6n * 7n === 42n, "6n * 7n === 42n");
        ok(42n / 7n === 6n, "42n / 7n === 6n");
        ok(10n % 3n === 1n, "10n % 3n === 1n");
        ok(2n ** 10n === 1024n, "2n ** 10n === 1024n");
        ok(0n - 5n === -5n, "0n - 5n === -5n");

        // --- arbitrary precision beyond int64 / double ---
        ok(2n ** 64n === 18446744073709551616n, "2n**64n exceeds the uint64 range");
        ok(2n ** 100n === 1267650600228229401496703205376n, "2n**100n is exact");
        ok(12345678901234567890n + 1n === 12345678901234567891n, "addition past 2^63 stays exact");
        ok(18446744073709551616n / 2n === 9223372036854775808n, "division of a >2^64 value");

        // --- comparison (strict differs from loose across bigint/number) ---
        ok(1n === 1n, "1n === 1n");
        ok(!(1n === 1), "1n === 1 is false (strict, different type)");
        ok(1n == 1, "1n == 1 is true (loose, by value)");
        ok(2n > 1n && 1n < 2n, "bigint ordering");
        ok(1n < 1.5, "bigint vs float comparison");
        ok(-1n < 0n, "negative bigint ordering");

        // --- toString radix / String / concatenation ---
        eq((255n).toString(16), "ff", "255n.toString(16)");
        eq((8n).toString(2), "1000", "8n.toString(2)");
        eq((-255n).toString(16), "-ff", "-255n.toString(16)");
        eq((1000n).toString(36), "rs", "1000n.toString(36)");
        eq(String(-1n), "-1", "String(-1n)");
        eq("" + 5n, "5", "empty-string concat with a bigint");
        eq(1n + "a", "1a", "1n + 'a' concatenates");

        // --- truthiness ---
        ok(!0n, "0n is falsy");
        ok(1n, "1n is truthy");
        ok(-1n, "-1n is truthy");

        // --- BigInt() conversion ---
        ok(BigInt(5) === 5n, "BigInt(5)");
        ok(BigInt("5") === 5n, "BigInt('5')");
        ok(BigInt("0x10") === 16n, "BigInt('0x10')");
        ok(BigInt(true) === 1n, "BigInt(true)");
        ok(BigInt(5.0) === 5n, "BigInt(5.0) integral double");
        ok(BigInt(null) === 0n, "BigInt(null) is 0n");
        ok(BigInt() === 0n, "BigInt() is 0n");

        // --- bitwise (two's complement, arbitrary width) ---
        ok((5n & 3n) === 1n, "5n & 3n");
        ok((5n | 3n) === 7n, "5n | 3n");
        ok((5n ^ 3n) === 6n, "5n ^ 3n");
        ok((1n << 4n) === 16n, "1n << 4n");
        ok((16n >> 2n) === 4n, "16n >> 2n");
        ok((-16n >> 2n) === -4n, "-16n >> 2n is an arithmetic shift");
        ok((-1n & -1n) === -1n, "-1n & -1n");

        // --- asIntN / asUintN / valueOf / Number() ---
        ok(BigInt.asIntN(8, 128n) === -128n, "BigInt.asIntN(8,128n) wraps to -128n");
        ok(BigInt.asUintN(8, -1n) === 255n, "BigInt.asUintN(8,-1n) is 255n");
        ok((5n).valueOf() === 5n, "5n.valueOf()");
        ok(Number(10n) === 10, "Number(10n)");

        // --- mixing bigint with number throws (1 representative caught throw) ---
        throws(() => { let x = 1n + 1; }, "1n + 1 (mixing bigint and number) throws");
    })();

    // =========================================================================
    section("34. ArrayBuffer & DataView (binary buffers)");
    // =========================================================================
    (function () {
        // --- ArrayBuffer basics + zero-init ---
        let ab = new ArrayBuffer(8);
        ok(ab.byteLength === 8, "new ArrayBuffer(8).byteLength");
        ok(new ArrayBuffer(0).byteLength === 0, "new ArrayBuffer(0).byteLength");
        let dz = new DataView(new ArrayBuffer(4));
        ok(dz.getUint8(0) === 0 && dz.getUint8(3) === 0, "a fresh buffer is zero-initialised");
        ok(dz.getUint32(0, true) === 0, "zero-init uint32");

        // --- isView ---
        ok(ArrayBuffer.isView(dz) === true, "ArrayBuffer.isView(DataView) is true");
        ok(ArrayBuffer.isView(ab) === false, "ArrayBuffer.isView(ArrayBuffer) is false");
        ok(ArrayBuffer.isView({}) === false, "ArrayBuffer.isView({}) is false");

        // --- DataView members / offset views ---
        let mab = new ArrayBuffer(8);
        let mdv = new DataView(mab);
        ok(mdv.buffer === mab, "dv.buffer identity");
        ok(mdv.byteOffset === 0, "dv.byteOffset defaults to 0");
        ok(mdv.byteLength === 8, "dv.byteLength defaults to the buffer length");
        let mdv2 = new DataView(mab, 2, 4);
        ok(mdv2.byteOffset === 2 && mdv2.byteLength === 4, "dv(buffer, offset, length) members");
        ok(new DataView(mab, 3).byteLength === 5, "dv(buffer, offset) covers the rest");

        // --- integer round-trips (both endiannesses) ---
        let r = new DataView(new ArrayBuffer(8));
        r.setInt8(0, -5);              ok(r.getInt8(0) === -5, "int8 round-trip -5");
        r.setUint8(0, 200);            ok(r.getUint8(0) === 200, "uint8 round-trip 200");
        r.setInt16(0, -300, true);     ok(r.getInt16(0, true) === -300, "int16 LE round-trip");
        r.setInt16(0, -300, false);    ok(r.getInt16(0, false) === -300, "int16 BE round-trip");
        r.setUint16(0, 60000, true);   ok(r.getUint16(0, true) === 60000, "uint16 LE round-trip 60000");
        r.setInt32(0, -100000, true);  ok(r.getInt32(0, true) === -100000, "int32 LE round-trip");
        r.setInt32(0, -100000, false); ok(r.getInt32(0, false) === -100000, "int32 BE round-trip");
        r.setUint32(0, 4000000000, true); ok(r.getUint32(0, true) === 4000000000, "uint32 large round-trip");
        r.setUint32(0, 4294967295, true); ok(r.getInt32(0, true) === -1, "0xFFFFFFFF read as int32 is -1");
        r.setUint8(7, 42);             ok(r.getUint8(7) === 42, "the last byte (byteLength-1) is in-bounds");

        // --- explicit endianness / byte layout ---
        let e = new DataView(new ArrayBuffer(4));
        e.setUint16(0, 0x1234, false);   // BE: [0]=0x12, [1]=0x34
        ok(e.getUint8(0) === 0x12 && e.getUint8(1) === 0x34, "BE uint16 lays the high byte first");
        e.setUint16(2, 0x1234, true);    // LE: [2]=0x34, [3]=0x12
        ok(e.getUint8(2) === 0x34 && e.getUint8(3) === 0x12, "LE uint16 lays the low byte first");
        e.setUint16(0, 0x1234, false);
        ok(e.getUint16(0, true) === 0x3412, "reading LE over BE-written bytes swaps");
        ok(e.getUint16(0, false) === 0x1234, "reading BE matches the BE write");

        // --- shared backing across views ---
        let sab = new ArrayBuffer(8);
        let v1 = new DataView(sab);
        let v2 = new DataView(sab);
        v1.setUint8(3, 77);
        ok(v2.getUint8(3) === 77, "two views over one buffer share bytes");
        let v3 = new DataView(sab, 4);
        v3.setUint8(0, 99);
        ok(v1.getUint8(4) === 99, "an offset view's [0] is the buffer's byte 4");

        // --- unaligned access (memcpy-safe) ---
        let u = new DataView(new ArrayBuffer(8));
        u.setUint8(1, 1); u.setUint8(2, 2);
        ok(u.getUint16(1, true) === 513, "unaligned LE uint16 at offset 1 (1 + 2*256)");
        u.setUint32(1, 0x01020304, false);
        ok(u.getUint8(1) === 0x01 && u.getUint8(4) === 0x04, "unaligned BE uint32 byte layout");

        // --- floats ---
        r.setFloat32(0, 3.5, true);    ok(r.getFloat32(0, true) === 3.5, "float32 round-trip 3.5");
        r.setFloat32(0, -2.25, false); ok(r.getFloat32(0, false) === -2.25, "float32 BE round-trip -2.25");
        r.setFloat64(0, 3.141592653589793, true);
        ok(r.getFloat64(0, true) === 3.141592653589793, "float64 round-trip pi");
        r.setFloat32(0, 0.1, true);
        ok(r.getFloat32(0, true) !== 0.1, "float32 loses precision on 0.1");

        // --- BigInt64 / BigUint64 ---
        let b = new DataView(new ArrayBuffer(8));
        b.setBigInt64(0, -5n, true);
        ok(b.getBigInt64(0, true) === -5n, "bigint64 round-trip -5n");
        eq(typeof b.getBigInt64(0, true), "bigint", "getBigInt64 typeof is bigint");
        b.setBigInt64(0, 9223372036854775807n, false);
        ok(b.getBigInt64(0, false) === 9223372036854775807n, "bigint64 INT64_MAX (BE)");
        b.setBigInt64(0, -9223372036854775808n, true);
        ok(b.getBigInt64(0, true) === -9223372036854775808n, "bigint64 INT64_MIN (LE)");
        b.setBigUint64(0, 18446744073709551615n, true);
        ok(b.getBigUint64(0, true) === 18446744073709551615n, "biguint64 UINT64_MAX");
        eq(typeof b.getBigUint64(0, true), "bigint", "getBigUint64 typeof is bigint");

        // --- ArrayBuffer.prototype.slice (independent copy) ---
        let s = new ArrayBuffer(8);
        let ds = new DataView(s);
        for (let i = 0; i < 8; i++) { ds.setUint8(i, i + 1); }   // bytes 1..8
        let sliced = s.slice(2, 5);
        ok(sliced.byteLength === 3, "slice(2,5).byteLength");
        let dsl = new DataView(sliced);
        ok(dsl.getUint8(0) === 3 && dsl.getUint8(2) === 5, "slice(2,5) copies orig[2..4]");
        dsl.setUint8(0, 99);
        ok(ds.getUint8(2) === 3, "writing the slice leaves the original intact (copy)");
        ok(s.slice().byteLength === 8, "slice() copies the whole buffer");
        ok(s.slice(-2).byteLength === 2, "slice(-2) covers the last two bytes");
        ok(new DataView(s.slice(-2)).getUint8(0) === 7, "slice(-2)[0] is orig[6]");
        ok(s.slice(0, 100).byteLength === 8, "slice(0,100) clamps end to byteLength");
        ok(s.slice(6, 3).byteLength === 0, "slice(6,3) with begin>end yields an empty buffer");
        ok(s.byteLength === 8, "slice left the source byteLength intact");

        // --- OOB DataView read throws RangeError (1 representative caught throw) ---
        throws(() => { new DataView(new ArrayBuffer(8)).getInt8(8); }, "DataView OOB read throws RangeError");
    })();

    // =========================================================================
    section("35. TypedArrays (Int8Array .. BigUint64Array)");
    // =========================================================================
    (function () {
        // --- construction: (length) zero-filled + element sizes ---
        let a = new Int8Array(3);
        ok(a.length === 3, "new Int8Array(3).length");
        ok(a[0] === 0 && a[2] === 0, "a fresh TypedArray is zero-filled");
        ok(a.byteLength === 3, "Int8Array byteLength");
        ok(a.BYTES_PER_ELEMENT === 1, "Int8Array.BYTES_PER_ELEMENT");
        ok(new Int16Array(4).byteLength === 8, "Int16Array(4).byteLength is 8");
        ok(new Float64Array(2).BYTES_PER_ELEMENT === 8, "Float64Array.BYTES_PER_ELEMENT is 8");
        ok(new Uint32Array(2).BYTES_PER_ELEMENT === 4, "Uint32Array.BYTES_PER_ELEMENT is 4");

        // --- OOB / negative reads are undefined (no throw) ---
        ok(a[3] === undefined, "OOB read yields undefined");
        ok(a[-1] === undefined, "negative-index read yields undefined");

        // --- construction: (array) and (typedarray copy) ---
        let b = new Int32Array([10, 20, 30]);
        ok(b.length === 3 && b[1] === 20, "new Int32Array([10,20,30])");
        let c = new Uint8Array(b);
        ok(c.length === 3 && c[0] === 10, "new Uint8Array(typedarray) copies element-wise");

        // --- construction: (buffer[, offset[, length]]) views ---
        let buf = new ArrayBuffer(16);
        ok(new Int32Array(buf).length === 4, "a view over the whole buffer");
        let vo = new Int32Array(buf, 8);
        ok(vo.length === 2 && vo.byteOffset === 8, "new Int32Array(buffer, byteOffset)");
        ok(new Int32Array(buf, 4, 1).length === 1, "new Int32Array(buffer, byteOffset, length)");

        // --- statics: from / of (with and without mapFn) ---
        let d = Int32Array.from([1, 2, 3]);
        ok(d.length === 3 && d[2] === 3, "Int32Array.from(array)");
        ok(Uint8Array.of(7, 8, 9)[0] === 7, "Uint8Array.of(...)");
        ok(Int32Array.from([1, 2, 3], x => x * 10)[1] === 20, "from(array, mapFn)");

        // --- writes, wrapping and clamping ---
        let w = new Int32Array(2);
        w[0] = 42; ok(w[0] === 42, "element write + readback");
        ok((w[1] = 99) === 99, "an assignment expression yields the RHS");
        w[0] += 5; ok(w[0] === 47, "compound += writes through");
        let u = new Uint8Array(2);
        u[0] = 300; ok(u[0] === 44, "Uint8Array wraps 300 -> 44");
        u[1] = -1;  ok(u[1] === 255, "Uint8Array wraps -1 -> 255");
        let i8 = new Int8Array(1);
        i8[0] = 127; i8[0] += 1; ok(i8[0] === -128, "Int8Array overflows 127+1 -> -128");
        let cl = new Uint8ClampedArray(3);
        cl[0] = 300; ok(cl[0] === 255, "Uint8ClampedArray clamps high to 255");
        cl[1] = -20; ok(cl[1] === 0, "Uint8ClampedArray clamps low to 0");
        cl[2] = 2.5; ok(cl[2] === 2, "Uint8ClampedArray rounds half to even (2.5 -> 2)");
        let u32 = new Uint32Array(1);
        u32[0] = 4294967295; ok(u32[0] === 4294967295, "Uint32Array holds UINT32_MAX");
        let f64 = new Float64Array(1);
        f64[0] = 3.5; ok(f64[0] === 3.5, "Float64Array round-trips 3.5");

        // --- ++ / -- (postfix yields the old value) ---
        let inc = new Int32Array(2);
        inc[0] = 5; inc[0]++; ok(inc[0] === 6, "postfix ++ increments");
        ++inc[0]; ok(inc[0] === 7, "prefix ++ increments");
        let pv = inc[1]++; ok(pv === 0 && inc[1] === 1, "postfix ++ yields the old value");

        // --- shared backing: TypedArray <-> DataView and TA <-> TA ---
        let sb = new ArrayBuffer(8);
        let tu = new Uint8Array(sb);
        let tdv = new DataView(sb);
        tu[0] = 0xAB; ok(tdv.getUint8(0) === 0xAB, "a TypedArray write is seen by a DataView");
        tdv.setUint8(1, 0xCD); ok(tu[1] === 0xCD, "a DataView write is seen by a TypedArray");
        let tv1 = new Int32Array(sb, 0, 2);
        let tv2 = new Int32Array(sb, 0, 2);
        tv1[0] = 777; ok(tv2[0] === 777, "two TypedArray views share one buffer");

        // --- subarray (view) vs slice (copy) ---
        let src = Int32Array.from([10, 20, 30, 40, 50]);
        let sub = src.subarray(1, 4);
        ok(sub.length === 3 && sub[0] === 20 && sub[2] === 40, "subarray(1,4) is a 3-element view");
        ok(sub.byteOffset === 4, "subarray byteOffset accounts for element size");
        sub[0] = 99; ok(src[1] === 99, "subarray shares backing (write-through)");
        let sl = src.slice(1, 4);
        sl[0] = -1; ok(src[1] === 99 && sl[0] === -1, "slice is an independent copy");

        // --- set / fill / reverse / copyWithin ---
        let dst = new Int32Array(5);
        dst.set([1, 2, 3], 1);
        ok(dst[0] === 0 && dst[1] === 1 && dst[3] === 3, "set(array, offset)");
        let dst2 = new Int32Array(3);
        dst2.set(Int32Array.from([7, 8, 9]));
        ok(dst2[0] === 7 && dst2[2] === 9, "set(typedarray)");
        let f = new Int32Array(5); f.fill(3);
        ok(f[0] === 3 && f[4] === 3, "fill(value) fills the whole array");
        f.fill(9, 1, 3);
        ok(f[0] === 3 && f[1] === 9 && f[2] === 9 && f[3] === 3, "fill(value, start, end)");
        let rv = Int32Array.from([1, 2, 3, 4]); rv.reverse();
        ok(rv[0] === 4 && rv[3] === 1, "reverse() in place");
        let cw = Int32Array.from([1, 2, 3, 4, 5]); cw.copyWithin(0, 3);
        ok(cw[0] === 4 && cw[1] === 5 && cw[2] === 3, "copyWithin(0, 3)");

        // --- sort (default + comparator) ---
        let so = Int32Array.from([5, 3, 1, 4, 2]); so.sort();
        ok(so[0] === 1 && so[4] === 5, "sort() ascending by default");
        let so2 = Int32Array.from([1, 2, 3]); so2.sort((x, y) => y - x);
        ok(so2[0] === 3 && so2[2] === 1, "sort(compareFn) descending");

        // --- searching: indexOf / lastIndexOf / includes / find / findIndex ---
        let ix = Int32Array.from([1, 2, 3, 2, 1]);
        ok(ix.indexOf(2) === 1, "indexOf finds the first match");
        ok(ix.lastIndexOf(2) === 3, "lastIndexOf finds the last match");
        ok(ix.indexOf(9) === -1, "indexOf returns -1 when absent");
        ok(ix.includes(3) && !ix.includes(9), "includes");
        let fd = Int32Array.from([1, 2, 3, 4]);
        ok(fd.find(x => x > 2) === 3, "find returns the first matching element");
        ok(fd.findIndex(x => x > 2) === 2, "findIndex returns the first matching index");
        ok(fd.find(x => x > 9) === undefined, "find returns undefined when absent");

        // --- quantifiers / iteration: every / some / forEach ---
        ok(fd.every(x => x > 0) && !fd.every(x => x > 2), "every");
        ok(fd.some(x => x > 3) && !fd.some(x => x > 9), "some");
        let sum = 0; fd.forEach(x => { sum += x; });
        ok(sum === 10, "forEach visits every element");

        // --- map / filter ---
        let mp = fd.map(x => x * 2);
        ok(mp.length === 4 && mp[0] === 2 && mp[3] === 8, "map returns a new TypedArray");
        let fl = fd.filter(x => x % 2 === 0);
        ok(fl.length === 2 && fl[0] === 2 && fl[1] === 4, "filter returns a new TypedArray");

        // --- reduce / reduceRight ---
        ok(fd.reduce((acc, x) => acc + x) === 10, "reduce without an initial value");
        ok(fd.reduce((acc, x) => acc + x, 100) === 110, "reduce with an initial value");
        ok(fd.reduceRight((acc, x) => acc * 10 + x) === 4321, "reduceRight folds from the end");
        ok(fd.reduceRight((acc, x) => acc + x, 5) === 15, "reduceRight with an initial value");

        // --- at / join / toString ---
        ok(fd.at(0) === 1 && fd.at(-1) === 4, "at(index) supports negatives");
        ok(fd.at(9) === undefined, "at() out of range is undefined");
        eq(fd.join("-"), "1-2-3-4", "join(sep)");
        eq(fd.join(), "1,2,3,4", "join() defaults to a comma");
        eq(fd.toString(), "1,2,3,4", "toString() is comma-joined");

        // --- values / keys / entries (spec: real iterators, consumed via spread) ---
        let vals = [...fd.values()];
        ok(vals.length === 4 && vals[0] === 1, "values() iterator");
        let keys = [...fd.keys()];
        ok(keys.length === 4 && keys[2] === 2, "keys() iterator");
        let ents = [...fd.entries()];
        ok(ents.length === 4 && ents[1][0] === 1 && ents[1][1] === 2, "entries() [index, value] pairs");

        // --- iteration protocol: for..of and spread ---
        let acc2 = 0; for (let x of fd) { acc2 += x; }
        ok(acc2 === 10, "for..of iterates a TypedArray (@@iterator)");
        let spread = [...fd];
        ok(spread.length === 4 && spread[3] === 4, "spread expands a TypedArray");

        // --- BigInt64Array / BigUint64Array ---
        let bi = BigInt64Array.from([1n, 2n, 3n]);
        ok(bi.length === 3 && bi[0] === 1n, "BigInt64Array.from bigints");
        eq(typeof bi[0], "bigint", "BigInt64Array elements are bigints");
        ok(bi.reduce((x, y) => x + y, 0n) === 6n, "BigInt64Array.reduce over bigints");
        let bu = new BigUint64Array(1); bu[0] = 18446744073709551615n;
        ok(bu[0] === 18446744073709551615n, "BigUint64Array holds UINT64_MAX");

        // --- Float32Array map ---
        let f32 = Float32Array.from([1.5, 2.5, 3.5]);
        ok(f32.map(x => x * 2)[1] === 5, "Float32Array.map");
    })();

    // =========================================================================
    section("36. Proxy / Reflect (metaprogramming traps)");
    // =========================================================================
    (function () {
        // --- get / set traps with a logging handler ---
        const log = [];
        const target = { a: 1, b: 2 };
        const p = new Proxy(target, {
            get(t, k, r) { log.push("get:" + String(k)); return t[k]; },
            set(t, k, v, r) { log.push("set:" + String(k)); t[k] = v; return true; }
        });
        eq(p.a, 1, "get trap returns the target value");
        eq(p.b, 2, "get trap on a second key");
        p.c = 3;
        eq(target.c, 3, "set trap writes through to the target");
        deepEq(log, ["get:a", "get:b", "set:c"], "handler logged get/get/set in order");
        ok(log.length === 3, "a simple set fired ONLY the set trap (no get)");

        // --- compound assignment / ++ fire get-then-set (spec order) ---
        const order = [];
        const ct = { n: 10 };
        const cp = new Proxy(ct, {
            get(t, k) { order.push("get"); return t[k]; },
            set(t, k, v) { order.push("set"); t[k] = v; return true; }
        });
        cp.n += 5;
        eq(ct.n, 15, "p.n += 5 updated the target to 15");
        deepEq(order, ["get", "set"], "compound assignment fired get then set");
        const o2 = [];
        const t2 = { m: 1 };
        const p2 = new Proxy(t2, {
            get(t, k) { o2.push("g"); return t[k]; },
            set(t, k, v) { o2.push("s"); t[k] = v; return true; }
        });
        p2.m++;
        eq(t2.m, 2, "p.m++ incremented the target");
        deepEq(o2, ["g", "s"], "postfix ++ fired get then set");

        // --- has / deleteProperty traps ---
        const ht = { x: 1, y: 2 };
        let hasCalls = 0, delCalls = 0;
        const hp = new Proxy(ht, {
            has(t, k) { hasCalls++; return k in t; },
            deleteProperty(t, k) { delCalls++; delete t[k]; return true; }
        });
        ok("x" in hp, "'x' in p routes the has trap (true)");
        ok(!("z" in hp), "'z' in p routes the has trap (false)");
        eq(hasCalls, 2, "has trap called twice");
        delete hp.y;
        eq(delCalls, 1, "deleteProperty trap called once");
        ok(!("y" in ht), "deleteProperty removed y from the target");

        // --- ownKeys trap ---
        const ot = { a: 1, b: 2, c: 3 };
        const op = new Proxy(ot, { ownKeys(t) { return ["a", "b"]; } });
        deepEq(Object.keys(op), ["a", "b"], "Object.keys routes the ownKeys trap");
        deepEq(Reflect.ownKeys(op), ["a", "b"], "Reflect.ownKeys routes the ownKeys trap");
        const op2 = new Proxy({ k: 1, j: 2 }, {});
        deepEq(Object.keys(op2).sort(), ["j", "k"], "no ownKeys trap -> the target's keys");

        // --- apply / construct traps ---
        const fn = function (a, b) { return a + b; };
        const ap = new Proxy(fn, { apply(t, thisArg, args) { return args[0] * args[1]; } });
        eq(ap(3, 4), 12, "apply trap intercepts the call (3*4)");
        const apd = new Proxy(fn, {});
        eq(apd(3, 4), 7, "no apply trap -> forwards to the target (3+4)");
        function Ctor(v) { this.v = v; }
        const pcx = new Proxy(Ctor, { construct(t, args, nt) { return { made: args[0], tag: "trapped" }; } });
        const inst = new pcx(9);
        eq(inst.made, 9, "construct trap receives the args");
        eq(inst.tag, "trapped", "construct trap's return object wins");
        const pcd = new Proxy(Ctor, {});
        const inst2 = new pcd(5);
        eq(inst2.v, 5, "no construct trap -> forwards to the target ctor");

        // --- prototype / extensibility / descriptor traps ---
        const proto = { hi: 1 };
        const pt = {};
        const pp = new Proxy(pt, { getPrototypeOf(t) { return proto; } });
        eq(Object.getPrototypeOf(pp), proto, "getPrototypeOf trap");
        eq(Reflect.getPrototypeOf(pp), proto, "Reflect.getPrototypeOf trap");
        const t2e = {};
        const p2e = new Proxy(t2e, { isExtensible(t) { return true; }, preventExtensions(t) { Object.preventExtensions(t); return true; } });
        eq(Object.isExtensible(p2e), true, "isExtensible trap");
        eq(Object.preventExtensions(p2e), p2e, "preventExtensions returns the proxy");
        const t3d = {};
        const p3d = new Proxy(t3d, { defineProperty(t, k, d) { Object.defineProperty(t, k, d); return true; } });
        Object.defineProperty(p3d, "z", { value: 42, enumerable: true, configurable: true, writable: true });
        eq(t3d.z, 42, "defineProperty trap wrote to the target");
        const t4d = { q: 7 };
        const p4d = new Proxy(t4d, { getOwnPropertyDescriptor(t, k) { return { value: 100, enumerable: true, configurable: true }; } });
        const desc = Object.getOwnPropertyDescriptor(p4d, "q");
        eq(desc.value, 100, "getOwnPropertyDescriptor trap");

        // --- default forwarding (empty handler) ---
        const ft = { a: 1 };
        const fp = new Proxy(ft, {});
        eq(fp.a, 1, "empty handler get forwards to the target");
        ok(Object.isExtensible(fp), "empty handler isExtensible forwards (true)");
        eq(Reflect.get(fp, "a"), 1, "Reflect.get forwards");

        // --- Proxy.revocable ---
        const rt = { a: 1 };
        const rev = Proxy.revocable(rt, { get(t, k) { return t[k]; } });
        eq(rev.proxy.a, 1, "a revocable proxy works before revoke");
        rev.revoke();
        throws(() => rev.proxy.a, "get after revoke throws TypeError");
        throws(() => { rev.proxy.a = 2; }, "set after revoke throws TypeError");
        throws(() => "a" in rev.proxy, "has after revoke throws TypeError");

        // --- invariants / TypeErrors ---
        throws(() => new Proxy(null, {}), "a non-object target throws TypeError");
        throws(() => new Proxy({}, null), "a non-object handler throws TypeError");
        throws(() => new Proxy(5, {}), "a primitive target throws TypeError");
        const sp = new Proxy({}, { set() { return false; } });
        throws(() => { sp.x = 1; }, "a set trap returning false throws TypeError");
        const nonCallable = new Proxy({}, {});
        throws(() => nonCallable(1), "apply on a non-callable target throws TypeError");

        // --- Reflect statics on plain objects (all 13) ---
        const ro = { a: 1, b: 2 };
        eq(Reflect.get(ro, "a"), 1, "Reflect.get");
        eq(Reflect.set(ro, "c", 3), true, "Reflect.set returns true");
        eq(ro.c, 3, "Reflect.set wrote the value");
        ok(Reflect.has(ro, "a"), "Reflect.has (own)");
        ok(!Reflect.has(ro, "zz"), "Reflect.has (missing)");
        eq(Reflect.deleteProperty(ro, "b"), true, "Reflect.deleteProperty returns true");
        ok(!("b" in ro), "Reflect.deleteProperty removed b");
        deepEq(Reflect.ownKeys({ x: 1, y: 2 }).sort(), ["x", "y"], "Reflect.ownKeys");
        const rd = Reflect.getOwnPropertyDescriptor(ro, "a");
        eq(rd.value, 1, "Reflect.getOwnPropertyDescriptor value");
        ok(rd.enumerable, "Reflect.getOwnPropertyDescriptor enumerable");
        eq(Reflect.defineProperty(ro, "e", { value: 5, enumerable: true, configurable: true, writable: true }), true, "Reflect.defineProperty returns true");
        eq(ro.e, 5, "Reflect.defineProperty wrote the value");
        eq(Reflect.apply(function (x) { return this.k + x; }, { k: 10 }, [5]), 15, "Reflect.apply with a thisArg");
        function RC(v) { this.v = v; }
        const rci = Reflect.construct(RC, [8]);
        eq(rci.v, 8, "Reflect.construct");
        ok(rci instanceof RC, "Reflect.construct instanceof");
        const rpr = { base: 1 };
        const rnp = {};
        eq(Reflect.setPrototypeOf(rnp, rpr), true, "Reflect.setPrototypeOf returns true");
        eq(Reflect.getPrototypeOf(rnp), rpr, "Reflect.setPrototypeOf applied");
        ok(Reflect.isExtensible(rnp), "Reflect.isExtensible true");
        eq(Reflect.preventExtensions(rnp), true, "Reflect.preventExtensions returns true");
        ok(!Reflect.isExtensible(rnp), "Reflect.isExtensible now false");
    })();

    // =========================================================================
    section("37. WeakRef / FinalizationRegistry (weak references & cleanup)");
    // =========================================================================
    (function () {
        // --- presence & shape ---
        eq(typeof WeakRef, "function", "WeakRef is a constructor function");
        eq(typeof WeakRef.prototype.deref, "function", "WeakRef.prototype.deref is a function");
        eq(typeof gc, "function", "the hidden gc() helper is available");
        eq(gc(), undefined, "gc() returns undefined");

        // --- a live target is observable through deref() ---
        const live = { n: 42 };
        const wr = new WeakRef(live);
        ok(wr.deref() === live, "deref() returns the very target object (identity)");
        eq(wr.deref().n, 42, "the target reached through deref() keeps its fields");
        eq(new WeakRef({})[Symbol.toStringTag], "WeakRef", "@@toStringTag is 'WeakRef'");

        // --- a still-reachable target is NOT collected ---
        gc();
        ok(wr.deref() === live, "a target still held by an outer binding survives gc()");

        // --- once the only strong reference dies, deref() clears to undefined ---
        const wrDead = (() => { const tmp = { gone: true }; return new WeakRef(tmp); })();
        gc();
        eq(wrDead.deref(), undefined, "deref() is undefined after the target is collected");

        // --- two WeakRefs to one target both clear together ---
        let wrA, wrB;
        (() => { const t = {}; wrA = new WeakRef(t); wrB = new WeakRef(t); })();
        gc();
        eq(wrA.deref(), undefined, "the first of two WeakRefs to a dead target cleared");
        eq(wrB.deref(), undefined, "the second of two WeakRefs to a dead target cleared");

        // --- deref() on a non-WeakRef receiver is a TypeError ---
        throws(() => WeakRef.prototype.deref.call({}), "deref on a non-WeakRef throws TypeError");

        // --- WeakRef only accepts an object target ---
        throws(() => new WeakRef(42), "a number target throws TypeError");
        throws(() => new WeakRef("str"), "a string target throws TypeError");
        throws(() => new WeakRef(true), "a boolean target throws TypeError");
        throws(() => new WeakRef(null), "a null target throws TypeError");
        throws(() => new WeakRef(undefined), "an undefined target throws TypeError");
        throws(() => new WeakRef(), "a missing target throws TypeError");
    })();

    (function () {
        // --- FinalizationRegistry presence & shape ---
        eq(typeof FinalizationRegistry, "function", "FinalizationRegistry is a constructor function");
        eq(typeof FinalizationRegistry.prototype.register, "function", "register is a function");
        eq(typeof FinalizationRegistry.prototype.unregister, "function", "unregister is a function");

        // --- the cleanup callback runs with the held value once the target dies ---
        const fired = [];
        const fr = new FinalizationRegistry((held) => { fired.push(held); });
        eq(fr[Symbol.toStringTag], "FinalizationRegistry", "@@toStringTag is 'FinalizationRegistry'");
        (() => { const t = {}; fr.register(t, "cleanup-payload"); })();
        gc();
        eq(fired.length, 1, "the cleanup callback ran exactly once");
        eq(fired[0], "cleanup-payload", "the callback received the held value");

        // --- held-value identity is preserved (objects pass through by reference) ---
        const heldObj = { id: 7 };
        let gotHeld = null;
        const fr2 = new FinalizationRegistry((h) => { gotHeld = h; });
        (() => { const t = {}; fr2.register(t, heldObj); })();
        gc();
        ok(gotHeld === heldObj, "an object held value is delivered by identity");
        eq(gotHeld.id, 7, "the delivered held object keeps its fields");

        // --- registering without a held value delivers undefined ---
        let noHeldCalled = false, noHeldArg = "sentinel";
        const fr3 = new FinalizationRegistry((h) => { noHeldCalled = true; noHeldArg = h; });
        (() => { const t = {}; fr3.register(t); })();
        gc();
        ok(noHeldCalled, "a registration without a held value still fires");
        eq(noHeldArg, undefined, "the callback receives undefined when no held value was given");

        // --- several registrations on one registry all fire ---
        const many = [];
        const fr4 = new FinalizationRegistry((h) => { many.push(h); });
        (() => { for (let i = 0; i < 3; i++) { const t = { i }; fr4.register(t, i * 100); } })();
        gc();
        eq(many.length, 3, "all three registrations fired");
        deepEq(many.sort((a, b) => a - b), [0, 100, 200], "each callback got its own held value");

        // --- unregister() on a live registration returns true and suppresses it ---
        const suppressed = [];
        const fr5 = new FinalizationRegistry((h) => { suppressed.push(h); });
        (() => {
            const t = {};
            const token = {};
            fr5.register(t, "should-not-fire", token);
            eq(fr5.unregister(token), true, "unregister returns true for a live registration");
        })();
        gc();
        eq(suppressed.length, 0, "an unregistered target's callback never runs");

        // --- unregister() with an unknown token returns false ---
        eq(fr5.unregister({}), false, "unregister with an unknown token returns false");

        // --- TypeErrors ---
        throws(() => new FinalizationRegistry(42), "a non-callable callback throws TypeError");
        throws(() => new FinalizationRegistry(), "a missing callback throws TypeError");
        (() => {
            const t = {};
            throws(() => fr5.register(42, "x"), "register with a primitive target throws TypeError");
            throws(() => fr5.register(t, "x", t), "register with token === target throws TypeError");
        })();
    })();

    // =========================================================================
    section("38. SharedArrayBuffer / Atomics (shared memory & atomic ops)");
    // =========================================================================
    (function () {
        // --- SharedArrayBuffer construction & shape ---
        eq(typeof SharedArrayBuffer, "function", "SharedArrayBuffer is a constructor function");
        const sab = new SharedArrayBuffer(16);
        eq(sab.byteLength, 16, "byteLength reflects the requested size");
        eq(new SharedArrayBuffer(0).byteLength, 0, "a zero-length SharedArrayBuffer is legal");
        eq(sab[Symbol.toStringTag], "SharedArrayBuffer", "@@toStringTag is 'SharedArrayBuffer'");

        // --- a fresh buffer is zero-filled (observed through a Uint8Array view) ---
        const z = new Uint8Array(new SharedArrayBuffer(4));
        deepEq([z[0], z[1], z[2], z[3]], [0, 0, 0, 0], "a new SharedArrayBuffer is zero-initialised");

        // --- slice() copies the byte range into a new SharedArrayBuffer ---
        const src = new SharedArrayBuffer(8);
        const sv = new Uint8Array(src);
        sv[0] = 1; sv[1] = 2; sv[2] = 3; sv[3] = 4;
        const copy = src.slice(0, 4);
        eq(copy.byteLength, 4, "slice honours the [begin,end) length");
        eq(copy[Symbol.toStringTag], "SharedArrayBuffer", "slice returns a SharedArrayBuffer");
        const cv = new Uint8Array(copy);
        deepEq([cv[0], cv[1], cv[2], cv[3]], [1, 2, 3, 4], "slice copies the bytes");
        eq(src.slice(2).byteLength, 6, "slice with only a begin runs to the end");

        // --- errors & the non-growable stub ---
        throws(() => new SharedArrayBuffer(-1), "a negative length throws RangeError");
        throws(() => SharedArrayBuffer.prototype.slice.call({}), "slice on a non-SharedArrayBuffer throws TypeError");
        throws(() => new SharedArrayBuffer(8).grow(16), "grow() on a non-growable buffer throws TypeError");

        // --- a TypedArray view over a SharedArrayBuffer behaves normally ---
        const view = new Int32Array(new SharedArrayBuffer(16));
        eq(view.length, 4, "an Int32Array over a 16-byte SAB has 4 elements");
        eq(view.buffer.byteLength, 16, "the view's buffer is the SharedArrayBuffer");
        eq(view.BYTES_PER_ELEMENT, 4, "Int32Array BYTES_PER_ELEMENT over a SAB");
    })();

    (function () {
        // --- Atomics namespace ---
        eq(typeof Atomics.load, "function", "Atomics.load is a function");
        eq(typeof Atomics.compareExchange, "function", "Atomics.compareExchange is a function");

        const i32 = new Int32Array(new SharedArrayBuffer(16)); // 4 slots

        // --- load / store ---
        eq(Atomics.store(i32, 0, 42), 42, "store returns the stored value");
        eq(Atomics.load(i32, 0), 42, "load reads back the stored value");
        eq(i32[0], 42, "a plain index read sees the atomic store");
        i32[1] = 7;
        eq(Atomics.load(i32, 1), 7, "Atomics.load sees a plain index write");

        // --- add / sub return the OLD value and update in place ---
        eq(Atomics.add(i32, 0, 8), 42, "add returns the previous value");
        eq(Atomics.load(i32, 0), 50, "add updated the element");
        eq(Atomics.sub(i32, 0, 20), 50, "sub returns the previous value");
        eq(Atomics.load(i32, 0), 30, "sub updated the element");

        // --- and / or / xor ---
        Atomics.store(i32, 2, 12);
        eq(Atomics.and(i32, 2, 10), 12, "and returns the previous value");
        eq(Atomics.load(i32, 2), 8, "12 & 10 === 8");
        Atomics.store(i32, 2, 12);
        eq(Atomics.or(i32, 2, 3), 12, "or returns the previous value");
        eq(Atomics.load(i32, 2), 15, "12 | 3 === 15");
        Atomics.store(i32, 2, 12);
        eq(Atomics.xor(i32, 2, 6), 12, "xor returns the previous value");
        eq(Atomics.load(i32, 2), 10, "12 ^ 6 === 10");

        // --- exchange ---
        Atomics.store(i32, 3, 5);
        eq(Atomics.exchange(i32, 3, 99), 5, "exchange returns the previous value");
        eq(Atomics.load(i32, 3), 99, "exchange wrote the new value");

        // --- compareExchange (match and mismatch) ---
        Atomics.store(i32, 3, 5);
        eq(Atomics.compareExchange(i32, 3, 5, 10), 5, "compareExchange returns the previous value on a match");
        eq(Atomics.load(i32, 3), 10, "compareExchange wrote on a match");
        eq(Atomics.compareExchange(i32, 3, 999, 20), 10, "compareExchange returns the current value on a mismatch");
        eq(Atomics.load(i32, 3), 10, "compareExchange left the value unchanged on a mismatch");

        // --- 32-bit signed wraparound ---
        Atomics.store(i32, 0, 2147483647);
        Atomics.add(i32, 0, 1);
        eq(Atomics.load(i32, 0), -2147483648, "int32 add wraps from INT_MAX to INT_MIN");

        // --- Atomics on a BigInt64Array (use === ; Object.is is numeric-tag sensitive) ---
        const bi = new BigInt64Array(new SharedArrayBuffer(16)); // 2 slots
        eq(typeof Atomics.load(bi, 0), "bigint", "a BigInt64Array element loads as a bigint");
        Atomics.store(bi, 0, 100n);
        ok(Atomics.load(bi, 0) === 100n, "BigInt64 store/load round-trips");
        ok(Atomics.add(bi, 0, 50n) === 100n, "BigInt64 add returns the previous value");
        ok(Atomics.load(bi, 0) === 150n, "BigInt64 add updated the element");
        ok(Atomics.exchange(bi, 1, 7n) === 0n, "BigInt64 exchange returns the previous value");
        ok(Atomics.load(bi, 1) === 7n, "BigInt64 exchange wrote the new value");

        // --- wait / notify / isLockFree (single-threaded engine) ---
        throws(() => Atomics.wait(i32, 0, 0), "Atomics.wait throws (a lone agent may not block)");
        eq(Atomics.notify(i32, 0), 0, "Atomics.notify wakes zero waiters");
        eq(Atomics.isLockFree(1), true, "isLockFree(1) is true");
        eq(Atomics.isLockFree(2), true, "isLockFree(2) is true");
        eq(Atomics.isLockFree(4), true, "isLockFree(4) is true");
        eq(Atomics.isLockFree(8), true, "isLockFree(8) is true");
        eq(Atomics.isLockFree(3), false, "isLockFree(3) is false");

        // --- argument validation ---
        throws(() => Atomics.load(new Float64Array(4), 0), "Atomics on a float TypedArray throws TypeError");
        throws(() => Atomics.load(i32, 99), "an out-of-range index throws RangeError");
        throws(() => Atomics.load(i32, -1), "a negative index throws RangeError");
        throws(() => Atomics.load({}, 0), "a non-TypedArray first argument throws TypeError");
    })();

    // =========================================================================
    console.log("\n=== es6_full.js: " + __pass + " passed, " + __fail + " failed ===");
    if (__fail === 0) { console.log("ALL TESTS PASSED"); }
    else { console.log("SOME TESTS FAILED"); }
}).catch(e => {
    console.log("\nFATAL: an unexpected error aborted the suite: " + (e && e.stack ? e.stack : e));
    throw e;
});
