# MultiPar

v1.3.1.4 is public

 This version has many changes. 
Though I tested the behavior on my PC, 
there may be a bug in other environments or in a rare case. 
Be careful to use this version for daily usage. 
When you want a stable version, 
you should not use this new one until other users try. 
If you see a strange behavior, odd problem, or failure, 
please report the incidinet to me. 
I will fix as possible as I can. 

 I improved MD5 hash calculation. 
It becomes faster on my PC. 
But, it may be slower on some CPUs. 
If you feel that hash calculation becomes slow, 
please report your case. 

 I implemented a function to calculate MD5 hash of multiple files at creating PAR2 files. 
It seems to be faster on SSD. (I cannot test the speed by myself.) 
But, it is slow on HDD. 
It detects your drive type and switches function automatically. 
If it fails and happens to be slow on your PC, 
please report your case. 
Because I don't know SSD's property so much, I will need help of users. 

 I tried to fix a location bug of MultiPar window on multiple monitors. 
But, I cannot test the behavior on my PC. 
There is a blur window problem on different DPI setting between multiple monitors. 
At this time, I cannot solve this yet. 
When someone has this trouble and want to help me, I will try to solve. 


[ Changes from 1.3.1.3 to 1.3.1.4 ]  

GUI update  
- Change  
  - An option "Don't search subfolders" is added for verification and reapir.  

- Bug fix  
  - It's possible to adjust opening window position on multiple monitors.  

PAR1 client update  
- Improvement  
  - MD5 hash calculation becomes slightly faster.  

PAR2 client update  
- New  
  - It's possible to set file access mode for debug usage.  

- Change  
  - When source files are on SSD, hash calculation may become faster.  

- Improvement  
  - MD5 hash calculation becomes faster on recent CPU.  

- Bug fix  
  - An access violation bug in restoring single source file was fixed.  

SFV/MD5 client update  
- Improvement  
  - MD5 hash calculation becomes slightly faster.  


[ Hash value ]  

MultiPar1314.zip  
MD5 : 44C31A7F81C6D0C24339D311FD37C10D  
SHA-1 : 8871BE03522CF4C6ED96F460E65A4578A0C1D2E3  

MultiPar1314_setup.exe  
MD5: E70D7A2222F99CB903B2A16365D9887E  
SHA1: 47A9BDAA489F99067516B160984A9A02352E56BF  


[ Hash value of other source code packages ]  
 Old versions and source code packages are available at [OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu) now.  

MultiPar_par2j_1314.7z  
MD5: B505C54360170EE14BDC418A62A422C7  
SHA1: B2CAB6C0074E48A7E06973D841F3C88251410ACD  

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

MultiPar_ResUI_1314.7z  
MD5: 2C6BB0F25A9E95E5C38BA856FA9DC4A6  
SHA1: F4ED32963CD476F5DF722BE6536E116F0FE55A23  

MultiPar_Help_1314.7z  
MD5: 5D274F59A5B908B1E31D62CD4F4A0D54  
SHA1: CF1114B3850CDF52535A2C03ED374D1FA5E6B30E  
