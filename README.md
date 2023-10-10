# MultiPar

### v1.3.3.0 is public

&nbsp; This is a testing version to improve speed of PAR2 calculation. 
Because the new method isn't tested so much, there may be a bug, failure, or mistake. 
Be careful to use this non-stable version. 
When you don't want to test by yourself, you should not use this yet. 
If you see a problem, please report the incident. 
I will try to solve as possible as I can.

&nbsp; The PAR2 calculation speed may be 10% ~ 50% faster than old version. 
The optimization depends on hardware environment. 
I don't know what is the best setting on which PC. 
From [many tests of debug versions](https://github.com/Yutaka-Sawada/MultiPar/issues/99), 
it will select maybe better setting automatically. 
Thanks testers for many trials. 
If you want to compare speed of different settings on your PC, you may try those debug versions.

&nbsp; I changed GPU implementation largely, too. 
To adopt CPU optimization, it will process smaller tasks on GPU. 
Because GPU don't use CPU's cache, it's inefficient for GPU's task. 
I don't know that new method is faster than old version or not.

Threshold to use GPU:
- Data size must be larger than 200 MB.
- Block size must be larger than 64 KB.
- Number of source blocks must be more than 192.
- Number of recovery blocks must be more than 8.

&nbsp; Because [a user requested](https://github.com/Yutaka-Sawada/MultiPar/issues/102), 
I implemented a way to add 5th item in "Media size" on Create window. 
Write this line `MediaList4=name:size` under `[Option]` section in `MultiPar.ini`. 
Currently, you cannot change the item on Option window.


[ Changes from 1.3.2.9 to 1.3.3.0 ]  

GUI update
- Change
  - Option adapted to new "lc" settings.
  - It's possible to add 5th item in "Media size" on Create window.

PAR2 client update
- Change
  - Max number of using threads is increased to 32.
  - Threshold to use GPU was decreased.

- Improvement
  - Matrix inversion may use more threads.
  - L3 cache optimization was improved for recent CPUs.


[ Hash value ]  

MultiPar1330.zip  
MD5: 79570F84B74ECF8E5100561F7AAC3803  
SHA1: ACF7F164001708789C5D94003ED6B5C172235D54  

MultiPar1330_setup.exe  
MD5: D1F1A5A4DF1C9EDD698C9A017AF31039  
SHA1: 4C3314B909572A303EBBE8E015A2E813841CFA33  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must start the installer with administrative privileges by selecting 
"Run as administrator" on right-click menu.

&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0).
