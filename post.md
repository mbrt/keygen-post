# Keygenning with KLEE
In the past weeks I enjoyed working on reversing a software (don't ask me the name), to study its protection scheme for the registration process. The story the user has to follow is the same for many others: download the trial, pay, get the serial number, use it in the annoying nag screen to get the fully functional version.

Since my purpose is to not damage the company developing the software, I will not mention the name of the software, nor I will publish the code of the final key generator. My goal is instead to study software protection in general, and real software is better than a crackme, because.... beh, it's more real :)

In this post we are going to take a look at the steps I followed to understand how this software decides about bad and good serial numbers, and a possible implementation of a key generator.

## First approach
First of all I needed a big picture of the software, so I opened the installation folder. I was a bit surprised to find a single x86 executable of 12 MB.

...

## Getting acquainted
Having the hands on the main data and functions that manipulate serial number, customer number and mail address, we can organize the high level workflow in IDA, by using the really useful proximity browser.
