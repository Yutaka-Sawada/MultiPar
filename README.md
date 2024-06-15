# MultiPar

### v1.3.3.3 is public

&nbsp; I fixed a few rare bugs in this version. 
While most users were not affected by those problems, 
those who saw the matter would better use new version. 
If there is a problem still, I will fix as possible as I can. 
I updated some help documents about Batch script. 
I mentioned the location of help files in ReadMe text.

&nbsp; New version supports a PC with max 8 OpenCL devices. 
Thanks [Yi Gu for reporting bug in a rare environment](https://github.com/Yutaka-Sawada/MultiPar/issues/110). 
I didn't think a user put so many OpenCL devices on a PC. 
It will detect a Graphics board correctly.

&nbsp; I improved source file splitting feature at creating PAR2 files. 
Thanks [AreteOne for reporting bug and suggestion of improvment](https://github.com/Yutaka-Sawada/MultiPar/issues/117). 
When file extension is a number, it didn't handle properly. 
If someone saw strange behavior at file splitting ago, it should have been solved in this version.

&nbsp; I fixed a bug in verifying external files. 
It might not find the last slice in a source file, when the file data is redundant. 
Thanks [dle-fr for reporting bug and testing many times](https://github.com/Yutaka-Sawada/MultiPar/issues/130). 
This solution may improve verification of damaged files, too. 
When source files are mostly random data like commpressed archive, there was no problem.


[ Changes from 1.3.3.2 to 1.3.3.3 ]  

Installer update
- Inno Setup was updated from v6.2.2 to v6.3.1.

PAR2 client update
- Bug fix
  - Fixed a bug in GPU acceleration, when there are many OpenCL devices.
  - Failure of splitting source files with numerical extension was fixed.
  - Faulty prediction of the last block in a file with repeated data was fixed.


[ Hash value ]  

MultiPar1333.zip  
MD5: 01A201CA340C33053E6D7D2604D54019  
SHA1: F7C30A7BDEB4152820C9CFF8D0E3DA719F69D7C6  

MultiPar1333_setup.exe  
MD5: 33F9E441F5C1B2C00040E9BAFA7CC1A9  
SHA1: 6CEBED8CECC9AAC5E8070CD5E8D1EDF7BBBC523A  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must select "Install for all users" at the first dialog.

&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/f/c/8eb5bd32c534a1d1/QtGhNMUyvbUggI5pAAAAAAAAKjWf9HxrAn-GDQ).
