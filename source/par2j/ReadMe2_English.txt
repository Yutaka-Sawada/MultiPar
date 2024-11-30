
Restore damaged or lost files with PAR 2.0 recovery files.

Parchive 2.0 client by Yutaka Sawada

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

[ Note ]

 This client support "Comments" in PAR files.
 This supports Multi-Byte Character like Japanese.
 This supports empty file (0 length file).
 This supports sub-directory and empty folder (directory tree).

 This verifies source files more speedy or closely by option.
 This re-calculate inverting matrix when it was failded at certain parity block.
 This search same source block as lost block in all Recovery-Set files and copy.
 This does not use parity block when block size is 4-byte.
 This trys to search available source blocks by their original order.
 Thus this may recover with less parity block than other PAR 2.0 clients.

 You may set many source files by file-list instead of command line.
 Source files may locate different directory with Recovery files.

 Max block size is 2 GB.
 You cannot specify Non-Recovery-Set files. (Empty files are set.)
 Number of source blocks is limited up to 32768. (Files are limited up to 65536.)
 Number of creating recovery blocks is limited up to 65535.

 This client consumes memory very much.
 Verifying file use same memory as the file or double of block size.
 Recovering block use more memory as the number of block is many.

 This requires a PC of Windows 7 or later (Windows 8, 10, 11).
 This is developed with Visual Studio 2022 on Windows 10.



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

[ Thanks for help ]

par2-specifications.pdf
Parity Volume Set Specification 2.0

	Michael Nahas
	Peter Clements
	Paul Nettle
	Ryan Gallagher
	May 11th, 2003



par2cmdline-0.4-x86-win32.zip : README
par2cmdline is a PAR 2.0 compatible file verification and repair tool.



par2cmdline-0.4-x86-win32.zip : reedsolomon.cpp, crc.cpp, crc.h

//  This file is part of par2cmdline (a PAR 2.0 compatible file verification and
//  repair tool). See http://parchive.sourceforge.net for details of PAR 2.0.
//
//  Copyright (c) 2003 Peter Brian Clements



phpar2_12src.zip : par2asm.cpp

// Paul Houle (paulhoule.com) March 22, 2008



par2cmdline-0.4-tbb-20081005.tar

//  Modifications for concurrent processing, Unicode support, and hierarchial
//  directory support are Copyright (c) 2007-2008 Vincent Tan.



par-v1.1.tar.gz : rs.doc
Dummies guide to Reed-Solomon coding.



Jerasure - A C/C++ Library for a Variety of Reed-Solomon and RAID-6 Erasure Coding Techniques
Revision 1.2A
May 24, 2011

James S. Plank
Department of Electrical Engineering and Computer Science
University of Tennessee



GF-Complete: A Comprehensive Open Source Library for Galois Field Arithmetic
Revision 1.03.   January 1, 2015.

Copyright (c) 2013, James S. Plank, Ethan L. Miller, Kevin M. Greenan,
Benjamin A. Arnold, John A. Burnum, Adam W. Disney, Allen C. McBride
All rights reserved.



MD5
; MD5 hash generator -- Paul Houle (paulhoule.com) 4/16/2010
;
; This code is in the public domain.  Please attribute the author.



crc_folding.c 
 * Compute the CRC32 using a parallelized folding approach with the PCLMULQDQ 
 * instruction.
 * Copyright (C) 2013 Intel Corporation. All rights reserved.



https://github.com/animetosho/ParPar
ParPar is a high performance, multi-threaded PAR2 creation tool and library for node.js.
License
This module is Public Domain.



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

[ License ]

This PAR2 client is written by Yutaka Sawada.
License is under GPL (GNU GENERAL PUBLIC LICENSE Version 2).

My name is Yutaka Sawada.
Mail address is "tenfon (at mark) outlook.jp".
The (at mark) is format to avoid junk mails, and replace it with "@".
