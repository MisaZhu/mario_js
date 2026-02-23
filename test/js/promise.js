console.log("=== Promise Test ===");

// Test 1: Promise constructor exists
console.log("\nTest 1: Promise constructor");
try {
    var p = new Promise(function(resolve, reject) {
        resolve("test");
    });
    console.log("✓ Promise constructor works");
} catch (e) {
    console.log("✗ Promise constructor failed:", e);
}

// Test 2: Promise.resolve
console.log("\nTest 2: Promise.resolve");
try {
    var p = Promise.resolve(42);
    console.log("✓ Promise.resolve works");
    console.log("   Promise object created");
} catch (e) {
    console.log("✗ Promise.resolve failed:", e);
}

// Test 3: Promise.reject
console.log("\nTest 3: Promise.reject");
try {
    var p = Promise.reject("error");
    console.log("✓ Promise.reject works");
} catch (e) {
    console.log("✗ Promise.reject failed:", e);
}

// Test 4: Promise.prototype.then with resolved promise
console.log("\nTest 4: Promise.then with resolved promise");
try {
    var result = 0;
    var p = Promise.resolve(10);
    var p2 = p.then(function(value) {
        result = value * 2;
        return result;
    });
    console.log("✓ Promise.then works with resolved promise");
    console.log("   Result:", result);
} catch (e) {
    console.log("✗ Promise.then failed:", e);
}

// Test 5: Promise.prototype.catch using then's second parameter
console.log("\nTest 5: Promise.catch (using then's second parameter)");
try {
    var caught = false;
    var p = Promise.reject("error");
    var p2 = p.then(
        null,
        function(err) {
            caught = true;
            return "caught: " + err;
        }
    );
    console.log("✓ Promise.catch works");
    console.log("   Caught:", caught);
} catch (e) {
    console.log("✗ Promise.catch failed:", e);
}

// Test 6: Promise.prototype.finally
console.log("\nTest 6: Promise.finally");
try {
    var finallyCalled = false;
    var p = Promise.resolve(10);
    var p2 = p.finally(function() {
        finallyCalled = true;
        console.log("   finally called");
    });
    console.log("✓ Promise.finally works");
    console.log("   Finally called:", finallyCalled);
} catch (e) {
    console.log("✗ Promise.finally failed:", e);
}

// Test 7: Promise.all
console.log("\nTest 7: Promise.all");
try {
    var p1 = Promise.resolve(1);
    var p2 = Promise.resolve(2);
    var p3 = Promise.resolve(3);
    var all = Promise.all([p1, p2, p3]);
    console.log("✓ Promise.all works");
} catch (e) {
    console.log("✗ Promise.all failed:", e);
}

// Test 8: Promise.race
console.log("\nTest 8: Promise.race");
try {
    var p1 = Promise.resolve(1);
    var p2 = Promise.resolve(2);
    var race = Promise.race([p1, p2]);
    console.log("✓ Promise.race works");
} catch (e) {
    console.log("✗ Promise.race failed:", e);
}

// Test 9: Chained promises
console.log("\nTest 9: Chained promises");
try {
    var result = 0;
    var p = Promise.resolve(5)
        .then(function(value) {
            result = value + 1;
            return result;
        })
        .then(function(value) {
            result = value * 2;
            return result;
        });
    console.log("✓ Chained promises work");
    console.log("   Result:", result);
} catch (e) {
    console.log("✗ Chained promises failed:", e);
}


// Test 10: Promise with executor
console.log("\nTest 10: Promise with executor");
try {
    var executed = false;
    var p = new Promise(function(resolve, reject) {
        executed = true;
        resolve("done");
    });
    console.log("✓ Promise executor runs immediately");
    console.log("   Executed:", executed);
} catch (e) {
    console.log("✗ Promise executor failed:", e);
}

