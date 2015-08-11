#include <stdio.h>

int magic_computation(int input) {
    int i;
    for (i = 0; i < 32; ++i)
        input ^= 1 << i;
    return input;
}

int main(int argc, char* argv[]) {
    int input = atoi(argv[1]);
    int output = magic_computation(input);
    if (output == 253)
        printf("You win!\n");
    else
        printf("You lose\n");
    return 0;
}
