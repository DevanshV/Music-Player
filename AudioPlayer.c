/****************************************************************************
* Devansh Vaid - @devanshv
* Kenny Jung - @k22jung
*****************************************************************************/


/****************************************************************************
*  Copyright (C) 2008-2012 by Michael Fischer.
*
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the author nor the names of its contributors may
*     be used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
*  THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
*  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
*  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
*  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
*  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
*  SUCH DAMAGE.
*
****************************************************************************
*  History:
*
*  07.11.2008  mifi  First Version, based on FatFs example.
*  11.02.2012  mifi  Tested with EIR.
*  23.08.2012  mifi  Tested with an Altera DE1.
****************************************************************************/
#define __MAIN_C__

/*=========================================================================*/
/*  Includes                                                               */
/*=========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>
#include "altera_avalon_pio_regs.h"
#include "time.h"

/*=========================================================================*/
/*  DEFINE: All Structures and Common Constants                            */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Macros                                                         */
/*=========================================================================*/

#define PSTR(_a)  _a

/*=========================================================================*/
/*  DEFINE: Prototypes                                                     */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Definition of all local Data                                   */
/*=========================================================================*/
static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

/*=========================================================================*/
/*  DEFINE: Definition of all local Procedures                             */
/*=========================================================================*/

/***************************************************************************/
/*  TimerFunction                                                          */
/*                                                                         */
/*  This timer function will provide a 10ms timer and                      */
/*  call ffs_DiskIOTimerproc.                                              */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static alt_u32 TimerFunction (void *context)
{
   static unsigned short wTimer10ms = 0;

   (void)context;

   Systick++;
   wTimer10ms++;
   Timer++; /* Performance counter for this module */

   if (wTimer10ms == 10)
   {
      wTimer10ms = 0;
      ffs_DiskIOTimerproc();  /* Drive timer procedure of low level disk I/O module */
   }

   return(1);
} /* TimerFunction */

/***************************************************************************/
/*  IoInit                                                                 */
/*                                                                         */
/*  Init the hardware like GPIO, UART, and more...                         */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static void IoInit(void)
{
   uart0_init(115200);

   /* Init diskio interface */
   ffs_DiskIOInit();

   //SetHighSpeed();

   /* Init timer system */
   alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

} /* IoInit */

/*=========================================================================*/
/*  DEFINE: All code exported                                              */
/*=========================================================================*/

uint32_t acc_size;                 /* Work register for fs command */
uint16_t acc_files, acc_dirs;
FILINFO Finfo;
#if _USE_LFN
char Lfname[512];
#endif

char Line[256];                 /* Console input buffer */

FATFS Fatfs[_VOLUMES];          /* File system object for each logical drive */
FIL File1, File2;               /* File objects */
DIR Dir;                        /* Directory object */
uint8_t Buff[8192] __attribute__ ((aligned(4)));  /* Working buffer */

static
FRESULT scan_files(char *path)
{
    DIR dirs;
    FRESULT res;
    uint8_t i;
    char *fn;


    if ((res = f_opendir(&dirs, path)) == FR_OK) {
        i = (uint8_t)strlen(path);
        while (((res = f_readdir(&dirs, &Finfo)) == FR_OK) && Finfo.fname[0]) {
            if (_FS_RPATH && Finfo.fname[0] == '.')
                continue;
#if _USE_LFN
            fn = *Finfo.lfname ? Finfo.lfname : Finfo.fname;
#else
            fn = Finfo.fname;
#endif
            if (Finfo.fattrib & AM_DIR) {
                acc_dirs++;
                *(path + i) = '/';
                strcpy(path + i + 1, fn);
                res = scan_files(path);
                *(path + i) = '\0';
                if (res != FR_OK)
                    break;
            } else {
                //      xprintf("%s/%s\n", path, fn);
                acc_files++;
                acc_size += Finfo.fsize;
            }
        }
    }

    return res;
}

/***************************************************************************/
/*  Custom Functions and Global Variables                                  */
/***************************************************************************/
int filenum = 0;
unsigned long fileSize[20];
char filename[20][20] = {""};
int currIndex = 0;
int currSong = 0;
long p1, p2, p3;
uint32_t s1, s2, cnt, blen = sizeof(Buff);
FILE* lcd;

unsigned int l_buf;
unsigned int r_buf;
alt_up_audio_dev * audio_dev;
uint8_t res, b1, drv = 0;
uint16_t w1;


// Selects a song using the LCD screen, will store the index of the song after play is pressed
void selectLCD (){
	alt_u8 currbutton;

	alt_u8 button = IORD(BUTTON_PIO_BASE,0);
	fprintf(lcd,"%i. %s\n\n", currIndex+1, filename[currIndex]);

	// Do not allow song to instantly play if pause is pressed
	if (button == 0xd) {
		while(button == 0xd)button = IORD(BUTTON_PIO_BASE,0) & 0xf;
	}

	while (1){
	  button = IORD(BUTTON_PIO_BASE,0) & 0xf;

	  if (button == 0xe || button == 0xd || button == 0x7){
		  currbutton = button;

		  // Wait for button to be let go
		  while(currbutton == button){
			  currbutton = IORD(BUTTON_PIO_BASE,0) & 0xf;
		  }

		  if (button == 0xe){ // fast forward
			  if (currIndex < filenum - 1){
				  currIndex++;
			  } else {
				  currIndex = 0;
			  }
		  } else if (button == 0xd){ // play/pause
			  break;
		  } else if (button == 0x7){ // backwards
			  if (currIndex > 0){
				  currIndex--;
			  } else {
				  currIndex = filenum - 1;
			  }
		  }

		  fprintf(lcd,"%i. %s\n\n", currIndex+1, filename[currIndex]);

	  }
	}

	return;
}



