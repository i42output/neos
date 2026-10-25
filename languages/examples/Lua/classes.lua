-- Classes with metatables: inheritance, methods and metamethods

local Vector = {}
Vector.__index = Vector

function Vector.new(x, y) return setmetatable({ x = x, y = y }, Vector) end
function Vector.__add(a, b) return Vector.new(a.x + b.x, a.y + b.y) end
function Vector.__eq(a, b) return a.x == b.x and a.y == b.y end
function Vector.__tostring(v) return "(" .. v.x .. ", " .. v.y .. ")" end
function Vector:length() return math.sqrt(self.x ^ 2 + self.y ^ 2) end

local a, b = Vector.new(3, 4), Vector.new(1, 2)
print(tostring(a + b), a:length(), a == Vector.new(3, 4))

local Animal = {}
Animal.__index = Animal
function Animal.new(name, sound) return setmetatable({ name = name, sound = sound }, Animal) end
function Animal:speak() return self.name .. " says " .. self.sound end

local Dog = setmetatable({}, { __index = Animal })
Dog.__index = Dog
function Dog.new(name) return setmetatable(Animal.new(name, "woof"), Dog) end
function Dog:fetch() return self.name .. " fetches the ball" end

local rex = Dog.new("Rex")
print(rex:speak())
print(rex:fetch())
print(getmetatable(rex) == Dog, getmetatable(Dog).__index == Animal)
