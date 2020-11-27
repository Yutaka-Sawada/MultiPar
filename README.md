# MultiPar

v1.3.1.3 is public

 This version is a minor update to test a new behavior. 
It's an option "Don't search subfolders", in "Creation options" section, 
on "Client behavior" tab, of MultiPar Options window. 
While it's under "Creation options" section still, 
it affects verification and repair for experimental usage now. 
In command-line, an option "/fo" is available for verification (and/or reapir). 

 This change affects searching misnamed (or moved) files at verification. 
Without "/fo" option, par2j will search misnamed files under all sub-folders recursively. 
This behavior might be bad for some users, 
when there are many sub-folders or a child folder includes many files. 
Because searching misnamed (or moved) files from many possible files requires long time, 
it would give error (such like exclusive file access) or system heavy (HDD becomes too busy). 
When you have such problem in using MultiPar, you may try setting the option. 

 If you feel no problem in using MultiPar daily, you don't need to update to this version. 
When you don't check "Don't search subfolders" check-box, there is no difference. 
Only when you check it now, you will see the difference. 
If there's something wrong with this change, please notice me. 
I may change current style, if there is a problem. 
If new behavior has no problem, 
I will move "Don't search subfolders" option into "Common options" section at MultiPar option in future. 


[ Changes from 1.3.1.2 to 1.3.1.3 ]

* GUI update  
Change  
 An option "Don't search subfolders" is enabled for verification and reapir.  

* PAR2 client update  
Change  
 An option "/fo" is available for verification and reapir.  


[ Hash value ]

MultiPar1313.zip  
MD5 : 7F09AD4201867C8ACE7039DE417F47C4  
SHA-1 : 27AA6F7C6F28180012EA956F87868004D4F63C1C  

MultiPar1313_setup.exe  
MD5: 4557DF01B68AA0ACD9D62B9EE6D61C5C  
SHA1: 7C1652C0CC494E0AEAA346313744FC6F73451C83  


[ Hash value of other source code packages ]  
 Old versions and source code packages are available at [OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu) now.  

MultiPar_par2j_1313.7z  
MD5: 6E1EB1FF7E6E723A83D3F11183A1EE75  
SHA1: B95D66E4D6ECE310529C384C48BDDD37571EC7D5  

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
