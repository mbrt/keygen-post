// test.c
//
// compile with `clang -emit-llvm -g -o test.ll -c test.c`
// test with `klee test.ll`
//
#include <assert.h>
#include <klee/klee.h>

int check_arg(int a) {
    if (a > 10)
        return 0;
    else if (a < 10)
        return 1;
    klee_assert(0);
    return 0; // not reachable
}

int main(int argc, char* argv[]) {
    int input;
    klee_make_symbolic(&input, sizeof(int), "input");
    return check_arg(input);
}
