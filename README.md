# MultiPar

v1.3.1.7 is public

 This is minor update version. 
I changed some for new environments, Windows 10 and Visual Studio 2019. 
If there is no serious problem, next version will be the last of v1.3.1 tree. 
When you see a bug, odd incident, or strange behavior, please let me know. 
I will fix before releasing a stable version. 

 Because Visual Studio 2019 supports new CPUs, I removed some obsolate code. 
It doesn't require par2j_extra.dll or par2j64_extra.dll to use AVX2 feature. 
It may not require extra memory barrier (_mm_sfence) for multi-threading. 
But, I'm not sure about the memory barrier. 
Visual Studio 2008 required them to support new age multi-core CPUs. 
I think that Visual Studio 2019 should treat those recent CPUs well. 
Though I tested with Intel i5 CPU on my PC, I don't know other CPUs. 
If you see "chacksum mismatch" error, please report the incident. 


[ Changes from 1.3.1.6 to 1.3.1.7 ]  

GUI update  
- Change  
  - Help documents are not compiled, but consist of plain html files.  
  - More large icons are added for High DPI.  
  - The installer will send MultiPar.ini to the recycle bin at uninstallation.  

PAR2 client update  
- Change  
  - AVX2 feature is implemented internally.  
  - Additional memory barrier is removed.  


[ Hash value ]  

MultiPar1317.zip  
MD5: DFC81D79AA0EBF27DA8945C1EECB2019  
SHA1: AAED4AEF23A8C9643032A3E8D5B652C52AF081C4  

MultiPar1317_setup.exe  
MD5: 16BC2DF7DF0033EDADF39D6D9F3BD2FE  
SHA1: FFAA6C11912164F2A8C0305DE6231B7DFD409E56  


[ Hash value of other source code packages ]  
 Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu).  

MultiPar_par2j_1317.7z  
MD5: B46EDD4E3789E65712D50E6B59E355FB  
SHA1: D951E582EC2B7B6BA6947AA95F4176D816A18044  

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

MultiPar_Help_1317.7z  
MD5: 0021DB7D2CA3B75912267E6D5DA70A3D  
SHA1: AC436E130A112A58C60EA49ED8DCF9E2E19AACC3  
