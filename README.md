# MultiPar

v1.3.2.0 is public

&nbsp; This is a beta version to test new encoder & decoder. 
I implemented a way of Cache Blocking for CPU's L3 cache optimization. 
It will calculate multiple blocks at once on multi-Core CPU. 
Old method calculated each block independently, 
and could not avail the advantage of shared memory. 
New method may use shared L3 cache more often, and will be fast on recent PCs.

&nbsp; Thanks [prdp19 and Slava46](https://github.com/Yutaka-Sawada/MultiPar/issues/47) for many tests. 
Thanks [Anime Tosho](https://github.com/Yutaka-Sawada/MultiPar/issues/21) for some idea and advice. 
Also, other users aided my development. 
Without their help, I could not perform this speed improvement.

&nbsp; While new version seems to be faster on most cases, 
it may happen to be slow for a few blocks. 
Though I don't know the speed on old PCs, it may not become slow. 
If you see a failure or strange result, please report the incident with ease. 
I will try to solve as possible as I can.

&nbsp; I adjusted CPU usage slider for CPUs with many Cores. 
Now, each position will set different number of threads always.  
Left most      : 1/4 of CPU cores  
One from left  : 2/4 of CPU cores  
Middle         : 3/4 of CPU cores  
One from right : 4/4 of CPU cores, or use one less threads on CPU with 6 or more Cores.  
Right most     : May use one more threads on CPU with 5 or less Cores.  

&nbsp; I improved calculating hash of multiple source files. 
From my testing result, I changed default number of threads for NVMe SSD. 
When you use a raid-system or external drive, it cannot detect the drive type. 
If it fails to detect, it uses HDD mode by default. 
At MultiPar options, it's possible to change the setting manually. 
You may select one of them; HDD, SSD, or Fast SSD. 
Caution, you should not select SSD, if your using drive is HDD.


[ Changes from 1.3.1.9 to 1.3.2.0 ]  

GUI update  
- Change  
  - Fast SSD is selectable as file access mode.  
  - Max number of log files was increased from 100 to 1000.  
  - CPU usage slider was adjusted on CPU with 6 or more Cores.  
  - Shadow of text over progress-bar becomes more smooth.  

PAR2 client update  
- Change  
  - Standard buffer size becomes double to decrease iteration.  
  - Single byte error in a single slice file may be corrected.  
  - Number of using threads was changed on CPU with 6 or more Cores.  
  - Enabling GPU won't use additional threads on multi-core CPU.  
  - Progress percent may move while writing blocks.  

- Improvement  
  - L3 cache optimization was implemented for multi-core CPU.  


[ Hash value ]  

MultiPar1320.zip  
MD5: 56524875BC77FD7A4E51A9E2C3F834CB  
SHA1: 245F3432DBCCAD335AEB2A70371EE57EFEF52CE7  

MultiPar1320_setup.exe  
MD5: 5D0A51F48CDE8FCB0B87CC949BE84DD1  
SHA1: B727D5193697E8C0A2335DB8233874CBDAFDEE40  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu.  


[ Hash value of other source code packages ]  
&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu).  

MultiPar_par2j_1320.7z  
MD5: 5DBF880047D05BC2E8FE349DC6EC610C  
SHA1: 1D50EFE97A7812B6AF0090B83AAEE61BCBABD1ED  

MultiPar_par1j_1318.7z  
MD5: F66285403BA0AD856BA6A8CCD922EBF5  
SHA1: 4CF5D819B16E60F1BBD82415D5F68CB46D3F53C3  

MultiPar_sfv_md5_1318.7z  
MD5: 4E6433808625C088E2773C961BBEBBD2  
SHA1: 68B54D178BA58637F63CC3E0CC656C96D4472A33  

MultiPar_ShlExt_1320.7z  
MD5: C413655ABF85BCFF3D4B349BAAADC24B  
SHA1: 8552E6CCF647B065D91E494D2751567C144ABD36  

MultiPar_ResUI_1319.7z  
MD5: E03B90A433466C945D726B5A49B4E547  
SHA1: E30FB11B8F121D44CC1CC368E8D91F06CFC15551  

MultiPar_Help_1320.7z  
MD5: 1F8CC009B1A5F11EFBA999C7225E4311  
SHA1: 6E392602F82A96E3015FEA65A590E08D2B6E39CB   
