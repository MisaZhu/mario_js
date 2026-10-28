// Boxed Number regression: `new Number(x)` must behave like x in string/numeric
// contexts even though mario's construction path keeps the fresh object (the
// constructor's primitive return is discarded). The value lives in a hidden @@v
// slot stamped onto the instance; prototype methods fall back to it.
var fails = 0, passes = 0;
function ok(name, cond) {
	if(cond) { passes++; }
	else { fails++; console.log("FAIL " + name); }
}
var n = new Number(5);
ok("typeof object", typeof n === "object");
ok("String()", String(n) === "5");
ok("toString()", n.toString() === "5");
ok("template", ("" + n) === "5");
ok("concat", ("v" + n) === "v5");
ok("valueOf", n.valueOf() === 5);
ok("toFixed", n.toFixed(2) === "5.00");
ok("keys hidden", Object.keys(n).length === 0);
var f = new Number(2.5);
ok("float toString", f.toString() === "2.5");
ok("float toFixed", f.toFixed(1) === "2.5");
var s = new Number("7");
ok("string arg", s.toString() === "7");
var z = new Number();
ok("no arg zero", z.toString() === "0");
ok("plain call still primitive", Number("9") === 9 && typeof Number("9") === "number");
console.log("boxed-number regression: " + passes + " passed, " + fails + " failed");
if(fails === 0) console.log("__BOXED_NUMBER_OK__");
