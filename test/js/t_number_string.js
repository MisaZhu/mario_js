/* ES2015 StringNumericLiteral (7.1.4.1.1) for Number(str) / unary + / ToNumber.
 * core-js gates its global Number wrapper on `!Number(" 0o1") || !Number("0b1")
 * || Number("+0x1")`; when those mis-parse it replaces global Number with a JS
 * wrapper whose constructed instances lose the engine's boxed-value slot, so
 * `new Number(x).valueOf()` reads 0 and CryptoJS-style hex digest encoding
 * (nibble .toString(16) over a boxed path) collapses to all zeros. */
var fail = 0;
function eq(got, want, label) {
    var g = String(got), w = String(want);
    if (g !== w) { fail++; console.log("FAIL " + label + ": got " + g + " want " + w); }
}
function eqn(got, want, label) {   /* NaN-aware compare */
    if (isNaN(got) && isNaN(want)) return;
    if (got === want) return;
    fail++; console.log("FAIL " + label + ": got " + got + " want " + want);
}
/* unsigned radix literal forms */
eqn(Number("0b1"), 1, "Number 0b1");
eqn(Number("0b101"), 5, "Number 0b101");
eqn(Number("0B101"), 5, "Number 0B101");
eqn(Number("0o1"), 1, "Number 0o1");
eqn(Number(" 0o1"), 1, "Number space 0o1");
eqn(Number("0o17"), 15, "Number 0o17");
eqn(Number("0O17"), 15, "Number 0O17");
eqn(Number("0x10"), 16, "Number 0x10");
eqn(Number(" 0x10 "), 16, "Number ws 0x10");
/* signed radix literals are NaN (grammar is unsigned-only) */
eqn(Number("+0x1"), NaN, "Number +0x1 NaN");
eqn(Number("-0x1"), NaN, "Number -0x1 NaN");
eqn(Number("+0b1"), NaN, "Number +0b1 NaN");
eqn(Number("-0o7"), NaN, "Number -0o7 NaN");
/* bad digits / trailing garbage are NaN */
eqn(Number("0o8"), NaN, "Number 0o8 NaN");
eqn(Number("0b2"), NaN, "Number 0b2 NaN");
eqn(Number("0x"), NaN, "Number 0x NaN");
eqn(Number("12abc"), NaN, "Number 12abc NaN");
eqn(Number("1_0"), NaN, "Number 1_0 NaN");
/* decimal forms unchanged */
eqn(Number(""), 0, "Number empty");
eqn(Number("   "), 0, "Number ws");
eqn(Number("010"), 10, "Number legacy 010");
eqn(Number("1e3"), 1000, "Number 1e3");
eqn(Number(".5"), 0.5, "Number .5");
eqn(Number("5."), 5, "Number 5.");
eqn(Number("Infinity"), Infinity, "Number Infinity");
eqn(Number("-Infinity"), -Infinity, "Number -Infinity");
eqn(Number("-12.5"), -12.5, "Number -12.5");
/* unary + shares the grammar */
eqn(+"0b101", 5, "unary + 0b101");
eqn(+"0o17", 15, "unary + 0o17");
eqn(+"+0x1", NaN, "unary + +0x1 NaN");
/* parseInt/parseFloat keep C-like prefix parsing */
eqn(parseFloat("12abc"), 12, "parseFloat 12abc");
eqn(parseInt("0x10"), 16, "parseInt 0x10");
/* the core-js wrapper gate must stay falsy so global Number stays native */
eq(!!(!Number(" 0o1") || !Number("0b1") || Number("+0x1")), false, "core-js Number gate");
/* boxed Number keeps its value (the symptom the gate used to cause) */
eq(new Number(255).valueOf(), 255, "boxed valueOf");
eq(new Number(255).toString(16), "ff", "boxed toString(16)");
console.log(fail === 0 ? "__NUMBER_STRING_OK__" : ("__NUMBER_STRING_FAIL__ " + fail));
