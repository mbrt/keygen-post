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

The result of this process it that we have discovered other 7 functions and the global variable `dword_156CFCC`, that is used as `dSerialNumber` parameter for `license_unk2`. Let's rename it to `gdSerialNumber`: a global variable retaining the serial number seems very useful :).

We can continue our high level analysis by looking at cross references to the `gdSerialNumber`, shown in the next figure:

![gdSerialNumber xrefs](https://github.com/michele-bertasi/keygen-post/raw/master/5_gdSerialNumber_xrefs.png)

As you can see some functions are already marked as `license_unkXX` and some other are new.

To discover more functions involved we can use even another trick. As compilers tend to group function addresses by translation unit (i.e. the functions of one ".cpp" file have similar addresses), we can look for other licensing functions assuming they are compiled in few translation units (and not scattered around the executable).

To do this, we can examine the **function window**, ordering functions by start address. You can see that some of our functions are really close together. We can also look at the functions between them, seeking for more information.

![lic funcs address proximity](https://github.com/michele-bertasi/keygen-post/raw/master/6_lic_funcs_code_proximity.png)

This iterative process goes on and on, until all interesting functions has been put in our bag, and all the variables manipulating license data (global or local) has been renamed.

## Getting acquainted
Having the hands on main data and functions manipulating serial number, customer number and mail address, we can organize the high level workflow in IDA, by using the really useful proximity browser.

Here you can see all the discovered functions within their call graph. The purpose of this view is to highlight the most important functions and their interactions. For example we can see that `license_unk2` and `license_unk13`, have an higher number of incoming edges.

![lic funcs proximity](https://github.com/michele-bertasi/keygen-post/raw/master/7_license_proximity.png)

Let's try to understand `license_unk2`, by looking at its decompiled version:

```C
signed int __cdecl license_unk2(int dSerialNumber, int *a2, int *a3, int *a4)
{
  signed int result; // eax@2
  char v5; // cl@3
  int v6; // edi@3
  int v7; // ebp@3
  int v8; // esi@3
  int v9; // edi@3
  int v10; // eax@20
  int v11; // edx@34
  int v12; // ecx@34

  if ( dSerialNumber == dword_1BB4294 )
  {
    result = dword_1BB42A4;
    *a2 = dword_1BB42A4 != 0 ? dword_1BB4298 : 0;
    *a3 = result != 0 ? dword_1BB429C : 0;
    *a4 = result != 0 ? dword_1BB42A0 : 0;
    return result;
  }
  *a2 = 0;
  *a3 = 0;
  v5 = dSerialNumber ^ ((dSerialNumber >> 16) - BYTE1(dSerialNumber));
  *a4 = 0;
  v6 = (unsigned __int8)(dSerialNumber ^ (unsigned __int8)(BYTE1(dSerialNumber) - v5));
  v7 = v6 ^ (unsigned __int8)(dSerialNumber - v5);
  v8 = (v6 | ((unsigned __int8)(dSerialNumber ^ (unsigned __int8)((dSerialNumber >> 16) - BYTE1(dSerialNumber))) << 8)) << 16;
  dword_1BB4294 = dSerialNumber;
  dword_1BB42A4 = 0;
  v9 = BYTE3(dSerialNumber);
  if ( BYTE3(dSerialNumber) )
    v9 = BYTE3(dSerialNumber) ^ 0x55;
  if ( v8 > (unsigned int)_time64(0) + 172800
    || v9 && v9 != 80 && v9 != 81 && v9 != 82 && v9 != 83 && v9 != 84 && v9 != 86 )
    return 0;
  if ( v7 == 237 )
  {
/*
... the function continue with other computations ...
I've skipped them for brevity
*/
  }
  return 0;
}
```

The first block of code checks if the given serial number is equal to the one saved in the global variable `dword_1BB4294 `. If so, it sets its arguments (that are output arguments) to the value of other global variables. This is basically caching the result of the function, in case the same argument is given multiple times, so it seems a performance optimization. After the first block, computations on the serial number and checks are performed and an integer result is returned. This is clearly a validation of the serial number alone, since it is the only parameter given to the function.

OK, instead of struggling inside this function, let's check some of its usages, to understand the meaning of the result and the output arguments.
