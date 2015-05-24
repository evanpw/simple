// For non-standard getline
#define _GNU_SOURCE

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "library.h"

#ifdef __APPLE__

extern void Z4main(void);

int main(void)
{
    Z4main();
    return 0;
}

#else

extern void _Z4main(void);

int main(void)
{
    _Z4main();
    return 0;
}

#endif

void fail(const char* str)
{
    puts(str);
    exit(1);
}

//// Reference counting ////////////////////////////////////////////////////////

#define Spl_INCREF(p) _incref((SplObject*)(p))
#define Spl_DECREF(p) _decref((SplObject*)(p))
#define Spl_DECREF_NO_FREE(p) _decrefNoFree((SplObject*)(p))

void _incref(SplObject* object)
{
    if (object == NULL || IS_TAGGED(object)) return;

    ++(object->refCount);
}

int64_t _decrefNoFree(SplObject* object)
{
    if (object == NULL || IS_TAGGED(object)) return 1;

    --object->refCount;

    if (object->refCount < 0)
    {
        fail("*** Exception: Reference count is negative");
    }

    return object->refCount;
}

static void destroy(SplObject* object);

void _decref(SplObject* object)
{
    if (object == NULL || IS_TAGGED(object)) return;

    --object->refCount;
    if (object->refCount < 0)
    {
        fail("*** Exception: Reference count is negative");
    }
    else if (object->refCount > 0)
    {
        return;
    }

    if (object->constructorTag <= MAX_STRUCTURED_TAG)
    {
        destroy(object);
    }
    else
    {
        myfree(object);
    }
}

// Recursively destroy object and decrement the reference count of its children.
// Does a depth-first traversal of the object graph in constant stack space by
// storing back-tracking pointers in the child pointers themselves.
// See "Deutsch-Schorr-Waite pointer reversal"
static void destroy(SplObject* object)
{
    if (object->refCount != 0)
    {
        fail("*** Exception: Destroying object with positive reference count");
    }

    SplObject* back = NULL;
    SplObject* next = object;

    while (1)
    {
continue_main_loop:
        if (next->pointerFields)
        {
            uint64_t mask = 1;
            SplObject** p = (SplObject**)(next + 1);

            while (next->pointerFields)
            {
                if (next->pointerFields & mask)
                {
                    // Decrement child. If also at refcount 0, then recurse
                    if (_decrefNoFree(*p) == 0)
                    {
                        // Rotate back, next, *p cyclically to the left
                        SplObject* tmp = back;
                        back = next;
                        next = *p;
                        *p = tmp;
                        goto continue_main_loop;
                    }
                    else
                    {
                        next->pointerFields &= ~mask;
                    }
                }

                mask <<= 1;
                ++p;
            }
        }

        myfree(next);

        if (back)
        {
            // Backtrack
            next = back;

            uint64_t mask = 1;
            SplObject** p = (SplObject**)(next + 1);
            while (!(next->pointerFields & mask))
            {
                mask <<= 1;
                ++p;
            }

            next->pointerFields &= ~mask;
            back = *p;
        }
        else
        {
            break;
        }
    }
}

//// Ints //////////////////////////////////////////////////////////////////////

int64_t toInt(int64_t n)
{
    return (n << 1) + 1;
}

int64_t fromInt(int64_t n)
{
    return n >> 1;
}

//// Strings ///////////////////////////////////////////////////////////////////

char* strContent(String* s)
{
    return (char*)(s + 1);
}

String* makeStr(const char* data)
{
    String* result = mymalloc(sizeof(SplObject) + strlen(data) + 1);
    result->constructorTag = STRING_TAG;
    result->refCount = 0;
    result->pointerFields = 0;
    result->markBit = 0;
    strcpy(strContent(result), data);

    return result;
}

int64_t strLength(String* s)
{
    return toInt(strlen(strContent(s)));
}

