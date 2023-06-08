# MultiPar

### v1.3.2.8 is public
&nbsp; This is a minor update version to fix some bugs in rare cases. 
Most users don't see difference. 
If there is no serious problem in this version, 
next version will be the last of v1.3.2 tree.

&nbsp; I fixed a [problem of MultiPar shell extension](https://github.com/Yutaka-Sawada/MultiPar/issues/86), 
when UAC (User Account Control on Windows Vista or later) is disabled. 
Thanks whulkhulk and Slava46 for test and confirm. 
If other users could not "Integrate MultiPar into Shell" ago, he may try this new version.

&nbsp; I added a confirm dialog at closing MultiPar, when it's creating or repairing. 
This change may reduce accidental loss of working data. 
Though MultiPar inherits most usage and behavior of QuickPar, I would improve a bit.

&nbsp; I fixed [small bugs in my OpenCL code for GPU](https://github.com/Yutaka-Sawada/MultiPar/issues/88). 
Though I'm not sure the incident, it might not work rarely. 
Because I don't use a graphics board on my PC, I didn't test myself. 
Thanks apprehensivemom for test. 
Even when you checked "Enable GPU acceleration", it may not use GPU for small data. 
It's because starting GPU is slow. 
If calculation finishes in a few seconds, using CPU only may be faster. 
GPU may require at least a few minutes task to see speed difference. 
As a note, I write its threshold below.

Threshold to use GPU:
- Data size must be larger than 512 MB.
- Block size must be larger than 64 KB.
- Number of source blocks must be more than 256.
- Number of recovery blocks must be more than 32.


[ Changes from 1.3.2.7 to 1.3.2.8 ]  

GUI update
- Change
  - It won't erase Zone.Identifier flag of MultiPar.exe automatically.
  - It shows confirm dialog before close, when it's creating or repairing.

- Improvement
  - It will show error, when calling PAR client doesn't exist.

- Bug fix
  - When UAC is disabled, Shell Extesnion DLL uses HKEY_LOCAL_MACHINE.

PAR2 client update
- Bug fix
  - It will show correct efficiency for over than TB size files.
  - GPU function works with MMX, when all SSE2, SSSE3, AVX2 are disabled.

All clients update
- Change
  - It will search hidden files, when Windows Explorer shows them.


[ Hash value ]  

MultiPar1328.zip  
MD5: C7BD23C0D32C47555E344D9D88C149C2  
SHA1: 467F85E53011B3BC1E67E6685B1787D32B6F2296  

MultiPar1328_setup.exe  
MD5: 4D7A3BA6B88D9F37A22C35C425DA5F4D  
SHA1: 6BCCF834BC6038F1AC30F82B193A2B5F45FD7697  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu.

&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0).
