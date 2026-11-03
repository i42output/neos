// Containers: a type-generic growable array (macros), a hash map of strings to counts (open addressing), a singly linked list
// (reversed in place), and a binary search tree (in-order traversal) - the counts of the words of a text
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- a growable array of any element type

#define VECTOR(T) struct { T* data; size_t size; size_t capacity; }
#define VECTOR_PUSH(v, value) \
    do { \
        if ((v).size == (v).capacity) \
        { \
            (v).capacity = (v).capacity ? (v).capacity * 2u : 8u; \
            (v).data = realloc((v).data, (v).capacity * sizeof *(v).data); \
        } \
        (v).data[(v).size++] = (value); \
    } while (0)

// --- a hash map: string -> count

typedef struct { char* key; int count; } entry;
typedef struct { entry* slots; size_t capacity; size_t size; } map;

static uint64_t fnv1a(char const* s)
{
    uint64_t h = 14695981039346656037ull;
    while (*s)
    {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ull;
    }
    return h;
}

static entry* map_find(map* m, char const* key)
{
    for (size_t i = fnv1a(key) & (m->capacity - 1u);; i = (i + 1u) & (m->capacity - 1u))
        if (m->slots[i].key == NULL || strcmp(m->slots[i].key, key) == 0)
            return &m->slots[i];
}

static void map_grow(map* m)
{
    map grown = { calloc(m->capacity * 2u, sizeof(entry)), m->capacity * 2u, m->size };
    for (size_t i = 0; i < m->capacity; ++i)
        if (m->slots[i].key != NULL)
            *map_find(&grown, m->slots[i].key) = m->slots[i];
    free(m->slots);
    *m = grown;
}

static void map_add(map* m, char const* key)
{
    if ((m->size + 1u) * 4u > m->capacity * 3u)
        map_grow(m);
    entry* const e = map_find(m, key);
    if (e->key == NULL)
    {
        e->key = strdup(key);   // (C23)
        ++m->size;
    }
    ++e->count;
}

// --- a linked list

typedef struct node { char const* word; struct node* next; } node;

static node* reverse(node* list)
{
    node* reversed = nullptr;
    while (list != nullptr)
    {
        node* const next = list->next;
        list->next = reversed;
        reversed = list;
        list = next;
    }
    return reversed;
}

// --- a binary search tree, by count then word

typedef struct tree { entry const* e; struct tree* left; struct tree* right; } tree;

static bool before(entry const* a, entry const* b)
{
    return a->count != b->count ? a->count > b->count : strcmp(a->key, b->key) < 0;
}

static tree* insert(tree* t, entry const* e)
{
    if (t == nullptr)
    {
        t = malloc(sizeof *t);
        *t = (tree){ e, nullptr, nullptr };
    }
    else if (before(e, t->e))
        t->left = insert(t->left, e);
    else
        t->right = insert(t->right, e);
    return t;
}

static void walk(tree const* t, int* remaining)
{
    if (t == nullptr || *remaining == 0)
        return;
    walk(t->left, remaining);
    if (*remaining > 0)
    {
        printf("  %-6s %d\n", t->e->key, t->e->count);
        --*remaining;
    }
    walk(t->right, remaining);
}

static void destroy(tree* t)
{
    if (t != nullptr)
    {
        destroy(t->left);
        destroy(t->right);
        free(t);
    }
}

int main(void)
{
    char text[] =
        "it was the best of times it was the worst of times it was the age of wisdom it was the age of foolishness "
        "it was the epoch of belief it was the epoch of incredulity it was the season of light it was the season of darkness";
    VECTOR(char const*) words = { 0 };
    for (char* w = strtok(text, " "); w != nullptr; w = strtok(nullptr, " "))
        VECTOR_PUSH(words, w);
    printf("%zu words\n", words.size);

    map counts = { calloc(8, sizeof(entry)), 8, 0 };
    for (size_t i = 0; i < words.size; ++i)
        map_add(&counts, words.data[i]);
    printf("%zu distinct (in %zu slots)\n", counts.size, counts.capacity);

    tree* t = nullptr;
    for (size_t i = 0; i < counts.capacity; ++i)
        if (counts.slots[i].key != nullptr)
            t = insert(t, &counts.slots[i]);
    printf("the most frequent:\n");
    int remaining = 6;
    walk(t, &remaining);

    node* list = nullptr;
    node nodes[8];
    for (size_t i = 0; i < 8; ++i)
    {
        nodes[i] = (node){ words.data[i], list };
        list = &nodes[i];
    }
    printf("the first 8, through a list built backwards and reversed:");
    for (node const* n = reverse(list); n != nullptr; n = n->next)
        printf(" %s", n->word);
    printf("\n");

    VECTOR(int) squares = { 0 };
    for (int i = 1; i <= 10; ++i)
        VECTOR_PUSH(squares, i * i);
    int sum = 0;
    for (size_t i = 0; i < squares.size; ++i)
        sum += squares.data[i];
    printf("sum of the first 10 squares: %d\n", sum);

    destroy(t);
    for (size_t i = 0; i < counts.capacity; ++i)
        free(counts.slots[i].key);
    free(counts.slots);
    free(words.data);
    free(squares.data);
    return 0;
}
