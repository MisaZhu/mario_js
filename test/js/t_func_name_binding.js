// Regression: an inferred function display name is not a lexical self-binding.
// Only an explicitly named function expression may bind its own name in-body.
var passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " want " + want); }
}
function ok(value, msg) { eq(!!value, true, msg); }

var assigned = {};
assigned.isNaN = function(t) { return isNaN ? isNaN(t) : t !== t; };
eq(assigned.isNaN(1), false, "assigned anonymous function calls global isNaN");
eq(assigned.isNaN("x"), true, "assigned anonymous function handles NaN");
eq(assigned.isNaN.name, "isNaN", "assignment still infers display name");

var literal = {
    isNaN: function(t) { return isNaN(t); },
    isFinite: function(t) { return isFinite(t); }
};
eq(literal.isNaN("x"), true, "object-literal anonymous function calls global isNaN");
eq(literal.isFinite(1), true, "object-literal anonymous function calls global isFinite");
eq(literal.isNaN.name, "isNaN", "object literal still infers display name");

var methods = {
    isNaN(t) { return isNaN(t); }
};
eq(methods.isNaN("x"), true, "method display name does not shadow global");
eq(methods.isNaN.name, "isNaN", "method keeps display name");

var recursive = function self(n) { return n > 0 ? self(n - 1) : 0; };
eq(recursive(4), 0, "explicit named function expression keeps self-binding");
eq(recursive.name, "self", "explicit named function expression keeps its name");

function declaredName() { return 7; }
eq(declaredName(), 7, "function declaration remains callable");
eq(declaredName.name, "declaredName", "function declaration keeps display name");
function declaredStack() { return new Error("marker").stack; }
ok(String(declaredStack()).indexOf("declaredStack") >= 0,
   "function declaration remains visible in Error.stack");

console.log(failed === 0 ? "__FUNC_NAME_BINDING_OK__" : ("__FUNC_NAME_BINDING_FAIL__ " + failed));
console.log("function name binding regression: " + passed + " passed, " + failed + " failed");
