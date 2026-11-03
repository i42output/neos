// Classes in C: structures with a table of function pointers (virtual functions), "inheritance" by embedding the base as the
// first member, and constructors and destructors
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct shape shape;

typedef struct
{
    char const* name;
    double (*area)(shape const* self);
    double (*perimeter)(shape const* self);
    void (*destroy)(shape* self);
} shape_vtable;

struct shape
{
    shape_vtable const* vtable;
    int id;
};

static double const pi = 3.14159265358979323846;
static int next_id = 1;

static void shape_init(shape* self, shape_vtable const* vtable)
{
    self->vtable = vtable;
    self->id = next_id++;
}

static void shape_destroy(shape* self)
{
    printf("  destroying %s #%d\n", self->vtable->name, self->id);
    free(self);
}

// --- circle

typedef struct
{
    shape base;
    double radius;
} circle;

static double circle_area(shape const* self)
{
    circle const* c = (circle const*)self;
    return pi * c->radius * c->radius;
}

static double circle_perimeter(shape const* self)
{
    return 2.0 * pi * ((circle const*)self)->radius;
}

static shape_vtable const circle_vtable = { "circle", circle_area, circle_perimeter, shape_destroy };

static shape* circle_new(double radius)
{
    circle* c = malloc(sizeof *c);
    shape_init(&c->base, &circle_vtable);
    c->radius = radius;
    return &c->base;
}

// --- rectangle, and square (a rectangle whose name differs)

typedef struct
{
    shape base;
    double width, height;
} rectangle;

static double rectangle_area(shape const* self)
{
    rectangle const* r = (rectangle const*)self;
    return r->width * r->height;
}

static double rectangle_perimeter(shape const* self)
{
    rectangle const* r = (rectangle const*)self;
    return 2.0 * (r->width + r->height);
}

static shape_vtable const rectangle_vtable = { "rectangle", rectangle_area, rectangle_perimeter, shape_destroy };
static shape_vtable const square_vtable = { "square", rectangle_area, rectangle_perimeter, shape_destroy };

static shape* rectangle_new(double width, double height)
{
    rectangle* r = malloc(sizeof *r);
    shape_init(&r->base, width == height ? &square_vtable : &rectangle_vtable);
    r->width = width;
    r->height = height;
    return &r->base;
}

// --- triangle (its sides)

typedef struct
{
    shape base;
    double a, b, c;
} triangle;

static double triangle_perimeter(shape const* self)
{
    triangle const* t = (triangle const*)self;
    return t->a + t->b + t->c;
}

static double triangle_area(shape const* self)
{
    triangle const* t = (triangle const*)self;
    double const s = triangle_perimeter(self) / 2.0;
    return sqrt(s * (s - t->a) * (s - t->b) * (s - t->c));
}

static shape_vtable const triangle_vtable = { "triangle", triangle_area, triangle_perimeter, shape_destroy };

static shape* triangle_new(double a, double b, double c)
{
    triangle* t = malloc(sizeof *t);
    shape_init(&t->base, &triangle_vtable);
    *t = (triangle){ t->base, a, b, c };
    return &t->base;
}

static int by_area(void const* x, void const* y)
{
    shape const* a = *(shape* const*)x;
    shape const* b = *(shape* const*)y;
    double const d = a->vtable->area(a) - b->vtable->area(b);
    return (d > 0) - (d < 0);
}

int main(void)
{
    shape* shapes[] = { circle_new(1.5), rectangle_new(2.0, 3.5), triangle_new(3.0, 4.0, 5.0), rectangle_new(2.5, 2.5), circle_new(0.5) };
    size_t const count = sizeof shapes / sizeof shapes[0];
    qsort(shapes, count, sizeof shapes[0], by_area);
    double total = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        shape const* s = shapes[i];
        printf("%-9s #%d: area %7.3f, perimeter %7.3f\n", s->vtable->name, s->id, s->vtable->area(s), s->vtable->perimeter(s));
        total += s->vtable->area(s);
    }
    printf("total area %.3f\n", total);
    for (size_t i = 0; i < count; ++i)
        shapes[i]->vtable->destroy(shapes[i]);
    return 0;
}
