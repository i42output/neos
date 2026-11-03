--  Ada.Numerics.Generic_Complex_Arrays.ads
--
--  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.
--
--  Redistribution and use in source and binary forms, with or without
--  modification, are permitted provided that the following conditions are
--  met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--
--     * Redistributions in binary form must reproduce the above copyright
--       notice, this list of conditions and the following disclaimer in the
--       documentation and/or other materials provided with the distribution.
--
--     * Neither the name of the copyright holder nor the names of its
--       contributors may be used to endorse or promote products derived from
--       this software without specific prior written permission.
--
--  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
--  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
--  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
--  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
--  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
--  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
--  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
--  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
--  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
--  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
--  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--
--  This package specification is an altered copy of the specification of
--  package Ada.Numerics.Generic_Complex_Arrays in the Ada Reference Manual, 202x Edition, Draft 25 (G.3.2),
--  the draft of ISO/IEC 8652:2023, which carries the following notices:
--
--    Ada Reference Manual - Language and Standard Libraries
--
--    Copyright © 1992, 1993, 1994, 1995, Intermetrics, Inc.
--
--    This copyright is assigned to the U.S. Government. All rights reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is or with alterations, provided that (1) alterations are clearly marked as
--    alterations and (2) this copyright notice is included unmodified in any copy.
--    Compiled copies of standard library units and examples need not contain this
--    copyright notice so long as the notice is included in all copies of source
--    code and documentation.
--
--
--
--    Technical Corrigendum 1
--
--    Copyright © 2000, The MITRE Corporation. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of MITRE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    MITRE, its Board of Trustees, officers, agents, and employees, from any and
--    all liability or damages to yourself or your hardware or software, or third
--    parties, including attorneys' fees, court costs, and other related costs and
--    expenses, arising out of your use of this document irrespective of the cause
--    of said liability.
--
--    MITRE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL MITRE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF MITRE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--
--
--    Amendment 1
--
--    Copyright © 2004, 2005, 2006, 2007, AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--    Third Edition
--
--    Copyright © 2008, 2009, 2010, 2011, 2012 AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--    Technical Corrigendum 1 for Ada 2012
--
--    Copyright © 2013, 2014, 2015, 2016 AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--    Ada 202x version
--
--    Copyright © 2016, 2017, 2018, 2019, 2020 AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--
--
--    Ada 2005 Consolidated Standard
--
--    Copyright © 2004, 2005, 2006, Ada-Europe.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of Ada-Europe.
--
--    You use this document on the condition that you indemnify and hold harmless
--    Ada-Europe and its Board from any and all liability or damages to yourself or
--    your hardware or software, or third parties, including attorneys' fees, court
--    costs, and other related costs and expenses, arising out of your use of this
--    document irrespective of the cause of said liability.
--
--    ADA-EUROPE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO
--    WARRANTY, EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL ADA-EUROPE
--    BE LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF ADA-EUROPE HAS BEEN ADVISED OF THE POSSIBILITY OF
--    SUCH DAMAGES.
--
--
--  Alterations (neos):
--  None: the declaration is the Reference Manual's.
--  (See languages/packages/ada/NOTICE.)

generic
   with package Real_Arrays   is new
      Ada.Numerics.Generic_Real_Arrays   (<>);
   use Real_Arrays;
   with package Complex_Types is new
      Ada.Numerics.Generic_Complex_Types (Real);
   use Complex_Types;
