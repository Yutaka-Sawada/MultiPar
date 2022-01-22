# MultiPar

v1.3.2.1 is public

&nbsp;  This includes some bug fix and a sample of new feature. 
I found a problem in my old code, and fixed them. 
Because they are 32-bit version PAR2 client and PAR1 client, nobody saw them for long time. 
Thanks [Safihre for finding the bug](https://github.com/Yutaka-Sawada/MultiPar/issues/55).

 I changed some behavior of handling invalid filenames on Windows OS. 
In previous version, those bad filenames were sanitized automatically. 
But, it might be strange and confused users of other OSes. 
Thanks [Danilo for reporting the issue](https://github.com/Yutaka-Sawada/MultiPar/issues/58). 
Now, this version warns such incompatible filenames only. 
Because it's difficult to rename some bad fileanames on Windows Explorer, 
I made a simple tool to rename invalid filenames, too. 
The [tool "Rename7" is available on my GitHub page](https://github.com/Yutaka-Sawada/Rename7).

 Some [users requested a queue repair feature](https://github.com/Yutaka-Sawada/MultiPar/issues/57) ago. 
I didn't make such utility, because there were some tools for the usage. 
But, it seems to be old, or it's not updated so much. 
So, I implemented a feature to repair multiple PAR2 recovery sets in queue. 
Currently, it's a simple tool and will be changed more. 
If a user is interested in the feature, 
please read "Command_Queue.txt" in "help" folder. 
Because it's sample, there may be a problem or fault. 
I will improve later, when users tested the behavior.


[ Changes from 1.3.2.0 to 1.3.2.1 ]  

GUI update  
- New  
  - For a folder with PAR2 files, MultiPar may invoke ParQueue.  

- Change  
  - In command-line, it uses "/" for each option instead of "-".  

All clients update  
- Change  
  - It will warn incompatible filenames on Windows OS.  

PAR1 client update  
- Bug fix  
  - Failure in setting a file pointer was fixed.  

PAR2 client update  
- Bug fix  
  - Possible stack overflow at searching missing files was fixed.  


[ Hash value ]  

MultiPar1321.zip  
MD5: FC155A166F5C31F7FA2373F06A866427  
SHA1: 2E4DF4FF1DDEC301A51A9FA33C4F66B41D1F24C2  

MultiPar1321_setup.exe  
MD5: B4D94783D4CB3A72EEBF51A893C1ED39  
SHA1: B22AE13319FD3856BD4E9D7CC042119FDF6A4E34  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu.  


[ Hash value of other source code packages ]  
&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu).  

MultiPar_par2j_1321.7z  
MD5: 4853C852E6DAEB8BCC034595C46CFE87  
SHA1: 24027B8E91DD32EF3A16931091BBD84D88EDBDBF  

MultiPar_par1j_1321.7z  
MD5: 2BE9BCCA25672BD7FC6773B6D5C3A831  
SHA1: 2F3759136922627ADFC6197D0CA3911508104694  

MultiPar_sfv_md5_1321.7z  
MD5: 5CD627C2D768290EF574230259A19487  
SHA1: E768B38783DF56289549BBAA3D65DBDAE679213B  

MultiPar_ShlExt_1320.7z  
MD5: C413655ABF85BCFF3D4B349BAAADC24B  
SHA1: 8552E6CCF647B065D91E494D2751567C144ABD36  

MultiPar_ResUI_1319.7z  
MD5: E03B90A433466C945D726B5A49B4E547  
SHA1: E30FB11B8F121D44CC1CC368E8D91F06CFC15551  

MultiPar_Help_1320.7z  
MD5: 1F8CC009B1A5F11EFBA999C7225E4311  
SHA1: 6E392602F82A96E3015FEA65A590E08D2B6E39CB   
