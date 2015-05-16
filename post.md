# Keygenning with KLEE
In the past weeks I enjoyed working on reversing a software (don't ask me the name), to study how serial numbers are validated. The story the user has to follow is the same for many others: download the trial, pay, get the serial number, use it in the annoying nag screen to get the fully functional version.

Since my purpose is to not damage the company developing the software, I will not mention the name of the software, nor I will publish the code of the final key generator. My goal is instead to study a real case of serial number validation, and to highlight its weaknesses.

In this post we are going to take a look at the steps I followed to reverse the serial validation process and to make a key generator using [KLEE](http://klee.github.io/) symbolic virtual machine. We are not going to follow all the details on the reversing part, since you cannot reproduce them on your own. We will concentrate our thoughts on the key-generator itself: that is the most interesting part.

## Getting acquainted
The software is an `x86` executable, with no anti-debugging, nor anti-reversing techniques. When started it presents a nag screen asking for a registration composed by: customer number, serial number and a mail address. This is a fairly common software.

## Tools of the trade
First steps in the reversing are devoted to find all the interesting functions to analyze. To do this I used [IDA Pro](https://www.hex-rays.com/products/ida/) with Hex-Rays decompiler, and the [WinDbg](https://msdn.microsoft.com/en-us/library/windows/hardware/ff551063(v=vs.85).aspx) debugger.

Let me skip the actual operations I did, since they are not very interesting. You can find many other articles on the web that can guide you trough basic reversing techniques. I only kept in mind some simple rules, while going forward:
* always rename functions playing with interesting data, even if you don't know precisely what they do. A name like `license_validation_unknown_8` is always better than a default like `sub_46fa39`;
* similarly, rename data whenever you find it interesting;
* change data types when you are sure they are wrong: use structs and arrays in case of aggregates;
* follow cross references of data and functions to expand your collection;
* validate your beliefs with the debugger if possible. For example, if you think a variable contains the serial, break with the debugger and see if it is the case.

## Big picture
When I collected the most interesting functions, I tried to understand the high level flow and the simpler functions.
