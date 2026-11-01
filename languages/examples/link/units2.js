// Linking example (see units.sh): a second JavaScript translation unit: it uses units.js's function and constant (JavaScript's units share
// their global object, as scripts do) and both Python units' functions

console.log("units2.js:", shout(greeting + " again"), "cube(5) =", cube(5), "bump() =", bump());
