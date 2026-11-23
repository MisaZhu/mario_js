// Regression: Array iteration methods must honour the optional trailing
// thisArg (ES spec). mario used to bind the native's own arguments object as
// the callback receiver and ignore thisArg, so `list.forEach(fn, ctx)` ran fn
// with the wrong `this`. That broke regeneratorRuntime's Context
// (`tryLocs.forEach(pushTryEntry, this)` -> `this.tryEntries.push` on a
// non-array), which made every transpiled async function reject at once and,
// on jd.com, stopped colorRequestWithRetry from ever issuing its request
// (pchome_firstScreenSoa / pc_home_feed) -> empty first screen (gray blocks).
var fail = 0;
function chk(name, cond){ if(!cond){ fail++; console.log("FAIL "+name); } }

var ctx = { tag:"CTX", seen:[] };

// forEach
[1,2,3].forEach(function(v){ chk("forEach this", this === ctx); this.seen.push(v); }, ctx);
chk("forEach collected", ctx.seen.join(",") === "1,2,3");

// map
var m = ["a","b"].map(function(v){ return this.tag + v; }, ctx);
chk("map this", m.join(",") === "CTXa,CTXb");

// filter
var f = [1,2,3,4].filter(function(v){ return v % 2 === (this.par||0); }, {par:0});
chk("filter this", f.join(",") === "2,4");

// some / every
chk("some this", [1,2].some(function(v){ return this.ok && v===2; }, {ok:true}) === true);
chk("every this", [2,4].every(function(v){ return v % this.d === 0; }, {d:2}) === true);

// find / findIndex / findLast / findLastIndex
chk("find this", [1,2,3].find(function(v){ return v === this.want; }, {want:3}) === 3);
chk("findIndex this", [1,2,3].findIndex(function(v){ return v === this.want; }, {want:2}) === 1);
chk("findLast this", [1,2,3].findLast(function(v){ return v < this.n; }, {n:3}) === 2);
chk("findLastIndex this", [1,2,3].findLastIndex(function(v){ return v < this.n; }, {n:3}) === 1);

// flatMap
var fm = [1,2].flatMap(function(v){ return [v, v * this.k]; }, {k:10});
chk("flatMap this", fm.join(",") === "1,10,2,20");

// Array.from mapFn thisArg
var af = Array.from([1,2,3], function(v){ return v + this.base; }, {base:100});
chk("Array.from thisArg", af.join(",") === "101,102,103");

// absent thisArg -> receiver falls back to the engine default (globalThis),
// NOT the iteration method's own arguments object (the original bug bound the
// native env as `this`, so callbacks saw a bogus receiver).
var noThis = "clean";
[1].forEach(function(){ noThis = (this === globalThis) ? "global" : "other"; });
chk("no thisArg -> not the args object", noThis === "global");

// Function.prototype.length must stay 1 for the single-required-arg methods
chk("forEach.length", [].forEach.length === 1);
chk("map.length", [].map.length === 1);
chk("filter.length", [].filter.length === 1);
chk("some.length", [].some.length === 1);
chk("every.length", [].every.length === 1);
chk("find.length", [].find.length === 1);
chk("flatMap.length", [].flatMap.length === 1);

// regeneratorRuntime-style Context pattern that originally crashed
function pushTryEntry(ee){ var Ae={tryLoc:ee[0]}; if(1 in ee) Ae.catchLoc=ee[1]; this.tryEntries.push(Ae); }
function Context(ee){ this.tryEntries=[{tryLoc:"root"}]; ee.forEach(pushTryEntry, this); }
var c = new Context([[0,6],[9,15]]);
chk("Context tryEntries isArray", Array.isArray(c.tryEntries));
chk("Context tryEntries len", c.tryEntries.length === 3);
chk("Context root preserved", c.tryEntries[0].tryLoc === "root");

console.log(fail === 0 ? "__ARRAY_THISARG_OK__" : ("__ARRAY_THISARG_FAIL__ "+fail));
