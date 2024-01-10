# MultiPar

### v1.3.3.2 is public

&nbsp; This is a small fix version to improve performance of GPU acceleration. 
It will become faster on AMD Radeon graphics boards. 
It may be slightly faster on Nvidia GeForce graphics boards. 
There is no difference in CPU calculation. 
Because this isn't tested so much, there may be a bug, failure, or mistake. 
If you see a problem, please report the incident. 
I will try to solve as possible as I can.

&nbsp; I changed 3 points in my OpenCL implementation. 
It's possible to test them by `lc` option at command-line. 
Thanks [cavalia88, Slava46, and Anime Tosho for many tests and wonderful idea](https://github.com/Yutaka-Sawada/MultiPar/issues/107). 
OpenCL perfomance is varied in every graphics boards. 
If you have a fast graphics board, enabling "GPU acceleration" would be faster. 
If it's not so fast (or is slow) on your PC, just un-check the feature.
1) Data transfur between PC's RAM and GPU's VRAM
2) Calculation over GPU
3) Calculate 2 blocks at once to reduce number of table lookup


[ Changes from 1.3.3.1 to 1.3.3.2 ]  

PAR2 client update
- Improvement
  - GPU acceleration will work well on AMD graphics boards.


[ Hash value ]  

MultiPar1332.zip  
MD5: 5F2848ED7F65C632D1FED42A39B66F95  
SHA1: CFA2CC6D217704BE2AF9DEDE15B117E9DC26A25B  

MultiPar1332_setup.exe  
MD5: 338F9D0842762338DC83921BBE546AF8  
SHA1: 2A11FD544D49AA7B952214733C9D8E53F647592E  
&nbsp; To install under "Program Files" or "Program Files (x86)" directory, 
you must select "Install for all users" at the first dialog.

&nbsp; Old versions and source code packages are available at 
[GitHub](https://github.com/Yutaka-Sawada/MultiPar/releases) or 
[OneDrive](https://1drv.ms/u/s!AtGhNMUyvbWOaSo1n_R8awJ_hg0).
