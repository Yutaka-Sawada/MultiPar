# MultiPar

v1.3.1.9 is public

&nbsp; This is the final release of v1.3.1 tree. 
There would be no serious problem. 
While I fixed some rare bugs and improved a bit, it's hard to test all possible cases. 
When you see a bug, odd incident, or strange behavior, please let me know. 
I will fix as possible as I can.

&nbsp; I tested verification of multiple source files on my new PC. 
From the result, I set max number of threads for NVMe SSD to 4 threads, 
when CPU has 6 or more logical cores. 
This will improve speed at verifying complete source files. 
(It doesn't affect for PAR files nor damaged files.)  
SATA SSD : It will verify max 2 files at once.  
NVMe SSD : It will verify max 3 or 4 files at once.

&nbsp; When many input file slices are same, their checksums become same, too. 
There was a bug in my sorting function. 
Nobody found the error for over than 10 years, and I solved this problem at last. 
Thanks [NilEinne](https://github.com/Yutaka-Sawada/MultiPar/issues/36) for reporting the very rare incident.

&nbsp; When a file includes duplicated data, it's difficult to find slices in proper position. 
In old versions, it might ignore overlap of slices for speed. 
Then, it happend to fail finding some slices. 
I implemented more complex method, and it will work well in most cases. 
Thanks [swarup459](https://github.com/Yutaka-Sawada/MultiPar/issues/42) for bug report, offering samples, and many tests.

&nbsp; It's possible to add PAR2 recovery record to a ZIP file. 
When I wrote the instructions ago, 4 GB over ZIP file was not common so much. 
Because recent Windows OS supports ZIP64 format, I updated the text for compatibility. 
You may read the "Add recovery record" page of MultiPar's Help documents. 
Or, you may read [the article on my web-site](http://hp.vector.co.jp/authors/VA021385/record.htm). 
Now, MultiPar supports large ZIP file with ZIP64 format. 
Thanks [Dwaine Gonyier](https://github.com/Yutaka-Sawada/MultiPar/issues/44) for noticing the potential problem.


[ Changes from 1.3.1.8 to 1.3.1.9 ]  

Installer update  
- Inno Setup was updated from v6.1.2 to v6.2.0.  

GUI update  
- Change  
  - Clickable link to access author's page becomes SSL.  
  - Appending recovery record supports 2 GB over file size.  

PAR2 clients update  
- Change  
  - When source files are on NVMe SSD, verification may become faster.  
  - Appending recovery record supports ZIP64 format.  

- Improvement  
  - Simple verification will find a short slice in a tiny file.  

- Bug fix  
  - A stack overflow problem in quick sort function was removed.  
  - A bug of searching slices in a file with repeated content was fixed.  


[ Hash value ]  

MultiPar131.zip  
MD5: EF3486BB39724EF6A4109F5B02D4E027  
SHA1: D935BFAFF5156C9460FB45639271339D1068F522  

MultiPar131_setup.exe  
MD5: E2F6EF68AEB9BE0CCDD4D5ABF2A3F318  
SHA1: C2615960B9B28223BC174FC1175CAAECCC8A713A  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu.  


[ Hash value of other source code packages ]  
&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0?e=4V0gXu).  

MultiPar_par2j_1319.7z  
MD5: 9AC4C38762E2DBF64D6D3A738CD7CCD6  
SHA1: FFC0DA1A0BBCAD08489C5499154DAA0216E10F51  

MultiPar_par1j_1318.7z  
MD5: F66285403BA0AD856BA6A8CCD922EBF5  
SHA1: 4CF5D819B16E60F1BBD82415D5F68CB46D3F53C3  

MultiPar_sfv_md5_1318.7z  
MD5: 4E6433808625C088E2773C961BBEBBD2  
SHA1: 68B54D178BA58637F63CC3E0CC656C96D4472A33  

MultiPar_ShlExt_1318.7z  
MD5: 57E79698A53458681CD19842391A202F  
SHA1: 646145F1B429C1CF592F907614889C98FBE7E756  

MultiPar_ResUI_1319.7z  
MD5: E03B90A433466C945D726B5A49B4E547  
SHA1: E30FB11B8F121D44CC1CC368E8D91F06CFC15551  

MultiPar_Help_1319.7z  
MD5: 37547FA074DC24491D1696F6F0DB7452  
SHA1: 8069C5745F9C7660236F17E3E087B4F7324382ED  
