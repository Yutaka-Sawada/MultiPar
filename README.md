# MultiPar

v1.3.1.6 is public

 Because my old PC was broken, 
I upgraded my PC and development environment (to Visual Studio 2019). 
From this version, my products' supporting OS is Windows Vista or later. 
As I announced ago, v1.3.0.7 becomes the last stable version, which supports Windows XP. 

 This version has two bug fix. 
At first, I fixed a crash bug, which I happened to put in previous version. 
Though I tried to reduce using memory at seaching slices, the method was bad. 
I returned to old simple way, which allocates "block size * 3 times" size buffer. 
Sometimes I made an unknown bug to solve another problem. 
If someone sees odd behavior or strange result, please report with ease. 
Thanks to an anonymous helper. 

 Another bug was a failure of character encoding, which I found on Windows 10. 
I never saw such problem on Windows 7, or I was just lucky. 
It affected only users of multi-bytes characters. 
While it showed miss-decoded characters in old versions, 
there was no problem in verification itself. 
A user might see some odd characters on a file-list. 
I fixed the bug, and some other slight problems on Windows 10. 

 Because I changed whole development environment largely, 
there may be a compatibility issue. 
So, this version would be a test of compatibility. 
When you see a problem, please let me know. 
I will fix as possible as I can. 
I plan to refine my code at next version. 


[ Changes from 1.3.1.5 to 1.3.1.6 ]  

Installer update  
- Inno Setup was updated from v5.6.1 to v6.1.2.  

GUI update  
- Change  
  - A list-view control has Windows Explorer like Visual Style.  
  - On a folder selecting dialog, an initial selected folder is always visible.  

- Bug fix  
  - A rare failure of showing a multi-bytes character on file-list was fixed.  

PAR2 client update  
- Bug fix  
  - An access violation error while verifying splited files was fixed.  


[ Hash value ]  

MultiPar1316.zip  
MD5: B4C60214650BE024CE614474FBB62398  
SHA1: 26EA790C00B23F81E5D40FADEEC06C26391F7774  

MultiPar1316_setup.exe  
MD5: 4274B4D2D112653E2879E87D343F551E  
SHA1: 0068CB9EC0AA24842234484755BD8FBEA46E3149  


[ Hash value of other source code packages ]  
 Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu).  

MultiPar_par2j_1316.7z  
MD5: AA071C65BC7AEE7F866496F00D9D34DA  
SHA1: 76B3C63547AE1447D2981D07C00DC3BECADEDDBE  

MultiPar_par2j_extra_1294.7z  
MD5: 6D165CDA2645924ACAFE902F02FAD309  
SHA1: D77D4EA778423D5D8F820B8EAF97F733950F9FB1  

MultiPar_par1j_1314.7z  
MD5: E082D8A598A262E64CBAE2C42283488A  
SHA1: F706A3C1FCCAFCE225677BA0785CDB39870206A1  

MultiPar_sfv_md5_1314.7z  
MD5: 355B0CC6B9613B422126EF9EDAC15F87  
SHA1: EFD2CF25C47851B86EB12FD5B709BFEEC73AC36D  

MultiPar_ShlExt_1298.7z  
MD5: BE0F04DF1A6B936F23F6F01930562248  
SHA1: 52818266B45ECE135EECFF12D8DA2640A6AD5075  

MultiPar_ResUI_1315.7z  
MD5: B8B6A9DA4BD9D418CFA90FD01CCC615A  
SHA1: E5B4B16DBCAECACA2095A64006C117E04D3C9E74  

MultiPar_Help_1316.7z  
MD5: E2C0E073200754590D9EDAE332CD0462  
SHA1: A5D8CE73454073B553A7C9F703E5A6CDEDFF7CBD  
