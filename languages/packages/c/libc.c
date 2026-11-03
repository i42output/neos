/*
  libc.c

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
  The C library's own C code: the prelude (compiled with every C program): the functions that end the program. The rest of the
  library in C is in lib/, a file compiled with a program only if the program uses a function that it provides (its "provides"
  line); the rest of the library is native (the C runtime plugin's).
*/

int __neos_exit_prepare(int status);
int __neos_quick_exit_prepare(int status);
int __neos_exit_flush(int status);
void __neos_abort(void);

/* --- <stdlib.h>: program termination */

[[noreturn]] void exit(int status)
{
    __builtin_neos_exit(__neos_exit_prepare(status));
}

[[noreturn]] void quick_exit(int status)
{
    __builtin_neos_exit(__neos_quick_exit_prepare(status));
}

[[noreturn]] void _Exit(int status)
{
    __builtin_neos_exit(__neos_exit_flush(status));
}

[[noreturn]] void abort(void)
{
    __neos_abort();
    __builtin_neos_exit(__neos_exit_flush(134));
}
