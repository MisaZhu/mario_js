// Verify ES 25.5.1.1: JSON.parse MUST throw SyntaxError on malformed input.
// This is the standard gap that broke JD's h5st signer (try{JSON.parse(algo)}
// catch{eval} - the catch never ran because mario returned undefined).
var pass = 0, fail = 0;
function chk(name, cond) {
  if (cond) { pass++; console.log("PASS " + name); }
  else { fail++; console.log("FAIL " + name); }
}

// 1. bare function token -> must throw
var threw = false;
try { JSON.parse("function test(a){return a;}"); } catch (e) { threw = (e instanceof SyntaxError) || /Syntax/.test(String(e)); }
chk("function token throws SyntaxError", threw);

// 2. unquoted garbage token -> must throw
threw = false;
try { JSON.parse("foo bar"); } catch (e) { threw = true; }
chk("bare identifier throws", threw);

// 3. malformed object -> must throw
threw = false;
try { JSON.parse('{"a": }'); } catch (e) { threw = true; }
chk("malformed object throws", threw);

// 4. valid JSON still parses (no regression)
var o = JSON.parse('{"a":1,"b":"str","c":[1,2,3],"d":true,"e":null}');
chk("valid object parses", o.a === 1 && o.b === "str" && o.c.length === 3 && o.c[2] === 3 && o.d === true && o.e == null);

// 5. a string VALUE containing the word function parses fine (NOT a throw)
var s = JSON.parse('{"algo":"function test(a){return a;}"}');
chk("quoted function-string parses", s.algo === "function test(a){return a;}");

// 6. valid arrays / scalars
chk("valid array", JSON.parse("[1,2,3]").length === 3);
chk("valid number", JSON.parse("42") === 42);
chk("valid negative", JSON.parse("-7") === -7);
chk("valid string", JSON.parse('"hi"') === "hi");
chk("valid bool true", JSON.parse("true") === true);
chk("valid null (lenient: == null)", JSON.parse("null") == null);

// 7. the JD fallback pattern now works: catch runs (CLI has no eval, use Function)
function parseAlgo(str) {
  try { return JSON.parse(str); } catch (e) { return new Function("return (" + str + ")")(); }
}
var fn = parseAlgo("function test(a,b){return a+b;}");
chk("try/catch Function fallback yields function", typeof fn === "function" && fn(2,3) === 5);

console.log(pass + " passed, " + fail + " failed");
if (fail === 0) console.log("ALL TESTS PASSED");
