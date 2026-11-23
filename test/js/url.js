// URL / URLSearchParams smoke test (WHATWG practical subset).
// Self-checking: prints a pass/fail tally and a final sentinel.
var passed = 0, failed = 0;
function eq(actual, expected, msg) {
    if (actual === expected) { passed++; }
    else { failed++; console.log("FAIL " + msg + ": got [" + actual + "] want [" + expected + "]"); }
}
function ok(cond, msg) { eq(!!cond, true, msg); }

console.log("=== URLSearchParams ===");
var usp = new URLSearchParams("a=1&b=2&a=3");
eq(usp.get("a"), "1", "usp.get first");
eq(usp.get("b"), "2", "usp.get b");
eq(usp.get("zz"), null, "usp.get missing -> null");
eq(usp.getAll("a").length, 2, "usp.getAll length");
eq(usp.getAll("a")[1], "3", "usp.getAll second");
eq(usp.has("b"), true, "usp.has true");
eq(usp.has("no"), false, "usp.has false");
eq(usp.toString(), "a=1&b=2&a=3", "usp.toString roundtrip");

usp.append("c", "4");
eq(usp.get("c"), "4", "usp.append");
usp.set("a", "9");
eq(usp.getAll("a").length, 1, "usp.set dedups");
eq(usp.get("a"), "9", "usp.set value");
usp.delete("b");
eq(usp.has("b"), false, "usp.delete");

var usp2 = new URLSearchParams("?x=hello+world&y=a%20b");
eq(usp2.get("x"), "hello world", "usp decodes '+' as space");
eq(usp2.get("y"), "a b", "usp decodes %20");
eq(usp2.toString(), "x=hello+world&y=a+b", "usp re-encodes space as '+'");
eq(decodeURIComponent("cna=a+b%2Bc"), "cna=a+b+c", "decodeURIComponent preserves literal plus");
eq(decodeURI("a+b%2Bc"), "a+b%2Bc", "decodeURI preserves plus and reserved escape");

var usp3 = new URLSearchParams([["k", "v"], ["k2", "v2"]]);
eq(usp3.get("k"), "v", "usp array-of-pairs init");
eq(usp3.get("k2"), "v2", "usp array-of-pairs init 2");

var usp4 = new URLSearchParams({ p: "1", q: "2" });
eq(usp4.get("p"), "1", "usp record init");
eq(usp4.get("q"), "2", "usp record init 2");

var sortUsp = new URLSearchParams("b=1&a=2&c=3");
sortUsp.sort();
eq(sortUsp.toString(), "a=2&b=1&c=3", "usp.sort by key");

var seen = [];
new URLSearchParams("m=1&n=2").forEach(function (v, k) { seen.push(k + "=" + v); });
eq(seen.join(","), "m=1,n=2", "usp.forEach");
eq(new URLSearchParams("z=1&z=2").keys().length, 2, "usp.keys");
eq(new URLSearchParams("z=1&z=2").values().join(","), "1,2", "usp.values");
eq(new URLSearchParams("z=1").entries()[0][0], "z", "usp.entries");

console.log("=== URL ===");
var u = new URL("https://user:pass@Example.COM:8080/a/b/../c?q=1#frag");
eq(u.protocol, "https:", "url.protocol");
eq(u.hostname, "example.com", "url.hostname lowercased");
eq(u.port, "8080", "url.port");
eq(u.host, "example.com:8080", "url.host");
eq(u.username, "user", "url.username");
eq(u.password, "pass", "url.password");
eq(u.pathname, "/a/c", "url.pathname dot-segments removed");
eq(u.search, "?q=1", "url.search");
eq(u.hash, "#frag", "url.hash");
eq(u.origin, "https://example.com:8080", "url.origin");
eq(u.searchParams.get("q"), "1", "url.searchParams.get");
eq(u.href, "https://user:pass@example.com:8080/a/c?q=1#frag", "url.href");

// default port omission
var d = new URL("http://x.com:80/p");
eq(d.port, "", "url default port http:80 omitted");
eq(d.href, "http://x.com/p", "url href omits default port");
var d2 = new URL("https://x.com:443/");
eq(d2.port, "", "url default port https:443 omitted");
eq(d2.pathname, "/", "url empty path normalised to /");

// origin for default-port special scheme
var o = new URL("https://example.com/x");
eq(o.origin, "https://example.com", "url.origin no port");

// relative resolution against base
var r1 = new URL("/abs/path", "http://h/base/dir");
eq(r1.href, "http://h/abs/path", "relative absolute-path");
var r2 = new URL("rel.html", "http://h/base/dir/index.html");
eq(r2.href, "http://h/base/dir/rel.html", "relative merge");
var r3 = new URL("../up", "http://h/a/b/c");
eq(r3.href, "http://h/a/up", "relative ../");
var r4 = new URL("?onlyquery", "http://h/p?old=1");
eq(r4.href, "http://h/p?onlyquery", "relative ?query keeps path");
var r5 = new URL("#onlyhash", "http://h/p?q=1");
eq(r5.href, "http://h/p?q=1#onlyhash", "relative #hash keeps path+query");
var r6 = new URL("//other.com/x", "http://h/p");
eq(r6.href, "http://other.com/x", "protocol-relative inherits scheme");

// toString reflects live searchParams mutation
var m = new URL("http://h/p?a=1");
m.searchParams.set("b", "2");
eq(m.toString(), "http://h/p?a=1&b=2", "url.toString reflects searchParams.set");
m.searchParams.delete("a");
eq(m.toString(), "http://h/p?b=2", "url.toString reflects searchParams.delete");

// canParse
eq(URL.canParse("https://ok.com/x"), true, "canParse absolute true");
eq(URL.canParse("/rel", "https://base.com"), true, "canParse relative-with-base true");
eq(URL.canParse("not a url"), false, "canParse no-scheme false");

// file: origin is "null"
var f = new URL("file:///etc/hosts");
eq(f.origin, "null", "file origin null");
eq(f.pathname, "/etc/hosts", "file pathname");

console.log("=== URL result: " + passed + " passed, " + failed + " failed ===");
if (failed === 0) console.log("__URL_OK__");
