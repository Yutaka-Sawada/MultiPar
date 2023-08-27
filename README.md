# MultiPar

### v1.3.2.9 is public
&nbsp; This is the final release of v1.3.2 tree. 
Because I want to public this as a stable version, I didn't change contents so much. 
PAR clients are same as previous version. 
Including long term used applications may be good to avoid false positive at Malware detection.

&nbsp; I fixed a [compatibility issue in calling 7-Zip](https://github.com/Yutaka-Sawada/MultiPar/issues/92), 
which I didn't know the change. 
Thanks Lyoko-Jeremie for bug report. 
The incident happened, when a user selected many files.

&nbsp; I made a sample feature to Save & Restore different "base directories". 
When you put PAR files in another folder from source files, it will set the previous directory automatically. 
Because this feature was tested little, it's disabled by default at this time. 
If you want to enable, add section `[Path]` on "MultiPar.ini". 
Then set `MRUMax` value, which is the maximum number of stored directries. 
You may set the value upto 26. It's disabled, when the value is 0. 
These two lines are like below:
```
[Path]
MRUMax=5
```

&nbsp; While I made MultiPar as an utility tool, I didn't give priority to its speed. 
If someone wants faster Parchive tool, I suggest to use ParPar tools instead of MultiPar. 
They are "[High performance PAR2 create client for NodeJS](https://github.com/animetosho/ParPar)" or 
"[speed focused par2cmdline fork](https://github.com/animetosho/par2cmdline-turbo)". 
Though the speed depends on hardware environments and user's setting, it would be 50% ~ 100 % faster than my par2j. 
Only when you have a very fast graphics borad, GPU enabled par2j may be faster. 
I plan to improve speed of par2j in next v1.3.3 tree.
Though it will become 20% ~ 30% faster than old par2j, ParPar would be faster mostly.


[ Changes from 1.3.2.8 to 1.3.2.9 ]  

GUI update
- New
  - Verification may save different base directories in MultiPar.ini file.

- Bug fix
  - Archiver's option was updated for recent 7-Zip versions.


[ Hash value ]  

MultiPar132.zip  
MD5: 305D86C8C7A0F5C1A23CEAFFBE4F02BF  
SHA1: 464BB7AB7D14FD35D2AEF99042EEB8E556DA0417  

MultiPar132_setup.exe  
MD5: 18F9BE1FF1C6D668E3A3906C691CCB98  
SHA1: 116C6B2A15FCFD9BB74F0EF9D6C8A4BF78299588  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu.

&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0).