int playSong(){

	uint8_t out = 0;
	uint8_t rr_forward = 0;
	int count = 0;
	int jump_bytes = 0;

	int p1_i = fileSize[currIndex];
	alt_u8 button = IORD(BUTTON_PIO_BASE,0);

	// If current selected song does not match what was paused from before, update file parameters
	if (currSong != currIndex || p1 == 0){
		f_open(&File1, filename[currIndex], 1);
		p1 = fileSize[currIndex];
	}

	// Keep reading the song until stop/pause is pressed or the song runs out of data to read
	while (p1>0 && !out) {
		button = IORD(BUTTON_PIO_BASE,0);

		if (button != 0x7){
			rr_forward = 0;
			count = 0;
		}

		int n = 0; // Location of where you are reading from in the buffer

		if (button == 0x7 && !rr_forward) { // Skips file pointer back to play in rewind

			rr_forward = 1;
			p1 += 384000;

			if (p1_i > p1) {
				jump_bytes = p1_i - p1;
				count = 350;
			} else {
				jump_bytes = 0;
				p1 = fileSize[currIndex];
				while(button == 0x7)button = IORD(BUTTON_PIO_BASE,0) & 0xf;
			}

			res = f_lseek(&File1, jump_bytes  );
			p1 -= 512;
			cnt = 512;

		} else if ((uint32_t) p1 >= 512){
		    cnt = 512;
		    p1 -= 512;
		} else {
			cnt = p1;
			p1 = 0;
		}

		res = f_read(&File1, Buff, cnt, &s2);

		// Checks for stop or pause button presses
		if (button == 0xb) { // Stop
		   out++;
		   currSong = -1;
		} else if (button == 0xd) { // Pause
		   out++;
		   currSong = currIndex;
		}

		// Writes 512 bytes of buffer into the device
		while (((n+3) < cnt) && !out) {
			int fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
			if (fifospace > 0) // check if write space is available
			{
			  l_buf = (Buff[n+1] << 8) + Buff[n];
			  r_buf = (Buff[n+3] << 8) + Buff[n+2];

			  alt_up_audio_write_fifo(audio_dev, & (r_buf), 1, ALT_UP_AUDIO_RIGHT);
			  alt_up_audio_write_fifo(audio_dev, & (l_buf), 1, ALT_UP_AUDIO_LEFT);

			  if (button == 0xe) {//check for forward seek
				  n += 8;
			  } else {
				  n += 4;
			  }
			}
		}

		if (button == 0x7 && rr_forward){
			count--;

			if (count == 0) {
				rr_forward = 0;
			}
		}

	}

	return out;
}


int isWav(char *filename){

	int l = strlen(filename);

	if (l > 4)
		return strcasestr(filename,".WAV");

	return 0;

}

// Loads data into fileSize and filename from file directory
void getFileList(){
	char *ptr = NULL;
	uint8_t res;

	res = f_opendir(&Dir, ptr);
	filenum = 0;
	p1 = 0;

	for (;;) {

		res = f_readdir(&Dir, &Finfo);

		if ((res != FR_OK) || !Finfo.fname[0])
			break;

		if (isWav((Finfo.fname))){
			strcpy(filename[filenum], &(Finfo.fname[0]));
			fileSize[filenum] = Finfo.fsize;
			++filenum;
		}


	}
}






/***************************************************************************/
/*  Main                                                                   */
/***************************************************************************/
int main(void)
{
	int fifospace;
	uint8_t exit_condition = 0;
    char *ptr, *ptr2;
    alt_u8 button;

    static const uint8_t ft[] = { 0, 12, 16, 32 };
    uint32_t ofs = 0, sect = 0, blk[2];
    FATFS *fs;

    audio_dev = alt_up_audio_open_dev ("/dev/Audio");

    IoInit();

#if _USE_LFN
    Finfo.lfname = Lfname;
    Finfo.lfsize = sizeof(Lfname);
#endif

    // Mount disk
    f_mount(0, &Fatfs[0]);

    // Opens LCD for writing
    lcd = fopen(LCD_DISPLAY_NAME,"w");

    // Update fileSize and filename from file directory
    getFileList();

    while(1){
    	selectLCD();

    	exit_condition = 0;

    	while (!exit_condition) {

    		// exit_condition represents if the stop button has been pressed
    		exit_condition = playSong();

    		if (!exit_condition) {

    			// Increment current index if it doesn't exceed the total number of .wav files
				if (currIndex < filenum - 1){
					  currIndex++;
				} else {
					  currIndex = 0;
				}

				fprintf(lcd,"%i. %s\n\n", currIndex+1, filename[currIndex]);
    		}


    	}
    }

    return (0);
}

/*** EOF ***/
