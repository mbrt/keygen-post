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

```
enum result_t {
    INVALID,
    VALID,
    VALID_IF_LAST_VERSION
};
```
This is a convenient `enum` used as a result for the validation. `INVALID` and `VALID` values are pretty self-explanatory.  `VALID_IF_LAST_VERSION` tells that this registration is valid only if the current software version is the last available. The reasons for this strange possibility will be clear shortly.

```
enum { HEADER_SIZE = 8192 };
struct {
    int header[HEADER_SIZE];
    int data[1000000];
} mail_digest_table;
```
This is a data structure, containing digests of mail addresses of
known registered users. This is a pretty big file embedded in the executable itself. During startup, a resource is extracted in a temporary file and its contents copied into this struct. Each element of the `header` vector is an offset pointing inside the `data` vector.

Here we have a pseudo-C code for the registration check, that uses data types and variables explained above:
```
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
