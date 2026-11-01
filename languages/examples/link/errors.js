// Linking example (see errors.sh): JavaScript handling an exception that Python (errors.py) raises

console.log("checked_sqrt(2) =", checked_sqrt(2));
try {
    checked_sqrt(-1);
} catch (e) {
    console.log("caught:", e.message);
}
try {
    not_defined_anywhere();
} catch (e) {
    console.log("caught:", e.name + ":", e.message);
}
