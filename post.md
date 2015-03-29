# Keygenning with KLEE
In the past weeks I enjoyed working on reversing a software (don't ask me the name), to study its protection scheme for the registration process. The story the user has to follow is the same for many others: download the trial, pay, get the serial number, use it in the annoying nag screen to get the fully functional version.

Since my purpose is to not damage the company developing the software, I will not mention the name of the software, nor I will publish the code of the final key generator. My goal is instead to study software protection in general, and what is better than a random product picked up in the Internet? :)

In this post we are going to take a look at the steps I followed to understand how this software decides about bad and good serial numbers.

## First approach
First of all I needed a big picture of the software, so I opened the installation folder. I was a bit surprised to find a single x86 executable of 12 MB.
