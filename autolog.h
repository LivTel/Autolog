/*   
    Copyright 2006, Astrophysics Research Institute, Liverpool John Moores University.

    This file is part of autolog.

    autolog is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    autolog is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with autolog; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#ifndef _AUTOLOG_H
#define _AUTOLOG_H


#ifndef MAX
#define MAX(A,B) ((A)>(B)?(A):(B))
#endif

#ifndef MIN
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif

#ifndef SWAP
#define SWAP(a,b) itemp=(a);(a)=(b);(b)=itemp;
#endif

/* This ought to be defined in lt_filename.h */
/*#define FILENAME_LEN          27 */           /* Max length of std LT filename WITHOUT extention */
#define FIELD_LEN		33		/* Max length for most PEST input fields +1 */

#define FV FLEN_VALUE      			/* Shorthand FITS definition */
#define FC FLEN_COMMENT    			/* Shorthand FITS definition */




/* A structure to contain all the interesting data about a particular exposure */
typedef struct LogInfo_Struct{
  char object[19];
  char exposure[FILENAME_LENGTH];
  char ra[14];
  char dec[14];
  char utstart[13];
  double mjd;		/* Used for sorting. Not output to log */
  float airmass;
  char instrume[13];
  char propid[17];
  char groupid[21];
  float exptime;
  char grating[12];
  char filter[48];
  int binning;
  float l1seeing;
  float l1photom;
  float l1skybrt;
  int error;
}LogInfo;


int get_ext(char *fullname,int maxlen,char *ext);
int dir_exists (char dirname[]);
void echo_usage(void);
int indexx_dble (unsigned int nn, double arrin[], unsigned int indx[]);
void init_LogInfo(LogInfo *to_init);
int fileex(char *file);

#endif
