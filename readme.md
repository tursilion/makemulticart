1.1

Just a quick command-line tool to pack EA#5 images into a single bank-switched loader cart. The resulting cart will still require 32k!

The EA#5 programs must be self contained - no data files and no chaining to other programs.

This uses the multicart menu because it's possible to pack more EA#5 files in a single cart than will display on the normal menu.

You can hex edit the names starting at offset 0x2000 -- 20 characters are allocated for each display name. Just don't go past the last space and you'll be fine. Otherwise the names are based on the filename.

Input files must be EA#5 files with TIFILES headers. Place all of them in a folder and pass the folder to this program:

makemulticart c:\myfolder\ c:\output\cartridge8.bin

I heavily recommend the "8.bin" ending. It will be automatically added if you don't end with .bin

As of 9/7/2021 there is a new version with a random option, you MUST use today's also-released version of vgmcomp2's Quickplayer for it to work!