String* strSlice(String* s, int64_t tPos, int64_t tLength)
{
    int64_t pos = fromInt(tPos);
    int64_t length = fromInt(tLength);

    if (pos < 0 || pos >= strLength(s))
    {
        fail("*** Exception: String slice position out of range");
    }
    else if (length < 0 || pos + length >= strLength(s))
    {
        fail("*** Exception: String slice length out of range");
    }

    String* result = mymalloc(sizeof(SplObject) + length + 1);
    result->constructorTag = STRING_TAG;
    result->refCount = 0;
    result->pointerFields = 0;
    result->markBit = 0;

    strncpy(strContent(result), strContent(s) + pos, length);
    strContent(result)[length] = '\0';

    return result;
}

String* strCat(String* lhs, String* rhs)
{
    size_t n1 = strlen(strContent(lhs));
    size_t n2 = strlen(strContent(rhs));

    String* result = mymalloc(sizeof(SplObject) + n1 + n2 + 1);
    result->constructorTag = STRING_TAG;
    result->refCount = 0;
    result->pointerFields = 0;
    result->markBit = 0;

    char* dest = strContent(result);
    strncpy(dest, strContent(lhs), n1);
    strncpy(dest + n1, strContent(rhs), n2);
    *(dest + n1 + n2) = '\0';

    return result;
}

int64_t strAt(String* s, int64_t n)
{
    int64_t idx = fromInt(n);

    if (idx < 0 || idx >= strLength(s))
    {
        fail("*** Exception: String index out of range");
    }

    return toInt(strContent(s)[idx]);
}

#define IS_EMPTY(xs) ((xs)->constructorTag == 1)

String* strFromList(List* list)
{
    // Get length
    List* p = list;
    size_t length = 0;
    while (!IS_EMPTY(list))
    {
        list = list->next;
        ++length;
    }

    String* result = mymalloc(sizeof(SplObject) + length + 1);
    result->constructorTag = STRING_TAG;
    result->refCount = 0;
    result->pointerFields = 0;
    result->markBit = 0;

    char* out = strContent(result);
    while (!IS_EMPTY(list))
    {
        int64_t c = fromInt((int64_t)list->value);
        if (c < 0 || c > 255)
        {
            fail("*** Exception: Char value out of range");
        }

        *out++ = c;
        list = list->next;
    }

    *out++ = '\0';

    return result;
}

String* show(int64_t x)
{
    int64_t value = fromInt(x);

    String* result = mymalloc(sizeof(SplObject) + 20 + 1);
    result->constructorTag = STRING_TAG;
    result->refCount = 0;
    result->pointerFields = 0;
    result->markBit = 0;

    sprintf(strContent(result), "%" PRId64, value);
    return result;
}

//// I/O ///////////////////////////////////////////////////////////////////////

int64_t read()
{
    int64_t result;
    scanf("%" PRId64, &result);

    return toInt(result);
}

String* readLine()
{
    char* line = NULL;
    size_t len = 0;

    ssize_t read = getline(&line, &len, stdin);

    if (read == -1)
    {
        return makeStr("");
    }
    else
    {
        String* result = makeStr(line);
        myfree(line);

        return result;
    }
}

void print(String* s)
{
    printf("%s\n", strContent(s));
}

void die(String* s)
{
    fprintf(stderr, "%s\n", strContent(s));
    exit(1);
}

//// Garbage collector /////////////////////////////////////////////////////////

void markRecursive(SplObject* object)
{
    if (object->markBit) return;

    printf("Mark: %p\n", object);
    object->markBit = 1;

    SplObject** p = (SplObject**)(object + 1);
    uint64_t fields = object->pointerFields;
    while (fields)
    {
        if ((fields & 1) && !IS_TAGGED(*p))
        {
            markRecursive(*p);
        }

        fields >>= 1;
        ++p;
    }
}

#ifdef __APPLE__
extern void walkStackC(uint64_t*, uint64_t*, uint64_t*, uint64_t*) asm("walkStackC") ;
#endif

