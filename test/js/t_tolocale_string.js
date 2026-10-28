/* Regression: Number/String/Array must own toLocaleString so the lookup never
 * falls through to Object.prototype's alias ("[object Number]" etc.). The
 * GitHub contributors counter badge renders count.toLocaleString(). */
var fail = 0;
function eq(got, want, label) {
	if(got !== want) { fail++; console.log("FAIL " + label + ": got " + got + " want " + want); }
}
eq((286).toLocaleString(), "286", "int no group");
eq((7152).toLocaleString(), "7,152", "en-US grouping");
eq((1234567.891).toLocaleString(), "1,234,567.891", "group with fraction");
eq((-12345).toLocaleString(), "-12,345", "negative grouping");
eq((999).toLocaleString(), "999", "below threshold");
eq((7152).toLocaleString("en-US", {useGrouping: false}), "7152", "useGrouping false");
eq(Infinity.toLocaleString(), "Infinity", "infinity");
eq(NaN.toLocaleString(), "NaN", "nan");
eq("ab".toLocaleString(), "ab", "string passthrough");
eq([1234, "x", null, 5].toLocaleString(), "1,234,x,,5", "array elements");
eq(({}).toLocaleString(), "[object Object]", "plain object tag");
eq(true.toLocaleString(), "true", "boolean passthrough");
eq(Object.prototype.hasOwnProperty.call(Boolean.prototype, "toLocaleString"), true, "Boolean owns tls");
console.log(fail === 0 ? "__TO_LOCALE_STRING_OK__" : ("tolocale failures: " + fail));
