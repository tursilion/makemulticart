// MakeMultiCart.cpp
// Takes a folder of EA#5 files, and compiles them into a loader bank-switched ROM cart
// names are taken from the filenames, but can be extended by hex editting, space is left

#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <errno.h>
#include "multimenu.h"

using namespace std;
vector<string> filenames;
unsigned char buf[8192+6+128];    // max size (8k+6+128 6 bytes header, should be 8192, but some software was buggy, plus 128 bytes TIFILES)

unsigned char header[4096];
int hdrpos = 0;
unsigned char copydat[4096];
int copypos = 0;
unsigned char datadat[8192*64];   // 512kb
int datapos = 0;

unsigned char getPadChar() {
    static const char *txt = "Tursi's tool...";
    static int idx = 0;
    if (txt[idx] == '\0') idx = 0;
    return txt[idx++];
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Pass the path of a folder containing only EA#5 files in TIFILES format (others will be ignored)\n");
        printf("and the output file to write (will be non-inverted bankswitched ROM)\n");
        return 1;
    }

    string path = argv[1];
    path += "\\*";

    WIN32_FIND_DATA data;
    HANDLE h = FindFirstFile(path.c_str(), &data);
    while (INVALID_HANDLE_VALUE != h) {
        if (0 == (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            string thisFile = argv[1];
            thisFile += '\\';
            thisFile += data.cFileName;
            FILE *fp = fopen(thisFile.c_str(), "rb");
            if (NULL == fp) {
                printf("Failed to open %s, error %d\n", thisFile.c_str(), errno);
                return 1;
            }
            int sz = fread(buf, 1, sizeof(buf), fp);
            if (!feof(fp)) {
                printf("Skipping too big file: %s\n", thisFile.c_str());
            } else {
                if ((0 != memcmp(buf, "\x7TIFILES", 8))||(buf[10] != 1)) {
                    printf("Skipping non-TIFILES file or non-PROGRAM file: %s\n", thisFile.c_str());
                } else {
                    // remember it
                    filenames.push_back(thisFile);
                }
            }
            fclose(fp);
        }

        if (!FindNextFile(h, &data)) {
            FindClose(h);
            h = INVALID_HANDLE_VALUE;
        }
    }

    // sort it so the filenames are in order
    std::sort(filenames.begin(), filenames.end());

    // now process the menu - we build the header and the loader simultaneously, then patch them together

    // cart layout:
    // bank 0 - multicart
    // bank 1-x - data only
    // bank x - multicart

    // header layout:
    // AA01, 0000, 0000, 000C, 0000, 0000
    // title entries

    // Title layout (each entry 26 bytes):
    // next, start, >14, name (space padded 20 chars), >FF

    // copy program is written out literally

    header[hdrpos++] = 0xaa;    // header
    header[hdrpos++] = 0x01;
    header[hdrpos++] = 0x00;    // program count
    header[hdrpos++] = 0x00;
    header[hdrpos++] = 0x00;    // powerups
    header[hdrpos++] = 0x00;
    header[hdrpos++] = 0x60;    // program list
    header[hdrpos++] = 0x26;
    header[hdrpos++] = 0x00;    // DSRs
    header[hdrpos++] = 0x00;
    header[hdrpos++] = 0x00;    // subprograms
    header[hdrpos++] = 0x00;

    // we also put trampoline code here so there is a fixed address to copy it from
    // bl @>600c, data >address
    header[hdrpos++] = 0xc2;    // mov *r11,r11
    header[hdrpos++] = 0xdb;
    header[hdrpos++] = 0x02;    // li r4,>6020
    header[hdrpos++] = 0x04;
    header[hdrpos++] = 0x60;
    header[hdrpos++] = 0x20;
    header[hdrpos++] = 0x02;    // li r5,>8300
    header[hdrpos++] = 0x05;
    header[hdrpos++] = 0x83;
    header[hdrpos++] = 0x00;
    header[hdrpos++] = 0xcd;    // mov *r4+,*r5+
    header[hdrpos++] = 0x74;
    header[hdrpos++] = 0xcd;    // mov *r4+,*r5+
    header[hdrpos++] = 0x74;
    header[hdrpos++] = 0xcd;    // mov *r4+,*r5+
    header[hdrpos++] = 0x74;
    header[hdrpos++] = 0x04;    // b @>8300
    header[hdrpos++] = 0x60;
    header[hdrpos++] = 0x83;
    header[hdrpos++] = 0x00;

    // 0x6020
    header[hdrpos++] = 0xc8;    // mov r0,@>6000    this runs from scratchpad
    header[hdrpos++] = 0x00;
    header[hdrpos++] = 0x60;
    header[hdrpos++] = 0x00;
    header[hdrpos++] = 0x04;    // b *r11
    header[hdrpos++] = 0x5b;

    // 0x6026

    // it just makes life easier if copypos does not start at zero
    copydat[copypos++] = 0x54;  // TI
    copydat[copypos++] = 0x49;

    int boot = 0;
    bool first = true;
    for (string s : filenames) {
        FILE *fp = fopen(s.c_str(), "rb");
        if (NULL == fp) {
            // shouldn't happen now! Damn multitasking OS!
            printf("Late fail to open %s, error %d??\n", s.c_str(), errno);
            return 1;
        }
        // read it in
        int sz = fread(buf, 1, sizeof(buf), fp);
        fclose(fp);

        // remove TIFILES header
        memmove(buf, buf+128, sz-128);
        sz-=128;
        
        // parse the EA5 header
        int eaMore = buf[0]*256+buf[1];
        int eaSize = (buf[2]*256+buf[3]+1)/2;   // convert from bytes to words
        int eaLoad = buf[4]*256+buf[5];
        if (first) {
            printf("Processing %s...\n", s.c_str());

            // save the boot address
            boot = eaLoad;
            
            // add an entry to the header
            header[hdrpos] = (hdrpos+26+0x6000)/256;    // next entry (will zero later if needed)
            header[hdrpos+1] = (hdrpos+26+0x6000)%256;
            hdrpos += 2;
            header[hdrpos++] = copypos/256;             // code entry (offset now, will patch later)
            header[hdrpos++] = copypos%256;
            header[hdrpos++] = 20;                      // always 20 bytes of filename to allow hex editing
            
            //printf("  ... at copy offset >%04X\n", copypos);

            // strip any path off the filename
            size_t x = s.find_last_of('\\');
            string fn;
            if (x == -1) {
                fn = s;
            } else {
                fn = s.substr(x+1);
            }
            // first 20 chars of filename
            for (unsigned int idx=0; ((idx<fn.size()) && (idx<20)); ++idx) {
                header[hdrpos++] = fn[idx];     // multimenu supports lowercase
            }
            // last padding chars if under 20
            for (unsigned int idx=fn.size(); idx<20; ++idx) {
                header[hdrpos++] = ' ';
            }
            // padding byte to make even and make easier to hex edit by showing the boundary
            header[hdrpos++] = 0xff;

            // update the flag
            first = false;
        }

        // remove EA5 header
        memmove(buf, buf+6, sz-6);
        sz-=6;

        // store the data
        // first make sure we fit on a page, pad to new page if not
        if ((datapos%8192)+sz > 8192) {
            // pad to next page
            while (datapos%8192) {
                datadat[datapos++] = getPadChar();
            }
        }
        // now make sure we fit on the largest standard card
        if (datapos + sz >= sizeof(datadat)) {
            printf("Too much data for 512KB cartridge, abort.\n");
            return 1;
        }
        // all good, copy away
        int database = (datapos&0x1fff)+0x6000;
        memcpy(&datadat[datapos], buf, sz);
        datapos+=sz;

        // build the copy code
        // the bytes 994A need to remain unique for the later search
        // 0x6004 because page 0x6002 contains the boot code and 0x6000 is the multimenu
        // the -1 just makes sure we're on the right page in case we completely filled the last one
        copydat[copypos++] = 0x02;      // li r0,pageadr
        copydat[copypos++] = 0x00;
        copydat[copypos++] = (0x6004+(((datapos-1)/8192)*2))/256;
        copydat[copypos++] = (0x6004+(((datapos-1)/8192)*2))%256;

        copydat[copypos++] = 0x02;      // li r1,srcadr
        copydat[copypos++] = 0x01;
        copydat[copypos++] = database/256;
        copydat[copypos++] = database%256;

        copydat[copypos++] = 0x02;      // li r2,dstadr
        copydat[copypos++] = 0x02;
        copydat[copypos++] = eaLoad/256;
        copydat[copypos++] = eaLoad%256;

        copydat[copypos++] = 0x02;      // li r3,size
        copydat[copypos++] = 0x03;
        copydat[copypos++] = eaSize/256;
        copydat[copypos++] = eaSize%256;

        copydat[copypos++] = 0x06;      // bl @>994a (will patch later)
        copydat[copypos++] = 0xa0;
        copydat[copypos++] = 0x99;
        copydat[copypos++] = 0x4a;

        if (eaMore == 0) {
            // this was the last file in the list, so load the boot code
            copydat[copypos++] = 0x06;  // bl @>600c
            copydat[copypos++] = 0xa0;
            copydat[copypos++] = 0x60;
            copydat[copypos++] = 0x0c;
            copydat[copypos++] = boot/256;  // data boot
            copydat[copypos++] = boot%256;

            // next file will be first again
            first = true;
        }
    }

    if (!first) {
        printf("Warning, last file did not set final load flags...\n");
    }

    // zero out the last link address in the header
    header[hdrpos-26] = 0;
    header[hdrpos-25] = 0;

    // patch the boot addresses
    int off = 0x26 + 2;
    while (header[off]*256+header[off+1] != 0) {
        int oldoff = header[off]*256+header[off+1];
        int newoff = oldoff + (hdrpos+0x6000);

//        printf("Adjust copy offset from >%04X to >%04X\n", oldoff, newoff);

        header[off] = newoff/256;
        header[off+1] = newoff%256;
        off+=26;
    }

    // patch the copy subroutine links
    for (int idx=0; idx<copypos; idx+=2) {
        if (0 == memcmp(&copydat[idx], "\x6\xa0\x99\x4a", 4)) {
            idx+=2;
            copydat[idx] = (copypos+hdrpos+0x6000)/256;
            copydat[idx+1] = (copypos+hdrpos+0x6000)%256;
        }
    }

    // copy in the subroutine
    copydat[copypos++] = 0x02;              // li r4,copyfunc
    copydat[copypos++] = 0x04;
    copydat[copypos]   = (copypos+hdrpos+24+0x6000)/256;
    copydat[copypos+1] = (copypos+hdrpos+24+0x6000)%256;
    copypos+=2;
    
    copydat[copypos++] = 0x02;              // li r5,>8300
    copydat[copypos++] = 0x05;
    copydat[copypos++] = 0x83;
    copydat[copypos++] = 0x00;

    copydat[copypos++] = 0xcd;              // mov *r4+,*r5+
    copydat[copypos++] = 0x74;
    copydat[copypos++] = 0xcd;              // mov *r4+,*r5+
    copydat[copypos++] = 0x74;
    copydat[copypos++] = 0xcd;              // mov *r4+,*r5+
    copydat[copypos++] = 0x74;
    copydat[copypos++] = 0xcd;              // mov *r4+,*r5+
    copydat[copypos++] = 0x74;
    copydat[copypos++] = 0xcd;              // mov *r4+,*r5+
    copydat[copypos++] = 0x74;
    copydat[copypos++] = 0xcd;              // mov *r4+,*r5+
    copydat[copypos++] = 0x74;
    copydat[copypos++] = 0xcd;              // mov *r4+,*r5+
    copydat[copypos++] = 0x74;

    copydat[copypos++] = 0x04;              // b @>8300
    copydat[copypos++] = 0x60;
    copydat[copypos++] = 0x83;
    copydat[copypos++] = 0x00;

    copydat[copypos++] = 0xc4;              // mov r0,*r0       this code runs from scratchpad
    copydat[copypos++] = 0x00;
    copydat[copypos++] = 0xcc;              // mov *r1+,*r2+
    copydat[copypos++] = 0xb1;
    copydat[copypos++] = 0x06;              // dec r3
    copydat[copypos++] = 0x03;
    copydat[copypos++] = 0x16;              // jne lbl1
    copydat[copypos++] = 0xfd;
    
    copydat[copypos++] = 0xc8;              // mov r0,@>6002
    copydat[copypos++] = 0x00;
    copydat[copypos++] = 0x60;
    copydat[copypos++] = 0x02;

    copydat[copypos++] = 0x04;              // b *r11
    copydat[copypos++] = 0x5b;

    // now we can build the ROM

    // pad data to a full page
    while (datapos%8192) {
        datadat[datapos++] = getPadChar();
    }

    int desiredSize = ((datapos/8192)+2);  // total cartridge size in pages

    // only powers of 2 are meaningful, min is 4 and max is 256 (2MB) or 64 (512k)
    if (desiredSize < 4) desiredSize = 4;
    else if (desiredSize < 8) desiredSize = 8;
    else if (desiredSize < 16) desiredSize = 16;
    else if (desiredSize < 32) desiredSize = 32;
    else if (desiredSize <= 64) desiredSize = 64;
    else {
        printf("Cartridge larger than 512k, abort.\n");
        return 1;
    }
//    else if (desiredSize < 128) desiredSize = 128;
//    else desiredSize = 256;

    printf("Total cartridge size will be %dk\n", desiredSize*8);

    // pad out data to be this size - 3 pages
    while (datapos < (desiredSize-3)*8192) {
        datadat[datapos++] = getPadChar();
    }

    // make sure we didn't screw up the math
    if (datapos > 253*8192) {
        printf("Internal size calculation error, abort.\n");
        return 1;
    }

    // make room for the boot code
    memmove(datadat+16384, datadat, datapos);
    for (int idx=0; idx<16384; ++idx) {
        datadat[idx] = getPadChar();
    }

    // and copy it in, first the header
    memcpy(datadat+8192, header, hdrpos);
    // then the copy code
    memcpy(datadat+hdrpos+8192, copydat, copypos);

    // copy in the multicart menu binary to the first bank
    memcpy(datadat, MULTIMENU, SIZE_OF_MULTIMENU);

    // now copy same to the last bank
    memcpy(&datadat[(desiredSize-1)*8192], datadat, 8192);

    // and write the whole thing out
    string outname = argv[2];
    if (outname.substr(outname.size()-4) != ".bin") {
        outname += "8.bin";
    }
    FILE *fp = fopen(argv[2], "wb");
    if (NULL == fp) {
        printf("Failed to open output file '%s', code %d\n", argv[2], errno);
        return 1;
    }
    fwrite(datadat, 8192, desiredSize, fp);
    fclose(fp);

    printf("** DONE **\n");

    return 0;
}
