// Linking example (see units.sh): the first JavaScript translation unit; it uses units.py's function

function shout(s) {
    return s.toUpperCase() + "!";
}
const greeting = "hello";

console.log("units.js:", shout(greeting), "square(3) =", square(3));
