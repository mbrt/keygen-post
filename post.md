# Keygenning with KLEE
In the past weeks I enjoyed working on reversing a software (don't ask me the name), to study its protection scheme for the registration process. The story the user has to follow is the same for many others: download the trial, pay, get the serial number, use it in the annoying nag screen to get the fully functional version.

Since my purpose is to not damage the company developing the software, I will not mention the name of the software, nor I will publish the code of the final key generator. My goal is instead to study software protection in general, and real software is better than a crackme, because.... beh, it's more real :)

In this post we are going to take a look at the steps I followed to understand how this software decides about bad and good serial numbers, and a possible implementation of a key generator.

## First approach
First of all I needed a big picture of the software, so I opened the installation folder. I was a bit surprised to find a single x86 executable of 12 MB.

...

* `license_loadRegistration` is the function that loads registration information from registry.

Immediately after the place where this funciton gets called, there is another interesting call, that takes `dcustomerNumber`, `dSerialNumber` and `lpMail` parameters. We can rename this function to `license_unk1`, waiting for a better name. It is always useful to rename things, even if your information is partial, because even if we don't know now what this function does, we at least know that it is playing with the license.

![around loadRegistration](https://github.com/michele-bertasi/keygen-post/raw/master/1_around_load_registration.png)

Here is the proxymiti view of the just discovered funcion:

![license_unk1 proximity](https://github.com/michele-bertasi/keygen-post/raw/master/2_license_unk1_proximity.png)

There are two functions called by `license_unk1`; let's see in what way they are used, by looking at the decompiled version of `license_unk1`:

```C
int __cdecl license_unk1(int dSerialNumber, int dCustomerNumber, char *lpMail)
{
  int v3; // eax@3
  int result; // eax@4
  int v5; // [sp+4h] [bp-Ch]@1
  int v6; // [sp+8h] [bp-8h]@1
  int v7; // [sp+Ch] [bp-4h]@1

  v5 = 0;
  v7 = 0;
  v6 = 0;
  if ( sub_4591F0(dSerialNumber, &v5, &v7, (int)&v6) && v5 == 4 )
  {
    v3 = sub_458FB0(dSerialNumber, lpMail);
    if ( v3 == dCustomerNumber )
      result = 3;
    else
      result = 2 - ((unsigned __int16)(dCustomerNumber ^ (unsigned __int16)v3) != 0);
  }
  else
  {
    result = 0;
  }
  return result;
}
```

Easy peasy, they are interesting by definition, because they take the serial number and the mail parameters. We can also rename them to `license_unk2` and `license_unk3`. We can go fast at this stage, because we don't need to understand everything; we have to collect data and functions that plays with the license.

By looking at the cross references of `license_unk3` we can see that it does not call other functions and it is not called by other functions than `license_unk1`, so let's skip to `license_unk2` for now:

![license_unk2 xrefs](https://github.com/michele-bertasi/keygen-post/raw/master/3_license_unk2_xrefs.png)

We can now open one function at a time, and do the following operations:
* rename the known first parameter to be `dSerialNumber`;
* rename the function to `license_unkN` if it is clearly playing with license data.

The result of this process it that we have discovered other XXXX functions and the global variable `dword_156CFCC`, that is used as `dSerialNumber` parameter for `license_unk2`. Let's rename it to `gdSerialNumber`: a global variable retaining the serial number seems very useful (at least to me :) ).

## Getting acquainted
Having the hands on the main data and functions that manipulate serial number, customer number and mail address, we can organize the high level workflow in IDA, by using the really useful proximity browser.