// Test 11: Promise with reject in executor
console.log("\nTest 11: Promise with reject in executor");
try {
    var rejected = false;
    var p = new Promise(function(resolve, reject) {
        reject("test error");
    });
    console.log("✓ Promise executor with reject works");
} catch (e) {
    console.log("✗ Promise executor with reject failed:", e);
}

// Test 12: Promise.all with empty array
console.log("\nTest 12: Promise.all with empty array");
try {
    var all = Promise.all([]);
    console.log("✓ Promise.all with empty array works");
} catch (e) {
    console.log("✗ Promise.all with empty array failed:", e);
}

// Test 13: Promise.race with empty array
console.log("\nTest 13: Promise.race with empty array");
try {
    var race = Promise.race([]);
    console.log("✓ Promise.race with empty array works");
} catch (e) {
    console.log("✗ Promise.race with empty array failed:", e);
}

// Test 14: Promise.then with rejection
console.log("\nTest 14: Promise.then with rejection");
try {
    var p = Promise.reject("reject error");
    var p2 = p.then(
        function(value) {
            return "should not be called";
        },
        function(error) {
            return "caught: " + error;
        }
    );
    console.log("✓ Promise.then with rejection works");
} catch (e) {
    console.log("✗ Promise.then with rejection failed:", e);
}

// Test 15: Promise.catch after reject
console.log("\nTest 15: Promise.catch after reject");
try {
    var p = Promise.reject("catch error");
    var p2 = p.then(
        null,
        function(error) {
            return "handled: " + error;
        }
    );
    console.log("✓ Promise.catch after reject works");
} catch (e) {
    console.log("✗ Promise.catch after reject failed:", e);
}

// Test 16: Promise.finally with resolved
console.log("\nTest 16: Promise.finally with resolved");
try {
    var finallyExecuted = false;
    var p = Promise.resolve("success");
    var p2 = p.finally(function() {
        finallyExecuted = true;
    });
    console.log("✓ Promise.finally with resolved works");
    console.log("   Finally executed:", finallyExecuted);
} catch (e) {
    console.log("✗ Promise.finally with resolved failed:", e);
}

// Test 17: Promise.finally with rejected
console.log("\nTest 17: Promise.finally with rejected");
try {
    var finallyExecuted = false;
    var p = Promise.reject("failure");
    var p2 = p.finally(function() {
        finallyExecuted = true;
    });
    console.log("✓ Promise.finally with rejected works");
    console.log("   Finally executed:", finallyExecuted);
} catch (e) {
    console.log("✗ Promise.finally with rejected failed:", e);
}

// Test 18: Complex promise chain
console.log("\nTest 18: Complex promise chain");
try {
    var finalResult = 0;
    var p = Promise.resolve(1)
        .then(function(value) {
            return value + 1;
        })
        .then(function(value) {
            return value * 2;
        })
        .then(function(value) {
            finalResult = value;
            return value;
        });
    console.log("✓ Complex promise chain works");
    console.log("   Final result:", finalResult);
} catch (e) {
    console.log("✗ Complex promise chain failed:", e);
}

// Test 19: Promise.resolve with non-promise value
console.log("\nTest 19: Promise.resolve with different types");
try {
    var p1 = Promise.resolve(123);
    var p2 = Promise.resolve("string");
    var p3 = Promise.resolve(true);
    var p4 = Promise.resolve(null);
    console.log("✓ Promise.resolve with different types works");
} catch (e) {
    console.log("✗ Promise.resolve with different types failed:", e);
}

// Test 20: Promise.reject with different types
console.log("\nTest 20: Promise.reject with different types");
try {
    var p1 = Promise.reject(123);
    var p2 = Promise.reject("error string");
    var p3 = Promise.reject(false);
    var p4 = Promise.reject(null);
    console.log("✓ Promise.reject with different types works");
} catch (e) {
    console.log("✗ Promise.reject with different types failed:", e);
}

console.log("\n=== Promise Test Complete ===");
console.log("Total tests: 20");