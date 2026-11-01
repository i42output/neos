// Linking example (see objects.sh): JavaScript objects that Python (objects.py) holds and gives back, and Python objects that
// JavaScript holds and gives back

const origin = { x: 3, y: 4 };

function length(point) {
    return Math.sqrt(point.x * point.x + point.y * point.y);
}

function keep(value) {
    keep.kept = value;
    return value;
}

function kept() {
    return keep.kept;
}
