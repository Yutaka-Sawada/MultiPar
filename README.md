# MultiPar

### v1.3.2.6 is public

&nbsp; I added a new feature and changed some default behavior in this version. 
I explain the difference below. 
Be careful to use them. 
If you see a strange, odd, or wrong behavior, please report with ease. 
I will fix as possible as I can.

&nbsp; I saw [a question](https://github.com/Yutaka-Sawada/MultiPar/discussions/64) about number of recovery files. 
Max number of recovery blocks was limited to the max source file size. 
At old time, this limit was made to be similar to QuickPar. 
I changed the default setting from this version. 
In old versions, recovery file size was limited by default. 
If you wanted to disable the limit, you needed to set larger limit size at `Limit Size to` option. 
In new version, recovery file size isn't limited by default. 
If you want to limit, you need to set the limit size at `Limit Size to` option. 
I added an option `RecoveryFileLimit` on `MultiPar.ini`. 
If you want to limit recovery file size by the max source file size as same as old versions, 
add a new line `RecoveryFileLimit=1` in the .INI file.

&nbsp; While reviewing my old source code, I found an obsolate point. 
I omitted one step in verification for speed ago. 
Because recent CPUs are much faster than before, 
I enabled searching duplicated blocks at simple verification. 
When you select `Simple verification` at `Verification level`, 
it may become slightly slow. 
Most users won't see any difference.

&nbsp; I implemented [a new feature](https://github.com/Yutaka-Sawada/MultiPar/issues/74). 
It tries to repair damaged files by over writing recovered blocks directly. 
It doesn't make temporary files. 
To enable the feature, select `Aligned verification` at `Verification level` option. 
You must understand good and bad points of this mode. 
Becasue it's testing period still, I may change the behavior in future.

Pros:
- Fast verification.
- Fast recovery.
- Less disk space.

Cons:
- Backup of damaged files is disabled.
- Less finding available source blocks.
- Cannot treat splited source files.
- Cannot treat additional source files.
- Cannot treat external source files.
- Risk of more data loss at failed recovery.


[ Changes from 1.3.2.5 to 1.3.2.6 ]  

Installer update  
- Inno Setup was updated from v6.2.0 to v6.2.1.  

GUI update  
- Change  
  - It won't limit size of Recovery Files by default.  
  - Aligned verification is available for test.  

All clients update  
- Change  
  - Original filename item was removed from version information.  


[ Hash value ]  

MultiPar1326.zip  
MD5: 9CD095ABF31A2A9978A2FF79EDC47C6D  
SHA1: 7FDDB03B68CFAE404F54A67559BE0DA29CEE5374  

MultiPar1326_setup.exe  
MD5: 36AB1A803538D4CC60EF2D9E30ABEC7B  
SHA1: 3E7A2ED2D2ED01F80964D865F7616EE4E3CA2D5E  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu. 


[ Hash value of other source code packages ]  
&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu).  

MultiPar_par2j_1326.7z  
MD5: 2BC1FF60C2470119D0C2F1956260BF70  
SHA1: 4554C5C01B9F8C4B14D23BE010A8866AF447B5BC  

MultiPar_par1j_1326.7z  
MD5: 72C640381C56373CA56A73157A5AA026  
SHA1: EEC159984A7A9C3E5BCCAB31090D33984331D412  

MultiPar_sfv_md5_1326.7z  
MD5: EAEA85745126E8393CFFFFC6C8A0AB8E  
SHA1: C52A6F39566E04E42F03B347A0E5E97E2CE029BA  

MultiPar_ShlExt_1326.7z  
MD5: CD1A7DA095C61DF143E1630C487FDF67  
SHA1: 10F5A2A2A081D735A504E88442587102107749CD  

MultiPar_ResUI_1326.7z  
MD5: 4E9BCF5F0078F45C93BA44C848A1CF28  
SHA1: 1284A50274DB1EBB496FFA5313C49683975BA318  
