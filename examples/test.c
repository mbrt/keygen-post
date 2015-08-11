#include <assert.h>
#include <klee/klee.h>

#define FALSE 0
#define TRUE 1
typedef int BOOL;

BOOL check_arg(int a) {
    if (a > 10)
        return FALSE;
    else if (a <= 10)
        return TRUE;
    return FALSE; // not reachable
}

int main() {
    int input;
    klee_make_symbolic(&input, sizeof(int), "input");
    return check_arg(input);
}
