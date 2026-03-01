# MultiPar

### v1.3.3.6 is public

&nbsp; This is a minor update version. 
If there is no serious problem or large change, 
next version will be the last of v1.3.3 tree.

&nbsp; Because recent Nvidia GPU doesn't support 32-bit application, 
I changed MultiPar's behavior not to check GPU property. 
Thanks [betagitman for reporting this issue](https://github.com/Yutaka-Sawada/MultiPar/issues/155). 
On 64-bit Windows OS, users need to use par2j64.exe instead of par2j.exe for GPU acceleration.

&nbsp; MultiPar supports FLAC fingerprint (ffp). 
as [it was requested](https://github.com/Yutaka-Sawada/MultiPar/discussions/157). 
If you want to use FLAC Fingerprint on MultiPar, you need to put `flac.exe` and `libFLAC.dll` 
in MultiPar folder or somewhere PATH variable environment (such like Windows/System32). 
You can download them from [FLAC official download page](https://xiph.org/flac/download.html).

&nbsp; MultiPar shows misnamed or moved files at verification with .SFV and .MD5 checksum. 
To search misnamed files, you need to change "Verification level" option to "Additional verification" 
at "Verification and Repair options" on MultiPar Option window. 
If recursive search is slow, you may check "Don't search subfolders" item.

&nbsp; My web-pages on `vector.co.jp` disappered at 2024 December 20. 
Thanks Vector to support MultiPar for long time. 
I use [this GitHub page](https://github.com/Yutaka-Sawada/MultiPar) for MultiPar announcement.


[ Changes from 1.3.3.5 to 1.3.3.6 ]  

Installer update
- Inno Setup was updated from v6.5.4 to v6.7.1.

GUI update
- Change
  - GPU option is enabled always, even when GPU device doesn't exist.

SFV/MD5 client update
- New
  - FLAC Fingerprint files are supported (by flac.exe and libFLAC.dll).
  - It will search misnamed files in base directory by "vl2" option.


[ Hash value ]  

&nbsp; MultiPar1336.zip  
MD5: D84778B6BE68AD2A488FB6492C3F8992  
SHA1: 9CFE9EA7AB3559E6AE2C98A443F4EF1721D70614  

&nbsp; MultiPar1336_setup.exe  
MD5: 9A8D238DD657B9F93817BCCB90DC3C92  
SHA1: 82F22F5815F185FA50B2A6BD69967B0C6BFC2A92  

&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must select "Install for all users" at the first dialog.

&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/f/c/8eb5bd32c534a1d1/QtGhNMUyvbUggI5pAAAAAAAAKjWf9HxrAn-GDQ).

