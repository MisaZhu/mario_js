// Regression guard for regex/template literals inside the compiler's token and
// char-level pre-scans (call_args_have_spread, paren_group_is_arrow and
// scan_param_expr). Those scanners walk the source to find a matching ')' or a
// ',' terminator, but a '/' is only a division token at the lexer level: a regex
// literal is recognised later, in factor(). A regex holding a structural char
// (/\{/ , /\(/ , /[/]/) therefore leaked that brace/paren into the depth
// counters, so the real terminator was never seen at depth 0 and the construct
// mis-parsed ("made no progress" / "expected ','"). This locks the fixed
// behaviour: regexes and templates with structural chars are stepped over as
// single units, while genuine division still parses as division.
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + JSON.stringify(got) + " want " + JSON.stringify(want)); }
}

// --- spread call whose earlier argument holds a regex with a structural char --
(function () {
    function f() { return arguments.length; }
    var a = [1, 2];
    // /\{/ has an unbalanced '{': the old spread pre-scan lost the top level.
    eq(f({ m() { return /\{/; } }, ...a), 3, "spread detected after a regex-with-brace argument");
    eq(f({ m() { return /\(/; } }, ...a), 3, "spread detected after a regex-with-paren argument");
    eq(f(/re/, ...a), 3, "spread detected after a plain regex argument");
    eq(f(...a), 2, "plain spread still detected");
})();

// --- arrow function whose default parameter is a regex with a structural char -
(function () {
    var arrow_paren = (a = /\(/) => String(a);
    eq(arrow_paren(), "/\\(/", "arrow default = regex with paren");
    var arrow_two = (a = /\(/, b) => String(a) + "|" + b;
    eq(arrow_two(undefined, 7), "/\\(/|7", "arrow default regex then a second param");
    var arrow_class = (a = /[/]/) => String(a);
    eq(arrow_class(), "/[/]/", "arrow default = regex with a slash inside a char class");
    var arrow_tmpl = (a = `x)y`) => a;
    eq(arrow_tmpl(), "x)y", "arrow default = template with a paren");
})();

// --- ordinary function declaration with a regex default parameter -------------
(function () {
    function fn(x = /\{/, y = 1) { return String(x) + ":" + y; }
    eq(fn(), "/\\{/:1", "function default = regex with brace");
})();

// --- destructuring parameter / binding whose default holds a structural regex -
(function () {
    function dp({ a = /\[/ } = {}) { return String(a); }
    eq(dp(), "/\\[/", "destructuring param default = regex with bracket");
    var bv = (function () { var o = {}; var { a = /\[/ } = o; return String(a); })();
    eq(bv, "/\\[/", "destructuring binding default = regex with bracket");
    // computed member key of a destructure *target* holding a structural regex
    var tgt = {};
    [tgt[/\[/]] = [7];
    eq(tgt[/\[/], 7, "destructure member target with regex computed key");
})();

// --- genuine division must still parse as division, not as a regex ------------
(function () {
    var div1 = (a = 10 / 2) => a;
    eq(div1(), 5, "arrow default = division");
    var div2 = (a = 6, b = a / 2) => b;
    eq(div2(), 3, "second arrow default = division of the first param");
    function div3(p = 8 / 4) { return p; }
    eq(div3(), 2, "function default = division");
})();

console.log(failed === 0 ? "__REGEX_PRESCAN_OK__" : ("__REGEX_PRESCAN_FAIL__ " + failed));
console.log("regex pre-scan regression: " + passed + " passed, " + failed + " failed");
