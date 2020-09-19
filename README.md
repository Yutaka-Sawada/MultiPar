# MultiPar

v1.3.1.1 is public

 This version is minor update only. There is no big change nor bug fix. 
I release this to show some small changes, and write what were different before forget, hehe. 
When you have no problem in using previous versions, you don't need to update to this.

 I tried to make a function to calculate multiple MD5 hash values of some files on SSD at once. 
But, I didn't implement it in this version yet, because it's not tested well. 
I may finish it in future.

 There is an option "Memory usage upto around". 
By default (Auto) is between 75% and 87% against available memory. 
It's not a rate against total memory. 
The auto setting 's detail is like below;
32-bit max allocation size = 7/8 of available memory - 1/16 of total memory
64-bit max allocation size = 7/8 of available memory - 1/32 of total memory

 For those who use MultiPar in background, I changed an option "Run clients with lower priority". 
It decreases priority of MultiPar GUI itself, too. 
The level of priority may become -1 for GUI and -2 for clients. 
I added an option to open Verify window as minimized state. 
At this time, it can be enabled at "MultiPar.ini" only, because it's testing purpose still. 
If many users want to use the feature, I may add the item on GUI Option.


[ Changes from 1.3.1.0 to 1.3.1.1 ]

* GUI update  
Change  
 It's possible to type or edit directory on Folder selecting dialog.  
 An option "Run clients with lower priority" decreases priority of GUI, too.  
 Verify button will be disabled, when recovery files are deleted after repair.  

* PAR2 client update  
Change  
 64-bit version may allocate a little more memory than before by default.  


[ Hash value ]

MultiPar1311.zip  
MD5 : 77E112F2B181039EF842DA5F5E8E5FD3  
SHA-1 : B6680E102FF4CCF639BA562333CFC4FF6AC3C20E  

MultiPar1311_setup.exe  
MD5: 9E1653EE15F751B4670CCA9EB7BAF8C6  
SHA1: 43E7EAEB21DA2CD957091976F6C802E4C900ED00  



[ Hash value of other source code packages ]
 Old versions and source code packages are available at [OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu) now.  

MultiPar_par2j_1311.7z  
MD5: 2F5644DC6EF10A277B2455C5148A31F4  
SHA1: FA5E8C4E766A87AD3851EDDFE8142EC02A8799D9  

MultiPar_par2j_extra_1294.7z  
MD5: 6D165CDA2645924ACAFE902F02FAD309  
SHA1: D77D4EA778423D5D8F820B8EAF97F733950F9FB1  

MultiPar_par1j_1300.7z  
MD5: 6C38E0E713D3D68D398C9A51363D153E  
SHA1: 33675AF52D58B350792937FFBAD55C5401B06EAA  

MultiPar_sfv_md5_1300.7z  
MD5: 284A62C612F8FAF6325E258845ECA645  
SHA1: C2F4F136571083EEDB0713D0B42E58EE36CF1805  

MultiPar_ShlExt_1298.7z  
MD5: BE0F04DF1A6B936F23F6F01930562248  
SHA1: 52818266B45ECE135EECFF12D8DA2640A6AD5075  

MultiPar_ResUI_1306.7z  
MD5: 94533C7023ECC1EBBF695553E1A76E49  
SHA1: 90B329BF1EA54D74CD85F39EEDC9B2A8CF7125FE  

MultiPar_Help_1307.7z  
MD5: B9BA3E9E06A6F5C79C32CB88C96AEC08  
SHA1: E7B2D5D0CD6B9704D0D9B9BEE803C9E5EC20F42F  
