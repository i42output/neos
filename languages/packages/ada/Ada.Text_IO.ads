--  Ada.Text_IO.ads
--
--  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.
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
--  package Ada.Text_IO in the Ada Reference Manual (ISO/IEC 8652:1995, A.10.1), which
--  carries the following notice:
--
--    Copyright © 1992, 1993, 1994, 1995, Intermetrics, Inc.
--
--    This copyright is assigned to the U.S. Government. All rights reserved.
--
--    This document may be copied, in whole or in part, in any form or by any
--    means, as is or with alterations, provided that (1) alterations are clearly
--    marked as alterations and (2) this copyright notice is included unmodified
--    in any copy. Compiled copies of standard library units and examples need
--    not contain this copyright notice so long as the notice is included in all
--    copies of source code and documentation.
--
--  Alterations: this specification declares only a subset of the Reference Manual's
--  declarations, and its private part is neos's
--  (see languages/packages/Ada/NOTICE).

package Ada.Text_IO is

    type File_Type is limited private; 

    type Count is range 0 .. Natural'Last; 
    subtype Positive_Count is Count range 1 .. Count'Last; 

    Unbounded : constant Count := 0; 

    procedure Put(File : in File_Type; Item : in Character); 
    procedure Put(Item : in Character); 
    procedure Put(File : in File_Type; Item : in String); 
    procedure Put(Item : in String); 
    procedure New_Line(File : in File_Type; Spacing : in Positive_Count := 1); 
    procedure New_Line(Spacing : in Positive_Count := 1);     
    procedure Put_Line(File : in File_Type; Item : in String); 
    procedure Put_Line(Item : in String); 

private

    type File_Type is access all System.File.Handle; 

    procedure DoPut(File : in File_Type; Item : in Character); 

end Ada.Text_IO;