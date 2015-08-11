#include <stdlib.h>
#include <assert.h>
#include <klee/klee.h>

int main(int argc, char* argv[]) {
    int result = argc > 1 ? atoi(argv[1]) : 0;
    if (result == 42)
        klee_assert(0);
    return result;
}
