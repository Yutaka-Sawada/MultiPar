# MultiPar

v1.3.1.5 is public

 This version has some trial functions. 
I fixed some problems of previous versions. 
Though I tested the behavior on my PC, 
there may be a bug in other environments or in a rare case. 
Be careful to use this version for daily usage. 
When you want a stable version, 
you should not use this new one until other users try. 
Unless you have a problem, v1.3.0.7 or v1.3.1.3 might be safe. 
If you see a strange behavior, odd problem, or failure, 
please report the incidinet to me. 
I will fix as possible as I can. 

 I increased max block size from 1 GB (old versions) to 2 GB (new version). 
This change is for rare case. 
Normally users should not set so large block size in most case. 
For compatibility, setting less than 100,000,000 bytes = 95 MB is good. 
Be careful, MultiPar cannot treat too large block size properly. 
When you have a set of PAR2 files with more than 2 GB block size (such like 4 GB), 
par2cmdline may support them. 
I adjusted some GUI components for big numbers. 
If you see something bad in your language UI, please let me know. 

 I implemented a function to calculate MD5 hash of multiple files at verifying source files. 
It seems to be faster on SSD. (I cannot test the speed by myself.) 
But, it is slow on HDD. 
It detects your drive type and switches function automatically. 
If it fails and happens to be slow on your PC, 
please report your case. 
Because I don't know SSD's property so much, I will need help of users. 
Thanks John L. Galt for tests and bug report on SSD. 

 There was a bug in v1.3.1.4, and created PAR2 files happened to contain broken packets. 
MD5 hash of some packets were wrong. 
When a file size was multiple of block size, the problem occured. 
Such PAR2 files are shown as damaged on MultiPar verification, 
and v1.3.1.4 could not verify source files without checksum packets. 
I fixed the bug in v1.3.1.5, and it can verify source files now. 
Though it's possible to use such broken PAR2 files (created by v1.3.1.4), 
you would better recreate new PAR2 files with this new version. 
I'm sorry for the inconvenience. 
Thanks nutpantz for this bug report. 

 When you created many PAR2 files (more than 512), verification had failed in previous versions. 
I (and many users) didn't see this bug for long time. 
Normally people don't create so many PAR2 files. 
Thanks Martin Klefas-Stennett for finding this rare problem. 


[ Changes from 1.3.1.4 to 1.3.1.5 ]  

GUI update  
- Change  
  - Max block size is increased to 2,118,123,520 bytes. (1.97 GB)  
  - Max split size is increased to 2,147,287,040 bytes. (1.99 GB)  

- Bug fix  
  - Memory allocation failure in verifying over than 512 PAR2 files was fixed.  

PAR2 client update  
- Change  
  - Max slice size is increased to 2,147,483,644 bytes. (2 GB)  
  - Max split size is increased to 4,294,967,292 bytes. (4 GB)  
  - When source files are on SSD, verification may become faster.  

- Improvement  
  - Setup of CRC-32 may become slightly faster on recent CPU.  

- Bug fix  
  - A bug in calculating hash of source files on SSD was fixed.  
  - A bug in verifying source files without Slice Checksum packet was fixed.  


[ Hash value ]  

MultiPar1315.zip  
MD5: DB3661C2AD4D5B6404C7FC8C4CF5AE2B  
SHA1: AECCB95F757163E439B9BB6372050D2AB1538D39  

MultiPar1315_setup.exe  
MD5: ECFC7F69FB5AF168478C24F5C06F025E  
SHA1: 66EB941B3E293EB35A48FAD80DD94BC3980DA3E8  


[ Hash value of other source code packages ]  
 Old versions and source code packages are available at [OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu) now.  

MultiPar_par2j_1315.7z  
MD5: FB245B4AFDB45F6C89FFC6231ECA3868  
SHA1: FC32C6F0A88C2750C9A8408525359AED6DBE2B2C  

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

MultiPar_Help_1314.7z  
MD5: 5D274F59A5B908B1E31D62CD4F4A0D54  
SHA1: CF1114B3850CDF52535A2C03ED374D1FA5E6B30E  
