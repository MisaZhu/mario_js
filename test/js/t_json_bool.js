// Regression: JSON.parse must produce V_BOOL for true/false, exactly like the
// true/false literals the compiler emits (INSTR_TRUE/INSTR_FALSE). It used to
// return var_new_int(1/0), so every parsed boolean was the NUMBER 1/0:
//   typeof JSON.parse('false') === 'number'   (should be 'boolean')
//   JSON.parse('false') === 0                 (should be false)
//   JSON.stringify(JSON.parse('false')) === '0'   (should be 'false')
// The fatal case was React: `flag && <C/>` with flag===0 renders the text "0"
// instead of nothing, so github's false banner flags hydrated a "0" text node
// into an SSR-empty container -> hydration mismatch #519 -> root element=null.
let passed = 0, failed = 0;
function eq(got, want, msg) {
    if (got === want) passed++;
    else { failed++; console.log("FAIL " + msg + ": got " + got + " (" + (typeof got) + ") want " + want); }
}

// --- typeof / identity ------------------------------------------------------
eq(typeof JSON.parse('false'), 'boolean', "typeof parsed false");
eq(typeof JSON.parse('true'), 'boolean', "typeof parsed true");
eq(JSON.parse('false'), false, "parsed false === false");
eq(JSON.parse('true'), true, "parsed true === true");
eq(JSON.parse('false') === 0, false, "parsed false is NOT the number 0");
eq(JSON.parse('true') === 1, false, "parsed true is NOT the number 1");

// --- nested in objects / arrays --------------------------------------------
eq(JSON.parse('{"a":false}').a, false, "object member false");
eq(JSON.parse('{"a":true}').a, true, "object member true");
eq(JSON.parse('[true,false]')[0], true, "array element true");
eq(JSON.parse('[true,false]')[1], false, "array element false");

// --- stringify round-trip ---------------------------------------------------
eq(JSON.stringify(JSON.parse('false')), 'false', "round-trip false");
eq(JSON.stringify(JSON.parse('true')), 'true', "round-trip true");
eq(JSON.stringify(JSON.parse('{"a":true,"b":false}')), '{"a":true,"b":false}', "round-trip object");

// --- React's `flag && <C/>` shape: false must render NOTHING, not "0" -------
var flag = JSON.parse('false');
eq(flag && 'RENDERED', false, "false && X === false (React renders nothing)");
eq(JSON.parse('false') ? 'yes' : 'no', 'no', "parsed false is falsy");
eq(JSON.parse('true') ? 'yes' : 'no', 'yes', "parsed true is truthy");

// --- booleans still coerce numerically (see t_bool_arith.js) ----------------
eq(JSON.parse('true') + 1, 2, "parsed true + 1 === 2");
eq(JSON.parse('false') * 5, 0, "parsed false * 5 === 0");

console.log(failed === 0 ? "__JSON_BOOL_OK__" : ("__JSON_BOOL_FAIL__ " + failed));
console.log("json boolean regression: " + passed + " passed, " + failed + " failed");
