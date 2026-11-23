/* Regression: Annex B.2.2 legacy static RegExp properties ($1..$9, input/$_,
 * lastMatch/$&, lastParen/$+, leftContext/$`, rightContext/$') must be updated
 * as a side effect of every SUCCESSFUL match (test/exec/match/replace/search/
 * split) and left intact on a failed match. Obfuscated bundles -- notably JD's
 * h5st date formatter `if(/(y+)/.test(p)) p.replace(RegExp.$1, year)` -- depend
 * on them; without them the h5st date segment stays the literal template and the
 * server rejects the signature. */
var fail = 0;
function eq(got, want, label) {
	if(got !== want) { fail++; console.log("FAIL " + label + ": got [" + got + "] want [" + want + "]"); }
}
/(y+)/.test("yyyyMMdd");
eq(RegExp.$1, "yyyy", "$1 after test");
/(M+)(d+)/.exec("MMdd");
eq(RegExp.$1, "MM", "$1 after exec");
eq(RegExp.$2, "dd", "$2 after exec");
"abcd".replace(/(b)(c)/, "X");
eq(RegExp.$1, "b", "$1 after replace");
eq(RegExp.$2, "c", "$2 after replace");
"hello".match(/(ll)/);
eq(RegExp.$1, "ll", "$1 after match");
/(z)/.test("hello");            /* fails: snapshot must be retained */
eq(RegExp.$1, "ll", "$1 kept after failed match");
eq(RegExp.input, "hello", "input after failed match kept prior");
/(y+)/.test("hello");           /* fails again: input still retained */
eq(RegExp.$_, "hello", "$_ (input alias) retained after failure");
/l+/.test("hello");
eq(RegExp.lastMatch, "ll", "lastMatch");
eq(RegExp["$&"], "ll", "$& alias");
eq(RegExp.leftContext, "he", "leftContext");
eq(RegExp["$`"], "he", "$` alias");
eq(RegExp.rightContext, "o", "rightContext");
eq(RegExp["$'"], "o", "$' alias");
/(h)(e)(l)/.exec("hello");
eq(RegExp.lastParen, "l", "lastParen highest group");
eq(RegExp["$+"], "l", "$+ alias");
/(a)|(b)/.exec("b");
eq(RegExp.$1, "", "non-participating group is empty");
eq(RegExp.$2, "b", "participating alternation group");
"xxabc".search(/(abc)/);
eq(RegExp.$1, "abc", "$1 after search");
/* ten groups: $1..$9 populated, group 10 not exposed as a static */
/(1)(2)(3)(4)(5)(6)(7)(8)(9)(0)/.exec("1234567890");
eq(RegExp.$9, "9", "$9 is ninth group");
eq(RegExp.$1, "1", "$1 is first group");
/* The JD h5st date-formatter idiom: token substitution via RegExp.$1 */
function fmtDate(d) {
	var p = "yyyyMMdd", o = { "M+": d.getMonth() + 1, "d+": d.getDate() };
	if(/(y+)/.test(p)) p = p.replace(RegExp.$1, (d.getFullYear() + "").substr(4 - RegExp.$1.length));
	for(var k in o) {
		if(new RegExp("(" + k + ")").test(p)) {
			var v = "00" + o[k];
			p = p.replace(RegExp.$1, v.substr(v.length - RegExp.$1.length));
		}
	}
	return p;
}
eq(fmtDate(new Date(2026, 10, 12)), "20261112", "h5st date formatter idiom");
console.log(fail === 0 ? "__REGEXP_STATIC_OK__" : ("regexp-static failures: " + fail));