package Ada.Numerics.Generic_Complex_Arrays
   with Pure, Nonblocking is

   -- Types
   type Complex_Vector is array (Integer range <>) of Complex;
   type Complex_Matrix is array (Integer range <>,
                                 Integer range <>) of Complex;

   -- Subprograms for Complex_Vector types
   -- Complex_Vector selection, conversion and composition operations
   function Re (X : Complex_Vector) return Real_Vector;
   function Im (X : Complex_Vector) return Real_Vector;
   procedure Set_Re (X  : in out Complex_Vector;
                     Re : in     Real_Vector);
   procedure Set_Im (X  : in out Complex_Vector;
                     Im : in     Real_Vector);
   function Compose_From_Cartesian (Re     : Real_Vector)
      return Complex_Vector;
   function Compose_From_Cartesian (Re, Im : Real_Vector)
      return Complex_Vector;
   function Modulus  (X     : Complex_Vector) return Real_Vector;
   function "abs"    (Right : Complex_Vector) return Real_Vector
                                                 renames Modulus;
   function Argument (X     : Complex_Vector) return Real_Vector;
   function Argument (X     : Complex_Vector;
                      Cycle : Real'Base)      return Real_Vector;
   function Compose_From_Polar (Modulus, Argument : Real_Vector)
      return Complex_Vector;
   function Compose_From_Polar (Modulus, Argument : Real_Vector;
                                Cycle             : Real'Base)
      return Complex_Vector;

   -- Complex_Vector arithmetic operations
   function "+"       (Right  : Complex_Vector) return Complex_Vector;
   function "-"       (Right  : Complex_Vector) return Complex_Vector;
   function Conjugate (X      : Complex_Vector) return Complex_Vector;
   function "+"  (Left, Right : Complex_Vector) return Complex_Vector;
   function "-"  (Left, Right : Complex_Vector) return Complex_Vector;
   function "*"  (Left, Right : Complex_Vector) return Complex;
   function "abs"     (Right : Complex_Vector) return Real'Base;

   -- Mixed Real_Vector and Complex_Vector arithmetic operations
   function "+" (Left  : Real_Vector;
                 Right : Complex_Vector) return Complex_Vector;
   function "+" (Left  : Complex_Vector;
                 Right : Real_Vector)    return Complex_Vector;
   function "-" (Left  : Real_Vector;
                 Right : Complex_Vector) return Complex_Vector;
   function "-" (Left  : Complex_Vector;
                 Right : Real_Vector)    return Complex_Vector;
   function "*" (Left  : Real_Vector;    Right : Complex_Vector)
      return Complex;
   function "*" (Left  : Complex_Vector; Right : Real_Vector)
      return Complex;

   -- Complex_Vector scaling operations
   function "*" (Left  : Complex;
                 Right : Complex_Vector) return Complex_Vector;
   function "*" (Left  : Complex_Vector;
                 Right : Complex)        return Complex_Vector;
   function "/" (Left  : Complex_Vector;
                 Right : Complex)        return Complex_Vector;
   function "*" (Left  : Real'Base;
                 Right : Complex_Vector) return Complex_Vector;
   function "*" (Left  : Complex_Vector;
                 Right : Real'Base)      return Complex_Vector;
   function "/" (Left  : Complex_Vector;
                 Right : Real'Base)      return Complex_Vector;

   -- Other Complex_Vector operations
   function Unit_Vector (Index : Integer;
                         Order : Positive;
                         First : Integer := 1) return Complex_Vector;

   -- Subprograms for Complex_Matrix types
   -- Complex_Matrix selection, conversion and composition operations
   function Re (X : Complex_Matrix) return Real_Matrix;
   function Im (X : Complex_Matrix) return Real_Matrix;
   procedure Set_Re (X  : in out Complex_Matrix;
                     Re : in     Real_Matrix);
   procedure Set_Im (X  : in out Complex_Matrix;
                     Im : in     Real_Matrix);
   function Compose_From_Cartesian (Re     : Real_Matrix)
      return Complex_Matrix;
   function Compose_From_Cartesian (Re, Im : Real_Matrix)
      return Complex_Matrix;
   function Modulus  (X     : Complex_Matrix) return Real_Matrix;
   function "abs"    (Right : Complex_Matrix) return Real_Matrix
                                                 renames Modulus;
   function Argument (X     : Complex_Matrix) return Real_Matrix;
   function Argument (X     : Complex_Matrix;
                      Cycle : Real'Base)      return Real_Matrix;
   function Compose_From_Polar (Modulus, Argument : Real_Matrix)
      return Complex_Matrix;
   function Compose_From_Polar (Modulus, Argument : Real_Matrix;
                                Cycle             : Real'Base)
      return Complex_Matrix;

   -- Complex_Matrix arithmetic operations
   function "+"       (Right : Complex_Matrix) return Complex_Matrix;
   function "-"       (Right : Complex_Matrix) return Complex_Matrix;
   function Conjugate (X     : Complex_Matrix) return Complex_Matrix;
   function Transpose (X     : Complex_Matrix) return Complex_Matrix;
   function "+" (Left, Right : Complex_Matrix) return Complex_Matrix;
   function "-" (Left, Right : Complex_Matrix) return Complex_Matrix;
   function "*" (Left, Right : Complex_Matrix) return Complex_Matrix;
   function "*" (Left, Right : Complex_Vector) return Complex_Matrix;
   function "*" (Left  : Complex_Vector;
                 Right : Complex_Matrix) return Complex_Vector;
   function "*" (Left  : Complex_Matrix;
                 Right : Complex_Vector) return Complex_Vector;

   -- Mixed Real_Matrix and Complex_Matrix arithmetic operations
   function "+" (Left  : Real_Matrix;
                 Right : Complex_Matrix) return Complex_Matrix;
   function "+" (Left  : Complex_Matrix;
                 Right : Real_Matrix)    return Complex_Matrix;
   function "-" (Left  : Real_Matrix;
                 Right : Complex_Matrix) return Complex_Matrix;
   function "-" (Left  : Complex_Matrix;
                 Right : Real_Matrix)    return Complex_Matrix;
   function "*" (Left  : Real_Matrix;
                 Right : Complex_Matrix) return Complex_Matrix;
   function "*" (Left  : Complex_Matrix;
                 Right : Real_Matrix)    return Complex_Matrix;
   function "*" (Left  : Real_Vector;
                 Right : Complex_Vector) return Complex_Matrix;
   function "*" (Left  : Complex_Vector;
                 Right : Real_Vector)    return Complex_Matrix;
   function "*" (Left  : Real_Vector;
                 Right : Complex_Matrix) return Complex_Vector;
   function "*" (Left  : Complex_Vector;
                 Right : Real_Matrix)    return Complex_Vector;
   function "*" (Left  : Real_Matrix;
                 Right : Complex_Vector) return Complex_Vector;
   function "*" (Left  : Complex_Matrix;
                 Right : Real_Vector)    return Complex_Vector;

   -- Complex_Matrix scaling operations
   function "*" (Left  : Complex;
                 Right : Complex_Matrix) return Complex_Matrix;
   function "*" (Left  : Complex_Matrix;
                 Right : Complex)        return Complex_Matrix;
   function "/" (Left  : Complex_Matrix;
                 Right : Complex)        return Complex_Matrix;
   function "*" (Left  : Real'Base;
                 Right : Complex_Matrix) return Complex_Matrix;
   function "*" (Left  : Complex_Matrix;
                 Right : Real'Base)      return Complex_Matrix;
   function "/" (Left  : Complex_Matrix;
                 Right : Real'Base)      return Complex_Matrix;

   -- Complex_Matrix inversion and related operations
   function Solve (A : Complex_Matrix; X : Complex_Vector)
      return Complex_Vector;
   function Solve (A, X : Complex_Matrix) return Complex_Matrix;
   function Inverse (A : Complex_Matrix) return Complex_Matrix;
   function Determinant (A : Complex_Matrix) return Complex;

   -- Eigenvalues and vectors of a Hermitian matrix
   function Eigenvalues(A : Complex_Matrix) return Real_Vector;
   procedure Eigensystem(A       : in  Complex_Matrix;
                         Values  : out Real_Vector;
                         Vectors : out Complex_Matrix);

   -- Other Complex_Matrix operations
   function Unit_Matrix (Order            : Positive;
                         First_1, First_2 : Integer := 1)
                                            return Complex_Matrix;

end Ada.Numerics.Generic_Complex_Arrays;
