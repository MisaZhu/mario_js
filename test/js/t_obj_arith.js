// Object arithmetic regression: an object operand in any arithmetic op except
// `+` must go through ToPrimitive(number) -> ToNumber (vm_to_number), so a
// Date's valueOf yields epoch ms (`d1 - d2` is the ms delta, `+d` the
// timestamp). `+` keeps the DEFAULT hint under which a Date prefers toString,
// so `date + date` concatenates strings per spec.
var fails = 0, passes = 0;
function ok(name, cond) {
        if(cond) { passes++; }
        else { fails++; console.log("FAIL " + name); }
}
var a = new Date(1000000);
var b = new Date(400000);
ok("date sub", a - b === 600000);
ok("date unary plus", +a === 1000000);
ok("date neg", -b === -400000);
ok("date mul", b * 2 === 800000);
ok("date div", a / 1000 === 1000);
ok("date mod", a % 7 === 1000000 % 7);
ok("date pow", (new Date(3)) ** 2 === 9);
ok("date band", (a & 0xffffffff) === (1000000 & 0xffffffff));
ok("date bor", (b | 0) === 400000);
ok("date lshift", (new Date(1) << 4) === 16);
ok("date add is string", typeof (a + b) === "string");
ok("date add str", typeof (a + "x") === "string");
ok("arr single", [5] * 2 === 10);
ok("arr multi NaN", isNaN([1,2] * 2));
var vo = { valueOf: function() { return 7; } };
ok("custom valueOf mul", vo * 6 === 42);
ok("custom valueOf unary", +vo === 7);
ok("plain obj NaN", isNaN({} - 1));
ok("mixed obj num", b - 100000 === 300000);
ok("mixed num obj", 1000000 - b === 600000);
ok("Number(date)", Number(a) === 1000000);
ok("int arith intact", 5 + 3 === 8 && 5 - 3 === 2 && 5 * 3 === 15 && 5 / 2 === 2.5 && 5 % 3 === 2);
ok("bool arith intact", true + 1 === 2 && 3 * false === 0);
ok("str concat intact", "5" + 1 === "51");
// null coerces to +0 in every arithmetic op (num_class ranks V_NULL NC_INT32),
// while `null + "x"` still concatenates per the spec's string-first `+` rule.
ok("null add", null + 1 === 1);
ok("null sub", null - 1 === -1);
ok("null mul", null * 5 === 0);
ok("null band", (null | 0) === 0);
ok("null concat", null + "x" === "nullx");
// Numeric strings coerce in every arithmetic op except `+`.
ok("str sub", "5" - 1 === 4);
ok("str mul", "5" * "2" === 10);
ok("str div", "9" / "3" === 3);
ok("str mod", "7" % 4 === 3);
ok("str neg", -"5" === -5);
ok("str bor", ("5" | 0) === 5);
ok("junk str NaN", isNaN("a" - "b") && isNaN("x" * 2));
ok("str str concat intact", "5" + "1" === "51");
ok("undef sub NaN", isNaN(undefined - 1));
// `+` with undefined and no string side is NaN (ToPrimitive yields no string,
// so both sides ToNumber); with a string side it still concatenates.
ok("undef add num", isNaN(undefined + 1));
ok("num add undef", isNaN(1 + undefined));
ok("undef add null", isNaN(undefined + null));
ok("undef add undef", isNaN(undefined + undefined));
ok("undef add bool", isNaN(undefined + true));
ok("undef concat right", undefined + "x" === "undefinedx");
ok("undef concat left", "x" + undefined === "xundefined");
console.log("obj-arith regression: " + passes + " passed, " + fails + " failed");
if(fails === 0) console.log("__OBJ_ARITH_OK__");
