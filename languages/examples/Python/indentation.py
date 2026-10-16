# Scoping by significant whitespace: nested blocks, dedenting several levels at once,
# statements following an if at the same level, inline bodies, comments and blank lines

def grade(score: int) -> str:
    if score >= 90: return "A"
    elif score >= 75:
        return "B"
    elif score >= 50:
        # comments inside a block don't affect it
        return "C"
    return "F"

def triangle(rows: int) -> int:
    total = 0
    row = 1
    while row <= rows:
        column = 0
        while column < row:
            if (row + column) % 2 == 0:
                total += 2
            else:
                total += 1

            column += 1
        row += 1
    return total

def count_odd_digits(n: int) -> int:
    count = 0
    while n > 0:
        digit = n % 10
        if digit % 2 == 1:
            count += 1
        n = n // 10
    return count

for s in range(40, 101, 20):
    print(str(s) + " -> " + grade(s))
print(triangle(5))
print(count_odd_digits(1234567))
x = 3
if x > 2:
    if x > 5:
        print("big")
    else:
        print("medium")
        if x == 3:
            print("three")
print("done")
