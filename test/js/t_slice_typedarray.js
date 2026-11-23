/* Regression: Array.prototype generic methods applied to a TypedArray receiver
 * (via .call/.apply, e.g. `Array.prototype.slice.call(uint8)`) must read the
 * elements out of the backing buffer. TypedArrays store their values in the
 * ArrayBuffer, not as named "0","1",.. members, so the generic array-like
 * materialisation path used to see every index as a hole and produce
 * [undefined,undefined,..] of the right length. CryptoJS (JD's h5st SDK) builds
 * the digest as a Uint8Array then does slice.call() on it before hex-formatting;
 * with holes the hex came out all '0', the h5st signature was rejected (403) and
 * the jd.com CSR feed stayed as gray skeleton blocks. */
var fail = 0;
function eq(got, want, label) {
	if(got !== want) { fail++; console.log("FAIL " + label + ": got [" + got + "] want [" + want + "]"); }
}

var u8 = new Uint8Array([226, 3, 131, 234]);
eq(JSON.stringify(Array.prototype.slice.call(u8)), "[226,3,131,234]", "slice.call(Uint8Array)");
eq(JSON.stringify([].slice.call(u8)), "[226,3,131,234]", "[].slice.call(Uint8Array)");
eq(JSON.stringify(Array.prototype.slice.call(u8, 1)), "[3,131,234]", "slice.call with start");
eq(JSON.stringify(Array.prototype.slice.call(u8, 1, 3)), "[3,131]", "slice.call with start+end");

/* other generic methods on a TypedArray receiver */
eq(Array.prototype.join.call(u8, "-"), "226-3-131-234", "join.call(Uint8Array)");
eq(Array.prototype.indexOf.call(u8, 131), 2, "indexOf.call(Uint8Array)");
var mapped = [];
Array.prototype.forEach.call(u8, function(v) { mapped.push(v * 2); });
eq(JSON.stringify(mapped), "[452,6,262,468]", "forEach.call(Uint8Array)");

/* different element types must widen/narrow correctly */
var i32 = new Int32Array([-1, 2, -3]);
eq(JSON.stringify(Array.prototype.slice.call(i32)), "[-1,2,-3]", "slice.call(Int32Array) negatives");
var u32 = new Uint32Array([4294967295, 1]);
eq(JSON.stringify(Array.prototype.slice.call(u32)), "[4294967295,1]", "slice.call(Uint32Array) max");
var f64 = new Float64Array([1.5, -2.25]);
eq(JSON.stringify(Array.prototype.slice.call(f64)), "[1.5,-2.25]", "slice.call(Float64Array)");
var u16 = new Uint16Array([65535, 258]);
eq(JSON.stringify(Array.prototype.slice.call(u16)), "[65535,258]", "slice.call(Uint16Array)");

/* plain arrays and array-likes must keep working */
eq(JSON.stringify(Array.prototype.slice.call([7, 8, 9])), "[7,8,9]", "slice.call(plain array)");
eq(JSON.stringify(Array.prototype.slice.call({ length: 2, 0: "a", 1: "b" })), '["a","b"]', "slice.call(array-like object)");
eq(JSON.stringify(Array.prototype.slice.call("abc")), '["a","b","c"]', "slice.call(string)");

/* empty typed array */
eq(JSON.stringify(Array.prototype.slice.call(new Uint8Array(0))), "[]", "slice.call(empty Uint8Array)");

console.log(fail === 0 ? "__SLICE_TYPEDARRAY_OK__" : ("slice-typedarray failures: " + fail));
