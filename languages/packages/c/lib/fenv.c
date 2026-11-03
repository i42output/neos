/*
  lib/fenv.c

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
  The C library: <fenv.h>'s environment and exception flag functions (a part of the library in C: compiled with a program that uses it (see the 
  "provides" line below))
*/

/* provides: fegetexceptflag fesetexcept fesetexceptflag fetestexceptflag fegetenv feholdexcept fesetenv feupdateenv */

#include <fenv.h>

/* --- <fenv.h>: the environment (the rounding direction and the exceptions) */

int fegetexceptflag(fexcept_t* flagp, int excepts)
{
    *flagp = fetestexcept(excepts);
    return 0;
}

int fesetexcept(int excepts)
{
    return feraiseexcept(excepts);
}

int fesetexceptflag(const fexcept_t* flagp, int excepts)
{
    feclearexcept(excepts);
    return feraiseexcept(*flagp & excepts);
}

int fetestexceptflag(const fexcept_t* flagp, int excepts)
{
    return *flagp & excepts;
}

int fegetenv(fenv_t* envp)
{
    envp->__round = fegetround();
    envp->__except = fetestexcept(FE_ALL_EXCEPT);
    return 0;
}

int feholdexcept(fenv_t* envp)
{
    fegetenv(envp);
    feclearexcept(FE_ALL_EXCEPT);
    return 0;
}

int fesetenv(const fenv_t* envp)
{
    feclearexcept(FE_ALL_EXCEPT);
    if (envp == FE_DFL_ENV)
        return fesetround(FE_TONEAREST);
    fesetround(envp->__round);
    return feraiseexcept(envp->__except);
}

int feupdateenv(const fenv_t* envp)
{
    int raised = fetestexcept(FE_ALL_EXCEPT);
    fesetenv(envp);
    return feraiseexcept(raised);
}
