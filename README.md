# MultiPar

### v1.3.2.2 is public

&nbsp; This is an emergency update to fix some serious bugs. 
When I changed sanitizing function at previous version, I mistook some points. 
Then, it failed at handling sub directories. 
I updated par2j and others. (though PAR1 and checksumer don't support directory.) 
Thanks [Sam Lane for reporting the bug](https://github.com/Yutaka-Sawada/MultiPar/issues/60). 
Sometimes I might add a new bug, while I was trying to fix another bug. 
If someone sees a failure or strange behavior, please tell me with ease. 
I will solve as possible as I can.

&nbsp; I added a feature to purge PAR2 files in my PAR2 client. 
Though I don't use such risky feature, 
[it was requested for batch repair](https://github.com/Yutaka-Sawada/MultiPar/issues/59). 
If a user is interested in this, refer "/p" option in "Command_par2j.txt". 
Be careful to set the option, because it may happen to remove un-intended PAR2 files.


[ Changes from 1.3.2.1 to 1.3.2.2 ]  

Installer update  
 - Inno Setup supports Windows 7 or later.  

GUI update  
 - New  
   - I added /batch command to MultiPar.exe for batch scripting.  

PAR2 client update  
 - New  
   - A new option was added to remove recovery files.  

 - Bug fix  
   - A fault of sanitizing function was fixed.  


[ Hash value ]  

MultiPar1322.zip  
MD5: E9154665A4400559345F14DF53072626  
SHA1: 89AD517E65726F3A112AD7977A9CC9CB57FC2847  

MultiPar1322_setup.exe  
MD5: 0DE12560C4414C0F52189D8BB7190477  
SHA1: E82B435201E3E403805B0C68AA1A5C47A3426E2A  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu.  


[ Hash value of other source code packages ]  
&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu).  

MultiPar_par2j_1322.7z  
MD5: 21E90BB1C46E135DD88D76097BB290A1  
SHA1: 1DD2D2DCB9C5518ACE6DF8452066EC7C3B4B81FF  

MultiPar_par1j_1322.7z  
MD5: E80BEB9BBFEAC709B0647E97580CD107  
SHA1: 84F51338C2AB1D8B9179319F20EB159F27D9FA2A  

MultiPar_sfv_md5_1322.7z  
MD5: 468540C08059987573404A860B43979A  
SHA1: 2B8AD55E1DC626A9F152554637B71EBE32F7C474  

MultiPar_ShlExt_1320.7z  
MD5: C413655ABF85BCFF3D4B349BAAADC24B  
SHA1: 8552E6CCF647B065D91E494D2751567C144ABD36  

MultiPar_ResUI_1319.7z  
MD5: E03B90A433466C945D726B5A49B4E547  
SHA1: E30FB11B8F121D44CC1CC368E8D91F06CFC15551  

MultiPar_Help_1320.7z  
MD5: 1F8CC009B1A5F11EFBA999C7225E4311  
SHA1: 6E392602F82A96E3015FEA65A590E08D2B6E39CB   
