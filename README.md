# MultiPar

### v1.3.3.1 is public

&nbsp; This is a testing version to improve speed of PAR2 calculation. 
Because the new method isn't tested so much, there may be a bug, failure, or mistake. 
Be careful to use this non-stable version. 
When you don't want to test by yourself, you should not use this yet. 
If you see a problem, please report the incident. 
I will try to solve as possible as I can.

&nbsp; CPU's L3 cache optimization depends on hardware environment. 
It's difficult to guess the best setting for unknown type. 
It seems to work well on Intel and AMD 's most CPUs. 
Thanks Anime Tosho and MikeSW17 for long tests. 
But, I'm not sure the perfomance of rare strange kind CPUs. 
If you want to compare speed of different settings on your CPU, 
you may try samples (TestBlock_2023-08-31.zip) in "MultiPar_sample" folder 
on [OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOg0cF2UHcs709Icv4).

&nbsp; I improved GPU implementation very much. 
Thanks [Slava46 and K2M74 for many tests](https://github.com/Yutaka-Sawada/MultiPar/issues/99). 
While I almost gave up to increase speed, their effort encouraged me to try many ways. 
Without their aid, I could not implement this GPU function. 
OpenCL perfomance is varied in every graphics boards. 
If you have a fast graphics board, enabling "GPU acceleration" would be faster. 
If it's not so fast (or is slow) on your PC, just un-check the feature.

&nbsp; I saw a new feature of Inno Setup 6, which changes install mode. 
It shows a dialog to ask which install mode. 
Then, a user can install MultiPar in "Program Files" directory by selecting "Install for all users". 
This method may be easier than starting installer by "Run as administrator". 
I test the selection dialog at this version. 
If there is no problem nor complaint from users, I use this style in later versions, too.


[ Changes from 1.3.3.0 to 1.3.3.1 ]  

Installer update
- It shows dialog to select "per user" or "per machine" installation.

PAR2 client update
- Change
  - Max number of threads to read files on SSD was increased to 6.

- Improvement
  - GPU acceleration would become faster.


[ Hash value ]  

MultiPar1331.zip  
MD5: ECFC1570C839DD30A2492A7B05C2AD6E  
SHA1: 5E0E4CC38DAA995294A93ECA10AEB3AE84596170  

MultiPar1331_setup.exe  
MD5: A55E6FA5A6853CB42E3410F35706BAD9  
SHA1: 8D46BD6702E82ABA9ACCFA5223B2763B4DCEFE9E  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must select "Install for all users" at the first dialog.

&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0).
