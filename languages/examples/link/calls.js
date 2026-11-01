// Linking example (see calls.sh): JavaScript that calls the Python functions of calls.py

console.log("add(2, 3) =", add(2, 3));
console.log("add(0.5, 0.25) =", add(0.5, 0.25));
console.log("add('neo', 's') =", add("neo", "s"));
console.log(describe(42));
console.log(describe(3.5));
console.log(describe("text"));
console.log(describe(true));
console.log(describe(null));
console.log(greet("JavaScript"));
console.log(typeof add);
