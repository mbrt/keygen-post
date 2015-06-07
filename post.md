# Keygenning with KLEE
In the past weeks I enjoyed working on reversing a software (don't ask me the name), to study how serial numbers are validated. The story the user has to follow is the same for many others: download the trial, pay, get the serial number, use it in the annoying nag screen to get the fully functional version.

Since my purpose is to not damage the company developing the software, I will not mention the name of the software, nor I will publish the code of the final key generator. My goal is instead to study a real case of serial number validation, and to highlight its weaknesses.

In this post we are going to take a look at the steps I followed to reverse the serial validation process and to make a key generator using [KLEE](http://klee.github.io/) symbolic virtual machine. We are not going to follow all the details on the reversing part, since you cannot reproduce them on your own. We will concentrate our thoughts on the key-generator itself: that is the most interesting part.

## Getting acquainted
The software is an `x86` executable, with no anti-debugging, nor anti-reversing techniques. When started it presents a nag screen asking for a registration composed by: customer number, serial number and a mail address. This is a fairly common software.

## Tools of the trade
First steps in the reversing are devoted to find all the interesting functions to analyze. To do this I used [IDA Pro](https://www.hex-rays.com/products/ida/) with Hex-Rays decompiler, and the [WinDbg](https://msdn.microsoft.com/en-us/library/windows/hardware/ff551063(v=vs.85).aspx) debugger. For the last part I used the [KLEE](http://klee.github.io/) symbolic virtual machine under Linux, the [gcc compiler](https://gcc.gnu.org/) and some bash scripting. The actual key generator was a simple [WPF](https://msdn.microsoft.com/en-us/library/aa970268%28v=vs.110%29.aspx) application.

Let me skip the first part, since it is not very interesting. You can find many other articles on the web that can guide you trough basic reversing techniques with IDA Pro. I only kept in mind some simple rules, while going forward:
* always rename functions playing with interesting data, even if you don't know precisely what they do. A name like `license_validation_unknown_8` is always better than a default like `sub_46fa39`;
* similarly, rename data whenever you find it interesting;
* change data types when you are sure they are wrong: use structs and arrays in case of aggregates;
* follow cross references of data and functions to expand your collection;
* validate your beliefs with the debugger if possible. For example, if you think a variable contains the serial, break with the debugger and see if it is the case.

## Big picture
When I collected the most interesting functions, I tried to understand the high level flow and the simpler functions. Here is the main variables and types used in the validation process. As a note for the reader: most of them has been actually purged from uninteresting details.

```C
enum {
    ERROR,
    STANDARD,
    PRO
} license_type = ERROR;
```
Here we have a global variable providing the type of the license, used to enable and disable features of the application.

```C
enum result_t {
    INVALID,
    VALID,
    VALID_IF_LAST_VERSION
};
```
This is a convenient `enum` used as a result for the validation. `INVALID` and `VALID` values are pretty self-explanatory.  `VALID_IF_LAST_VERSION` tells that this registration is valid only if the current software version is the last available. The reasons for this strange possibility will be clear shortly.

```C
enum { HEADER_SIZE = 8192 };
struct {
    int header[HEADER_SIZE];
    int data[1000000];
} mail_digest_table;
```
This is a data structure, containing digests of mail addresses of
known registered users. This is a pretty big file embedded in the executable itself. During startup, a resource is extracted in a temporary file and its contents copied into this struct. Each element of the `header` vector is an offset pointing inside the `data` vector.

Here we have a pseudo-C code for the registration check, that uses data types and variables explained above:
```C
enum result_t check_registration(int serial, int customer_num, const char* mail) {
    // validate serial number
    license_type = get_license_type(serial);
    if (license_type == ERROR)
        return INVALID;
    
    // validate customer number
    int expected_customer = compute_customer_number(serial, mail);
    if (expected_customer != customer_num)
        return INVALID;
    
    // validate w.r.t. known registrations
    int index = get_index_in_mail_table(serial);
    if (index > HEADER_SIZE)
        return VALID_IF_LAST_VERSION;
    int mail_digest = compute_mail_digest(mail);
    for (int i = 0; i < 3; ++i) {
        if (mail_digest_table[index + i] == mail_digest)
            return VALID;
    }
    return INVALID;
}
```

The validation is divided in three main parts:
* serial number must be valid by itself;
* serial number, combined with mail address have to correspond to the actual customer number;
* there have to be a correspondence between serial number and mail address, stored in a static table in the binary.

The last point is a little bit unusual. Let me restate it in this way: whenever a customer buy the software, the customer table gets updated with its data and become available in the *next* version of the software (because it is embedded in the binary and not downloaded trough the internet). This explains the `VALID_IF_LAST_VERSION` check: if you buy the software today, the current version does not contain your data. You are still allowed to get a "pro" version until a new version is released. In that moment you are forced to update to that new version, so the software can verify your registration with the updated table. Here is a pseudo-code of that check:

```C
switch (check_registration(serial, customer, mail) {
case VALID:
    // the registration is OK! activate functionalities
    activate_pro_functionality();
    break;
case VALID_IF_LAST_VERSION:
    {
        // check if the current version is the last, by
        // using the internet.
        int current_version = get_current_version();
        int last_version = get_last_version();
        if (current_version == last_version)
            // OK for now: a new version is not available
            activate_pro_functionality();
        else
            // else, force the user to download the new version
            // before proceed
            ask_download();
    }
    break;
case INVALID:
    // registration is not valid
    handle_invalid_registration();
    break;
}
```

The version check is done by making an HTTP request to a specific page that returns a page having only the last version number of the software. Don't ask me why the protection is not completely server side but involves static tables, version checks and things like that. I don't know!

Anyway, this is the big picture of the registration validation functions, and this is pretty boring. Let's move on to the interesting part. You may notice that I provided code for the main procedure, but not for the helper functions like `get_license_type`, `compute_customer_number`, and so on. This is because I did not have to reverse them. They contains a lot of arithmetical and logical operations on registration data, and they are very difficult to understand. The good news is that we do not have to understand, we need only to reverse them!

## KLEE
KLEE is a symbolic virtual machine that operates on [LLVM](http://llvm.org/) byte code, used for software verification purposes. KLEE is capable to automatically generate tests achieving high code coverage. KLEE is also able to find memory errors such as out of bound array accesses and many other common errors. To do that, it needs an LLVM byte code version of the program, symbolic variables and (optionally) assertions. I have also prepared a [Docker image](https://registry.hub.docker.com/u/mbrt/klee/) with `clang` and `klee` already configured and ready to use. So, you have no excuses to try it out! Take this example function:

```C
bool check_arg(int a) {
    if (a > 10)
        return false;
    else if (a <= 10)
        return true;
    return false; // not reachable
}
```

This is actually a silly example, I know, but let's pretend to verify this function with this main:

```C
#include <assert.h>
#include <klee/klee.h>

int main() {
    int input;
    klee_make_symbolic(&input, sizeof(int), "input");
    return check_arg(input);
}
```

In there we have a symbolic variable used as input for the function to be tested. We can also modify it to include an assertion:

```C
bool check_arg(int a) {
    if (a > 10)
        return false;
    else if (a <= 10)
        return true;
    klee_assert(false);
    return false; // not reachable
}
```

We can now use `clang` to compile the program to LLVM intermediate representation and run the test generation with the `klee` command:

```
clang -emit-llvm -g -o test.ll -c test.c
klee test.ll
```

We get this output:

```
KLEE: output directory is "/work/klee-out-0"

KLEE: done: total instructions = 26
KLEE: done: completed paths = 2
KLEE: done: generated tests = 2
```

KLEE will generate test cases for the `input` variable, trying to cover all the possible execution paths and to make the provided assertions to fail (if any given). In this case we have two execution paths and two generated test cases, covering them. Test cases are in the output directory (in this case `/work/klee-out-0`). The soft link `klee-last` is also provided for convenience, pointing to the last output directory. A bunch of files were created, including the two test cases named `test000001.ktest` and `test000002.ktest`. These are binary files, which can be examined with `ktest-tool` utility. Let's try it:

```
$ ktest-tool --write-ints klee-last/test000001.ktest 
ktest file : 'klee-last/test000001.ktest'
args       : ['test.ll']
num objects: 1
object    0: name: 'input'
object    0: size: 4
object    0: data: 2147483647
```

And the second one:

```
$ ktest-tool --write-ints klee-last/test000002.ktest 
...
object    0: data: 0
```

In these test files, KLEE reports the command line arguments, the symbolic objects along with their size and the value provided for the test. To cover the whole program, we need `input` variable to get a value greater than 10 and one below or equal. You can see that this is the case: in the first test case the value 2147483647 is used, covering the first branch, while 0 is provided for the second, covering the other branch.

So far, so good. But what if we change the function in this way?

```C
bool check_arg(int a) {
    if (a > 10)
        return false;
    else if (a < 10)    // instead of <=
        return true;
    klee_assert(false);
    return false;       // now reachable
}
```

We get this output:

```
$ klee test.ll 
KLEE: output directory is "/work/klee-out-2"
KLEE: ERROR: /work/test.c:9: ASSERTION FAIL: 0
KLEE: NOTE: now ignoring this error at this location

KLEE: done: total instructions = 27
KLEE: done: completed paths = 3
KLEE: done: generated tests = 3
```

And this is the `klee-last` directory contents:

```
$ ls klee-last/
assembly.ll   run.istats        test000002.assert.err  test000003.ktest
info          run.stats         test000002.ktest       warnings.txt
messages.txt  test000001.ktest  test000002.pc
```

Note the `test000002.assert.err` file. If we examine its corresponding test file, we have:

```
$ ktest-tool --write-ints klee-last/test000002.ktest 
ktest file : 'klee-last/test000002.ktest'
...
object    0: data: 10
```

As we had expected, the assertion fails while `input` value is 10. So, as we now have three execution paths, we also have three test cases, and the whole program gets covered. KLEE provides also the possibility to replay the tests with the real program, but we are not interested in it now. You can see an usage example at this [KLEE tutorial](http://klee.github.io/tutorials/testing-function/#replaying-a-test-case).

KLEE abilities to find execution paths of an application are very good. According to the [OSDI 2008 paper](http://llvm.org/pubs/2008-12-OSDI-KLEE.html), KLEE has been successfully used to test all 89 stand-alone programs in GNU COREUTILS and the equivalent busybox port, finding previously undiscovered bugs, errors and inconsistencies. The achieved code coverage were more than 90% per tool. Pretty awesome!

But, you may ask: [The question is, who cares?](https://www.youtube.com/watch?v=j_T9YtA1mRQ). You will see it in a moment.

## KLEE to reverse a function

As we have a powerful tool to find execution paths, we can use it to find the path we are interested in. As showed by the nice [symbolic maze](https://feliam.wordpress.com/2010/10/07/the-symbolic-maze/) post of Feliam, we can use KLEE to solve a maze. The idea is simple but very powerful: flag the portion of code you interested in with a `klee_assert(0)` call, causing KLEE to highlight the test case able to reach that point. In the maze example, this is as simple as changing a `read` call with a `klee_make_symbolic` and the `prinft("You win!\n")` with the already mentioned `klee_assert(0)`. Test cases triggering this assertion are the one solving the maze!

For a concrete example, let's suppose we have this function:

```C
int magic_computation(int input) {
    for (int i = 0; i < 32; ++i)
        input ^= 1 << i;
    return input;
}
```

And we want to know for what input we get the output 253. A main that tests this could be:

```C
int main(int argc, char* argv[]) {
    int input = atoi(argv[1]);
    int output = magic_computation(input);
    if (output == 253)
        printf("You win!\n");
    else
        printf("You loose\n);
    return 0;
}
```

KLEE can resolve this problem for us, if we provide symbolic inputs and actually an assert to trigger:

```C
int main(int argc, char* argv[]) {
    int input, result;
    klee_make_symbolic(&input, sizeof(int), "input");
    result = magic_computation(input);
    if (result == 153)
        klee_assert(0);
    return 0;
}
```

Run KLEE and print the result:

```
$ clang -emit-llvm -g -o magic.ll -c magic.c
$ klee magic.ll
$ ktest-tool --write-ints klee-last/test000001.ktest
ktest file : 'klee-last/test000001.ktest'
args       : ['magic.ll']
num objects: 1
object    0: name: 'input'
object    0: size: 4
object    0: data: -154
```

The answer is -154. Let's test it:

```
$ gcc magic.c
$ ./a.out -154
You win!
```

Yes!

## KLEE, libc and command line arguments

Not all the functions are so simple. At least we could have calls to the C standard library such as `strlen`, `atoi`, and such. We cannot link our test code with the system available C library, as it is not inspectable by KLEE. For example:

```C
int main(int argc, char* argv[]) {
    int input = atoi(argv[1]);
    return input;
}
```

If we run it with KLEE we get this error:

```
$ clang -emit-llvm -g -o atoi.ll -c atoi.c
$ klee atoi.ll 
KLEE: output directory is "/work/klee-out-4"
KLEE: WARNING: undefined reference to function: atoi
KLEE: WARNING ONCE: calling external: atoi(0)
KLEE: ERROR: /work/prova.c:5: failed external call: atoi
KLEE: NOTE: now ignoring this error at this location
...
```

To fix this we can use the KLEE uClibc and POSIX runtime. Taken from the help:

*"If we were running a normal native application, it would have been linked with the C library, but in this case KLEE is running the LLVM bitcode file directly. In order for KLEE to work effectively, it needs to have definitions for all the external functions the program may call. Similarly, a native application would be running on top of an operating system that provides lower level facilities like write(), which the C library uses in its implementation. As before, KLEE needs definitions for these functions in order to fully understand the program. We provide a POSIX runtime which is designed to work with KLEE and the uClibc library to provide the majority of operating system facilities used by command line applications"*.

Let's try to use these facilities to test our `atoi` function:

```
$ klee --optimize --libc=uclibc --posix-runtime atoi.ll --sym-args 0 1 3
KLEE: NOTE: Using klee-uclibc : /usr/local/lib/klee/runtime/klee-uclibc.bca
KLEE: NOTE: Using model: /usr/local/lib/klee/runtime/libkleeRuntimePOSIX.bca
KLEE: output directory is "/work/klee-out-5"
KLEE: WARNING ONCE: calling external: syscall(16, 0, 21505, 70495424)
KLEE: ERROR: /tmp/klee-uclibc/libc/stdlib/stdlib.c:526: memory error: out of bound pointer
KLEE: NOTE: now ignoring this error at this location

KLEE: done: total instructions = 5756
KLEE: done: completed paths = 68
KLEE: done: generated tests = 68
```

And KLEE founds the possible of bound access in our program. Because you know, our program is bugged :) Before to jump and fix our code, let me briefly explain what these new flags did:

* `--optimize`: this is for dead code elimination. It is actually a good idea to use this flag when working with non-trivial applications, since it speed things up;
* `--libc=uclibc` and `--posix-runtime`: these are the aforementioned options for uClibc and POSIX runtime;
* `--sym-args 0 1 3`: this flag tells KLEE to run the program with minimum 0 and maximum 1 argument of length 3, and make the arguments symbolic.

Note that adding `atoi` function to our code, adds 68 execution paths to the program. Using many libc functions in our code adds complexity, so we have to use them carefully when we want to reverse complex functions.

Let now make the program safe by adding a check to the command line argument length. Let's also add an assertion, because it is fun :)

```C
#include <stdlib.h>
#include <assert.h>
#include <klee/klee.h>

int main(int argc, char* argv[]) {
    int result = argc > 1 ? atoi(argv[1]) : 0;
    if (result == 42)
        klee_assert(0);
    return result;
}
```

We could also have written `klee_assert(result != 42)`, and get the same result. No matter what solution we adopt, now we have to run KLEE as before:

```
$ clang -emit-llvm -g -o atoi.ll -c atoi.c
$ klee --optimize --libc=uclibc --posix-runtime atoi.ll --sym-args 0 1 3
KLEE: NOTE: Using klee-uclibc : /usr/local/lib/klee/runtime/klee-uclibc.bca
KLEE: NOTE: Using model: /usr/local/lib/klee/runtime/libkleeRuntimePOSIX.bca
KLEE: output directory is "/work/klee-out-6"
KLEE: WARNING ONCE: calling external: syscall(16, 0, 21505, 53243904)
KLEE: ERROR: /work/atoi.c:8: ASSERTION FAIL: 0
KLEE: NOTE: now ignoring this error at this location

KLEE: done: total instructions = 5962
KLEE: done: completed paths = 73
KLEE: done: generated tests = 69
```

Here we go! We have fixed our bug. KLEE is also able to find an input to make the assertion fail:

```
$ ls klee-last/ | grep err
test000016.assert.err
$ ktest-tool klee-last/test000016.ktest
ktest file : 'klee-last/test000016.ktest'
args       : ['atoi.ll', '--sym-args', '0', '1', '3']
num objects: 3
...
object    1: name: 'arg0'
object    1: size: 4
object    1: data: '+42\x00'
...
```

And the answer is the string "+42"... as we know.

There are many other KLEE options and functionalities, but let's move on and try to solve our original problem. Interested readers can find a good tutorial, for example, in [How to Use KLEE to Test GNU Coreutils](http://klee.github.io/tutorials/testing-coreutils/).

## KLEE keygen

Now that we know basic KLEE commands, we can try to apply them to our particular case. We have understood some of the validation algorithm, but we don't know the computation details. They are just a mess of arithmetical and logical operations that we are tired to analyze.

Here is our plan:

* we need at least a valid customer number, a serial number and a mail address;
* more ambitiously we want a list of them, to make a key generator.

This is a possibility:

```C
// copy and paste of all the registration code
enum {
    ERROR,
    STANDARD,
    PRO
} license_type = ERROR;
// ...
enum result_t check_registration(int serial, int customer_num, const char* mail);
// ...

int main(int argc, char* argv[]) {
    int serial, customer;
    char mail[10];
    enum result_t result;
    klee_make_symbolic(&serial, sizeof(serial), "serial");
    klee_make_symbolic(&customer, sizeof(customer), "customer");
    klee_make_symbolic(&mail, sizeof(mail), "mail");

    valid = check_registration(serial, customer, mail);
    valid &= license_type == PRO;
    klee_assert(!valid);
}
```

Super simple. Copy and paste everything, make the inputs symbolic and assert a certain result (negated, of course).

No! That's not so simple. This is actually the most difficult part of the game. The high level picture of the validation algorithm I have presented is an ideal one. The `check_registration` function is actually a big set of auxiliary functions and data, very tightened with other parts of the program. Even if we now know the most interesting functions, we need to know how much of the related code, is useful or not. We cannot throw everything in our key generator, since every function brings itself other related data and functions. In this way we will end up having the whole program in it. We need to minimize the code KLEE has to analyze, otherwise it will only waste time trying to find something that is actually to difficult to find.

[...] Pictures here.

This is actually not the only problem I've found in this step. External constraints must be carefully considered. For example the [time](http://www.cplusplus.com/reference/ctime/time/) function can be handled by KLEE itself. KLEE tries to generate useful values even from that function. This is good if we want to test bugs related to a strange current time, but in our case, since the code will be executed by the program *at a particular time*, we are only interested in the value provided at that time. We don't want KLEE traits this function as symbolic; we only want the right time value. To solve that problem, I have replaced all the calls to `time` to a `my_time` function, returning a fixed value, defined in the source code.

Another problem comes from the extraction of the functions from their outer context. Often code is written with *implicit conventions* in mind. These are not self-evident in the code because checks are avoided. A trivial example is the null terminator and valid ASCII characters in strings. KLEE do not assumes those constraints, but the validation code do. This is because GUI provides only valid strings. A less trivial example is that the mail address is always passed lowercase from the GUI to the lower level application logic. This is not self-evident if you do not follow every step from the user input to the actual computations with the data.

The solution to this latter problem is to provide KLEE those constraints:

```C
char mail[10];
char c;
klee_make_symbolic(mail, sizeof(mail), "mail");
for (i = 0; i < sizeof(mail) - 1; ++i) {
    c = mail[i];
    klee_assume( (c >= '0' & c <= '9') | (c >= 'a' & c <= 'z') | c == '\0' );
}
klee_assume(mail[sizeof(mail) - 1] == '\0');
```

Logical operators inside `klee_assume` function are bitwise and not logical (i.e. `&` and `|` instead of `&&` and `||`) because they are simpler, since they do not add the extra branches required by lazy operators.

## TODO

Let's deconstruct the big picture of the registration check presented above in this perspective. We will re-construct it in a way KLEE is able to solve the problem.
