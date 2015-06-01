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
KLEE is a symbolic virtual machine that operates on [LLVM](http://llvm.org/) byte code, used for software verification purposes. KLEE is capable to automatically generate tests achieving high coverage on programs. KLEE is also able to find memory errors such as out of bound array access and many other common errors. To do that, it needs an LLVM byte code version of the program, symbolic variables and assertions. Take this example function:

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

And than modify the function to include an assertion:

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

Compile it to LLVM intermediate representation and run the test generation:

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

KLEE will generate test cases for the `input` variable, trying to cover all the possible execution paths and make the provided assertions to fail (if any given). In this case we have two execution paths and two generated test cases, covering them. We can find the test cases in the output directory (in this case `/work/klee-out-0`). The soft link `klee-last` is also provided for convenience, pointing to the last output directory. A bunch of files gets created, including the two test cases named `test000001.ktest` and `test000002.ktest`. These are binary files, which can be examined with `ktest-tool` utility. Let's try it:

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

As we expected, the `input` value is 10, and the assertion fails.