void walkStackC(uint64_t* stackTop, uint64_t* stackBottom, uint64_t* framePointer, uint64_t* globalVarTable)
{
    uint64_t* top = stackTop;
    uint64_t* bottom = framePointer;

    while (1)
    {
        printf("Stack frame: %p %p\n", top, bottom);
        for (uint64_t* p = top; p < bottom; ++p)
        {
            printf("%p: %p\n", p, (void*)*p);

            SplObject* object = (SplObject*)*p;
            if (object && !IS_TAGGED(object))
            {
                markRecursive(object);
            }
        }
        printf("\n");

        if (bottom == stackBottom)
        {
            break;
        }

        top = bottom + 2;
        bottom = (uint64_t*)*bottom;
    }

    printf("Globals:\n");
    uint64_t numGlobals = *globalVarTable;
    uint64_t** p = (uint64_t**)(globalVarTable + 1);
    for (size_t i = 0; i < numGlobals; ++i)
    {
        printf("%p: %p\n", p, (void*)**p);

        SplObject* object = (SplObject*)**p;
        if (object && !IS_TAGGED(object))
        {
            markRecursive(object);
        }

        ++p;
    }
}

typedef struct FreeBlock
{
    uint64_t size;
    uint64_t tag;
    struct FreeBlock* nextBlock;
} FreeBlock;

uint8_t* firstChunk = NULL;
uint8_t* currentChunk = NULL;
size_t chunkSize = 0;
FreeBlock* freeList = NULL;

void* mymalloc(size_t size)
{
    // First allocation: need to create a heap
    if (!firstChunk)
    {
        size_t newSize = 4 << 20;
        void* newChunk = mmap(0, newSize, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (newChunk == MAP_FAILED)
        {
            return NULL;
        }

        //printf("mymalloc chunk: %p\n", newChunk);

        firstChunk = currentChunk = newChunk;
        chunkSize = newSize;

        *(void**)currentChunk = NULL;

        freeList = (FreeBlock*)(currentChunk + sizeof(void*));
        freeList->size = newSize - sizeof(void*);
        freeList->tag = FREE_BLOCK_TAG;
        freeList->nextBlock = NULL;
    }

    // Search for a large-enough free block
    FreeBlock* p = freeList;
    FreeBlock* prev = NULL;
    while (p)
    {
        // This block is large enough.
        if (p->size >= size + 8)
        {
            uint64_t* result = (uint64_t*)p;

            // If this is large enough to accomodate the current request and
            // possibly another one, then split it.
            FreeBlock* nextBlock;
            if (p->size - size - 8 >= sizeof(SplObject) + 8)
            {
                FreeBlock* newBlock = (FreeBlock*)((uint8_t*)p + size + 8);
                newBlock->size = p->size - size - 8;
                newBlock->tag = FREE_BLOCK_TAG;
                newBlock->nextBlock = p->nextBlock;

                nextBlock = newBlock;
            }
            else // Otherwise, use the whole thing
            {
                uint64_t* result = (uint64_t*)p;
                nextBlock = p->nextBlock;
            }

            if (prev)
            {
                prev->nextBlock = nextBlock;
            }
            else
            {
                freeList = nextBlock;
            }

            *result = p->size - 8;
            return (void*)(result + 1);
        }

        prev = p;
        p = p->nextBlock;
    }

    // Current heap is full: need to allocate another chunk
    size_t newSize = 2 * chunkSize;
    while (newSize < size + sizeof(void*)) newSize *= 2;

    void* newChunk = mmap(0, newSize, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (newChunk == MAP_FAILED)
    {
        return NULL;
    }

    //printf("mymalloc chunk: %p\n", newChunk);

    *(void**)currentChunk = newChunk;

    currentChunk = newChunk;
    chunkSize = newSize;
    *(void**)currentChunk = NULL;

    FreeBlock* newHead = (FreeBlock*)(currentChunk + sizeof(void*));
    newHead->size = newSize - sizeof(void*);
    newHead->tag = FREE_BLOCK_TAG;
    newHead->nextBlock = freeList;
    freeList = newHead;

    return mymalloc(size);
}

void myfree(void* p)
{
    void* block = (uint8_t*)p - 8;
    size_t size = *(uint64_t*)block;
}
