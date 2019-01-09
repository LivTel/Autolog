/*   
    Copyright 2006-2014, Astrophysics Research Institute, Liverpool John Moores University.

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

/*
This scans a single directory for any LT files, reads the header information
from the FITS and creates a text file output giving basic observational parameters.
By running this at the end of each night, an observing log is created. This is 
different from any log generated by the RCS in that it record data which finally
made it to the output file directory rather than exposures which were started, but
may later have been aborted due to weather, TOOP etc.

There is nothing very clever in here. It should all be easy to hack about to add
new parameters as we decide we need them.
 
*/


/*# define _POSIX_SOURCE 
# define _POSIX_C_SOURCE 199309L 
# define _XOPEN_SOURCE 
# define _XOPEN_SOURCE_EXTENDED
# define _GNU_SOURCE */
#define _ISOC99_SOURCE

#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>		/* File permissions */
#include <dirent.h>		/* Directory access */

#include "fitsio.h"
/* slalib is only needed for the MJD conversion which is curently commented out */
/*#include "slalib.h" */
/*#include "slamac.h" */
#include "lt_filenames.h"
#include "autolog.h"


/* GLOBAL error code */
int Autolog_Error;

int main(int argc, char**argv)
{
  /* Misc. admin variables, counters etc */
  FILE *proglog,*outlog;
  unsigned int badfilect,filect;
  int fits_stat,skip_this_file,no_dprt,ii,jj;
  char comment[FC];
  char outlog_name[1024],putative_outlogdate[9];
  int create_outlog_name,multiple_nights_data;
  int tmp_int;
  char tmp_str[1024];
  /* int sla_stat=0 */

  LogInfo *LogInfo_vec;

  fitsfile *fitsin;
  DIR *pwd;
  struct dirent *pwd_ls;

  double *data_to_sort;
  unsigned int *data_indices;
  
  char cur_fits[200],tmp_fits[200],logpath[200];
  /*char ext[5];*/
  LTFileName cur,tmp_cur;

  int date_year,date_month,date_day,hour,minute;
  double second;
  /*double frac_day,mjdate; */
  /* mjday is an integer whereas mjdate includes a fractional part to specify the time*/
  /* unsigned int mjday; */
  time_t timer;
  
  /* char fits_str1[FIELD_LEN]; Even though cFITSIO sets this FIELD_LEN parameter, it does not enforce
   * it and will gracefully read in much longer fields which then overflow their array bounds
   * without any error messge or warning. Safer just to allocate an unnecessarily large string. */
  char fits_str1[1024];
  int fits_int;

  /* Misc variable initialisation */
  Autolog_Error = 0;		/* Value returned on exit */
  LogInfo_vec = NULL;		/* Set to NULL so first call to realloc does not cause a crash */

  badfilect = 0;		/* Number of files in the designated directory which were rejected and not read */
  filect = 0;			/* Number of files for which data if currently held in *LogInfo_vec */
  fitsin = NULL;		/* FITSIO FITS file pointer */

  proglog = NULL;

  /* Set a dummy value to enable us to identify the first run through the directory reading loop */
  sprintf(putative_outlogdate,"00000000");
  multiple_nights_data = 0;


  /* Check there are the correct number of command line parameters */
  create_outlog_name  = 0;
  if( (argc != 2) && (argc != 3) ){
    echo_usage();
    Autolog_Error = -10;
    exit(Autolog_Error);
  }
  switch ( argc ) {
  case 2:
    create_outlog_name  = 1;
    break;
  case 3:
    create_outlog_name  = 0;
    strcpy(outlog_name,argv[2]);
    break;
  }


  /* Open the input directory. If it cannot be opened, give an error and quit */
  pwd = NULL;
  pwd = opendir(argv[1]);
  if (pwd == NULL){
    Autolog_Error = -21;
    printf("Error opening directory (%d) - %s\n\n",Autolog_Error,argv[1]);
    echo_usage();
  }
  else{

    /* Create and open progress/error log file */
    timer = time(NULL);
    sprintf(logpath,"%s/autolog_status.log",argv[1]);
    proglog = fopen(logpath,"w");
    if(proglog==0){
      Autolog_Error = -23;
      printf("Could not open progress log (%d): %s\n",Autolog_Error,logpath);
      printf("Proceding no further\n");
      exit(Autolog_Error);
    }
    fprintf(proglog,"First line of the log.\n"); fflush(proglog);

    /* Loop over all the files in the directory, reading one at a time */
    while ( (pwd_ls=readdir(pwd)) ){
      if( strcmp(pwd_ls->d_name,".") && strcmp(pwd_ls->d_name,"..") && strcmp(pwd_ls->d_name,logpath) ){
	/* Deconstruct standard LT filename into a set of flags. If it is not
	 * a valid LT filename, give and error and proceeed to next file */
        if(chop_filename(pwd_ls->d_name,&cur)!=0){
	  Autolog_Error = 31;
	  fprintf(proglog,"Not an LT file name (%d): %s\n",Autolog_Error,pwd_ls->d_name);
	  if (DEBUG) { printf("Not an LT file name (%d): %s\n",Autolog_Error,pwd_ls->d_name); fflush(NULL); }
	  badfilect++;
	}				/* Flow returns to while (pwd_ls=readdir(pwd)) */
	else{
	  /* Ignore non FITS files. There could be reduced data products in the directory which 
	   * have valid LT names, but are not FITS */
          if(strncmp(cur.ext,"fits",4)==0){
            if(DEBUG) { printf("current exposure : %s\n",cur.exposure); fflush(NULL); }
            fprintf(proglog,"current exposure : %s\n",cur.exposure);

	    /* Check the date in this filename against all the others in the directory. If at the end
	     * this directory contains only files from a single night, we will use that date as the 
	     * log filename. If however there is a mix, we will resort to making up a semi-random filename
	     * based on the UTSTART in the last file read. This is about the best guess we can come up
 	     * with as to a sensible default filename 
	     */
	    if( (create_outlog_name==1) && (multiple_nights_data==0) ) {
	      /* This is first run through because dummy value is still in putative_outlogdate */
              if(strcmp(putative_outlogdate,"00000000")==0) {
		/* printf("Replacing dummy with real name on first run through: %s %s\n",cur.date,putative_outlogdate); */
	        strcpy(putative_outlogdate,cur.date);
	      } else {
		/* printf("Compare cur.date and putative_outlogdate: |%s| |%s|\n",cur.date,putative_outlogdate);  */
	        /* Check current file against putative_outlogdate, i.e., the first one read */
	        if(strcmp(cur.date,putative_outlogdate)!=0) {
		  /* printf("This looks like a new date: |%s| |%s|\n",cur.date,putative_outlogdate); */
		  multiple_nights_data = 1;
	        }
	      }
	    }
	    

	    /* Several FITS header keywords are set by Dp(RT). If the current file is unreduced,
	     * we first check to see if a reduced version exists. If it does, we bale out and ignore the
	     * unreduced version. The reduced one will get read in turn. If no reduced version exists,
	     * we do read it, but error messages will crop up in the logs 			*/
	    skip_this_file = 0;
	    no_dprt = 0;		/* Initially assume there is dp(rt) output */
	    if(cur.p[0] == '0'){
	      tmp_cur = cur;
	      tmp_cur.p[0] = '1';
	      construct_filename(&tmp_cur,tmp_fits);
	      fprintf(proglog,"File %s has not been reduced. Checking to see if %s exists\n",cur.exposure,tmp_fits);
	      if(fileex(tmp_fits)){
		fprintf(proglog,"Reduced data is available, so we will ignore this file.\n");
		skip_this_file = 1;		/* Used later to ignore errors on this file. We know why it failed */
		fits_stat = 1;			/* Don't run any FITSIO commands on this file, since it never gets opened */
	      }
	      else{
		fprintf(proglog,"No reduced data exists, so we are going to get some errors from this file.\n");
		no_dprt = 1;		/* Informational flag that none of the dp(rt) data will be available */
	      }

	    }

	    if(skip_this_file==0){
	      /* Allocate and initialise memory */
	      LogInfo_vec = (LogInfo *)realloc(LogInfo_vec,(filect+1)*sizeof(LogInfo));
	      init_LogInfo(&LogInfo_vec[filect]); 	/* Set strings to blank and values mostly to zero */

	      sprintf(LogInfo_vec[filect].exposure,"%s",cur.exposure);

	      /* Open FITS file */	    
	      sprintf(cur_fits,"%s/%s.%s",argv[1],cur.exposure,cur.ext);
	      /*sprintf(cur_fits,"%s/%s.fits",argv[1],cur.exposure); */
	      fits_stat = 0;
              fits_open_file(&fitsin,cur_fits,READONLY,&fits_stat);
              if(fits_stat){
	        Autolog_Error = 41;
	        fprintf(proglog,"Failed to open FITS (%d)- %s\n",Autolog_Error,cur.exposure);
	        printf("Failed to open FITS (%d)- %s\n",Autolog_Error,cur.exposure);
	        badfilect++;
	        LogInfo_vec[filect].error = -1;
	        fits_stat = 1;		/* This will prevent any FITSIO commands being executed */
		skip_this_file = 1;
	      }
	    }

	    if(skip_this_file==0){
	      /* This would be a bit safer if I read it into another string and then copied over a 
	       * max of 14 chars into LogInfo_vec[filect]->ra. Never mind. */
	      ffgkys(fitsin,"INSTRUME",LogInfo_vec[filect].instrume,comment,&fits_stat);
	      LogInfo_vec[filect].instrume[12]='\0';

	      ffgkys(fitsin,"PROPID",LogInfo_vec[filect].propid,comment,&fits_stat);
	      LogInfo_vec[filect].propid[16]='\0';

	      ffgkys(fitsin,"RA",LogInfo_vec[filect].ra,comment,&fits_stat); 
	      ffgkys(fitsin,"DEC",LogInfo_vec[filect].dec,comment,&fits_stat); 
	      LogInfo_vec[filect].ra[13]='\0';
	      LogInfo_vec[filect].dec[13]='\0';


	      ffgkys(fitsin,"UTSTART",LogInfo_vec[filect].utstart,comment,&fits_stat);
	      LogInfo_vec[filect].utstart[12]='\0';
	      ffgky(fitsin,TFLOAT,"EXPTIME",&LogInfo_vec[filect].exptime,comment,&fits_stat);

	      /* We have here the capabilty of reading a `mean airmass' keyword if Dp(RT)
	       * has been run. Since this is not yet calculated, we do the same whether
	       * the data are reduced or raw */
	      if(no_dprt)
	        ffgky(fitsin,TFLOAT,"AIRMASS",&LogInfo_vec[filect].airmass,comment,&fits_stat);
	      else
	        ffgky(fitsin,TFLOAT,"AIRMASS",&LogInfo_vec[filect].airmass,comment,&fits_stat);
	      /* Missing airmass is non-critical. Just reset the error. 
	      if(fits_stat==202) {
                  fits_stat = 0;
	      } */

	      ffgky(fitsin,TINT,"CCDXBIN",&LogInfo_vec[filect].binning,comment,&fits_stat);

	      if(!no_dprt){
		/* By preference we read L1SEESEC, but for backwards compatibilty with old images which 
		 * do not have that keyword, we try L1SEEING if L1SEESEC does not exist. */
	        ffgky(fitsin,TFLOAT,"L1SEESEC",&LogInfo_vec[filect].l1seeing,comment,&fits_stat);
		if(fits_stat==202) {
		  fits_stat = 0;
	          ffgky(fitsin,TFLOAT,"L1SEEING",&LogInfo_vec[filect].l1seeing,comment,&fits_stat);
		}

	        ffgky(fitsin,TFLOAT,"L1PHOTOM",&LogInfo_vec[filect].l1photom,comment,&fits_stat);
	        /* ffgky(fitsin,TFLOAT,"L1SKYBRT",&LogInfo_vec[filect].l1skybrt,comment,&fits_stat); */
	        ffgky(fitsin,TFLOAT,"SCHEDSKY",&LogInfo_vec[filect].l1skybrt,comment,&fits_stat);
		/* Non-critical error for L1 parameters to be missing. For example, this always true for SupIRCam */
		if(fits_stat==202) {
		  fits_stat = 0;
		}
		/* Sometimes RCS writes UNKNOWN into SCHEDSKY which causes FITSIO error because it is not a TFLOAT. */
		if(fits_stat==408) {
		  fits_stat = 0;
		  fits_read_key(fitsin,TSTRING,"SCHEDSKY",fits_str1,comment,&fits_stat);
		  if ( strncmp(fits_str1,"UNKNOWN",7) == 0 ) LogInfo_vec[filect].l1skybrt = 99.9;
		}
	      }


	      /* Here we can read OBJECT from the OSS or CAT-NAME from the TCS*/
	      ffgkys(fitsin,"CAT-NAME",fits_str1,comment,&fits_stat); 
	      if(fits_str1[0]=='\0'){
	        /* Try OBJECT instead. There may be something there. */
	        fits_stat = 0;
	printf("About to read OBJECT. fits_stat = %d. fits_str1 = %s\n",fits_stat,fits_str1);
	        ffgkys(fitsin,"OBJECT",fits_str1,comment,&fits_stat); 
	printf("After reading OBJECT. fits_stat = %d. fits_str1 = %s\n",fits_stat,fits_str1);
	      }
	      if(!fits_stat){
	        /* Replace all ' ' with '_' */
	        tmp_int = 0;
	        while (tmp_int < strlen(fits_str1) ) {
		  if (fits_str1[tmp_int] == ' ') fits_str1[tmp_int] = '_';
		  tmp_int++;
	        }
	        strncpy(LogInfo_vec[filect].object,fits_str1,19);
	        LogInfo_vec[filect].object[18]='\0';
	      }

	      /* GROUPID
	       * Needs manipulating
	       * 	Truncate to 20char
	       * 	Replace whitespace
	       */
	      ffgkys(fitsin,"GROUPID",fits_str1,comment,&fits_stat); 
              if(!fits_stat){
	        /* Replace all ' ' with '_' */
	        tmp_int = 0;
	        while (tmp_int < strlen(fits_str1) ) {
		  if (fits_str1[tmp_int] == ' ') fits_str1[tmp_int] = '_';
		  tmp_int++;
	        }
	        /* Only keep the first 20 chars, plus a \0 terminator */
		snprintf(LogInfo_vec[filect].groupid,21,"%s",fits_str1); 
              } else {
                sprintf(LogInfo_vec[filect].groupid,"Unknown");
                fits_stat = 0;
              }

	      /* ROTSKYPA not currently done */
	    
	      /* Time & DATE */
	      ffgkys(fitsin,"DATE-OBS",fits_str1,comment,&fits_stat);
	      if(!fits_stat){
	        if(sscanf(fits_str1,"%4d-%2d-%2dT%2d:%2d:%lf",&date_year,&date_month,&date_day,&hour,&minute,&second)!=6)
  		sprintf(LogInfo_vec[filect].utstart,"%2d:%2d:%6.3f",hour,minute,second);
		/* Though it does not get reported in the log file, MJD is used as the sort key to get the files in order */
  	        ffgkys(fitsin,"MJD",fits_str1,comment,&fits_stat);
  	        ffgky(fitsin,TDOUBLE,"MJD",&LogInfo_vec[filect].mjd,comment,&fits_stat);
		if(fits_stat) {
		  printf("Non-critical error reading MJD: %d: %s\n",fits_stat,LogInfo_vec[filect].exposure);
		  LogInfo_vec[filect].mjd = 0;
		  fits_stat = 0;
		}
  	        /* It is not yet clear as to whether we will always get MJD in the FITS header. We are therefore
  	         * leaving this code here to make it easy to replace if we find we need it. 
  	         * Note that sections of text surrounded by ** are comments.	
	        ffgkys(fitsin,"DATE-OBS",fits_str1,comment,&fits_stat);
	        if(sscanf(fits_str1,"%4d-%2d-%2dT%2d:%2d:%lf",&date_year,&date_month,&date_day,&hour,&minute,&second)!=6){
	          printf("Error reading date format: |%s|\n",fits_str1);
	        }
	        else{
	          second = (int)(second+0.5);		** seconds as an integer        **
		
	          ** Convert the date to MJD and the time to a fractional day 			*
	          * By combining these we get the MJDate, which is written to the database	**
	          slaDtf2d(hour,minute,second,&frac_day,&sla_stat);
	          slaCldj(date_year,date_month,date_day,&mjdate,&sla_stat);
	          mjday = (int)mjdate;
	          mjdate += frac_day;
	          LogInfo_vec[filect].mjd = mjdate;
	        }
	        */
	      }
	    	    
	      /* FILTERS 
	       * In all instruments so far there is one filter, so that is assumed. If the first filter
	       * is read OK, it tries to do the second. Failer of the second is not considered an error
	       * because we just assume this is SupIRCam with one filter */
	      sprintf(tmp_str,"");
	      ffgkys(fitsin,"FILTER1",fits_str1,comment,&fits_stat);
	      if(!fits_stat){
		if ( strcmp(fits_str1,"Clear") && strcmp(fits_str1,"clear") && strcmp(fits_str1,"NONE") ) 
		  sprintf(tmp_str,"%s",fits_str1);
	        ffgkys(fitsin,"FILTER2",fits_str1,comment,&fits_stat);
	        if(!fits_stat) {
		  if ( strcmp(fits_str1,"Clear") && strcmp(fits_str1,"clear") && strcmp(fits_str1,"NONE") ) {
		    if (strlen(tmp_str)) strcat(tmp_str,",");
	            strcat(tmp_str,fits_str1);
		  }
		  /* So try a third filter too! */
		  ffgkys(fitsin,"FILTER3",fits_str1,comment,&fits_stat);
		  if(!fits_stat) {
		    if ( strcmp(fits_str1,"Clear") && strcmp(fits_str1,"clear") && strcmp(fits_str1,"NONE") ) {
		      if (strlen(tmp_str)) strcat(tmp_str,",");
	              strcat(tmp_str,fits_str1);
		    }
		  }
		  else 
		    fits_stat = 0;
		}
	        else
  		  fits_stat = 0;
		/* Copy the result into LogInfo_vec[filect].filter */
		if ( strlen(tmp_str) == 0) 
		  sprintf(LogInfo_vec[filect].filter,"None");
		else
		  strcpy(LogInfo_vec[filect].filter,tmp_str);
	      } else {
	        sprintf(LogInfo_vec[filect].filter,"Error_reading_FITS");
	      }	
	    	    
	      /* GRATING  */
	      ffgkys(fitsin,"GRATID",fits_str1,comment,&fits_stat);
	      if(!fits_stat){
	        snprintf(LogInfo_vec[filect].grating,12,"%s ",fits_str1);
	      } else {
	        sprintf(LogInfo_vec[filect].grating," NA ");
  		fits_stat = 0;
	      }




	      /* Check all the L1STAT?? header keywords to see if any errors got written by Dp(RT) 
	       * Values of 1 or -1 imply no error detected. */
	      if(!no_dprt){
	        ffgky(fitsin,TINT,"L1STATOV",&fits_int,comment,&fits_stat);
	        if( abs(fits_int)!=1 ) 
	          LogInfo_vec[filect].error -= 2;
	        ffgky(fitsin,TINT,"L1STATZE",&fits_int,comment,&fits_stat);
	        if( abs(fits_int)!=1 ) 
	          LogInfo_vec[filect].error -= 4;
	        ffgky(fitsin,TINT,"L1STATTR",&fits_int,comment,&fits_stat);
	        if( abs(fits_int)!=1 ) 
	          LogInfo_vec[filect].error -= 8;
	        /*ffgky(fitsin,TINT,"L1STATZM",&fits_int,comment,&fits_stat);
	        if(fits_int!=1 && fits_int!=-1) 
	          LogInfo_vec[filect].error -= -34;*/
	        ffgky(fitsin,TINT,"L1STATFL",&fits_int,comment,&fits_stat);
	        if(fits_int!=1 && fits_int!=-1) 
	          LogInfo_vec[filect].error -= 16;
	        ffgky(fitsin,TINT,"L1STATDA",&fits_int,comment,&fits_stat);
	        if(fits_int!=1 && fits_int!=-1) 
	          LogInfo_vec[filect].error -= 32;
	        ffgky(fitsin,TINT,"L1STATFR",&fits_int,comment,&fits_stat);
	        if(fits_int!=1 && fits_int!=-1) 
	          LogInfo_vec[filect].error -= 64;

		/* Absense of these is not a critical error. I'll reset the status to 0 if they were missing */
	        if (fits_stat == 202) fits_stat = 0;
	      }


	      if(fits_stat) {
		printf("Error in fits_stat at end of parsing. Resetting to zero to allow file closing\n");
		printf("fits_stat for %s was %d\n",LogInfo_vec[filect].exposure,fits_stat);
		fits_stat = 0;
	      }

	      /* Read everything I want. Close the file and clean up */
              fits_close_file(fitsin,&fits_stat);

	    } /* End of skip_this_file */

	    if(fits_stat && !skip_this_file){
	      LogInfo_vec[filect].error = fits_stat;
	      fprintf(proglog,"A FITSIO error has occured: %d\n",fits_stat);
	    }

	    if(!skip_this_file){
	      fprintf(proglog,"Finished with %s\n",cur.exposure); fflush(proglog);
	      filect++;
	    }


	  }  /* End of if(cur_ext=="fits") */

	}  /* End of `this is a valid filename' */ 
      } /* End of `this is not . or .. */

    } /* End of while(readdir)) for incoming directory */
    closedir(pwd);
  }

  fprintf(proglog,"%5d files successfully read into log\n",filect); 
  fprintf(proglog,"%5d bad files not read\n",badfilect); fflush(proglog);

  if(filect==0){
    fprintf(proglog,"Nothing to do. Closing.\n"); 
    fclose(proglog);
    return 0;
  }


  /* Create an array holding the MJDs to sort. This could probably be done more
   * elegantly, but this will at least work. The array will be ordered simply in the
   * order in which the data are in LogInfo_vec. Then we create a seconed array which 
   * contains the array indices in the sorted order. We then reasd out the data in
   * order LogInfo[index[ii]] where 0 < ii < filect 
   *
   * This might be better and more generalised if I wrote a sort routine which you give 
   * any element of LogInfo_Struct and it sorts on that as the key, but that would need
   * writing. I have a simple vector sorter ready already.				*/
  data_to_sort = (double *)malloc(sizeof(double) * filect);
  data_indices = (unsigned int *)malloc(sizeof(unsigned int) * filect);
  for(ii=0; ii<filect; ii++)
    data_to_sort[ii] = LogInfo_vec[ii].mjd;
    
  indexx_dble(filect,data_to_sort,data_indices);

  /*for(ii=0; ii<filect; ii++)
    printf("%d : %d : %f\n",ii,data_indices[ii],data_to_sort[data_indices[ii]]);  */

  free(data_to_sort);

  if ( create_outlog_name == 1) {
    if( multiple_nights_data == 1) {
      if(date_year==0 || date_month==0 || date_day==0){
        printf("Error reading observations date. Log will be called autolog.log\n");
        fprintf(proglog,"Error reading observations date. Log will be called autolog.log\n");
        sprintf(logpath,"%s/autolog.log",argv[1]);
      }
      else {
	/* printf("Creating a new filename becuase of multiple mixed nights'\n"); */
        sprintf(logpath,"%s/%4d%02d%02d.log",argv[1],date_year,date_month,date_day);
      }
    } else {
      sprintf(logpath,"%s/%s.log",argv[1],putative_outlogdate);	
    }
  }
  /* Otherwise use the name provide on the command line */
  else 
    sprintf(logpath,"%s/%s",argv[1],outlog_name);


  outlog = fopen(logpath,"w");
  if(proglog==0){
    Autolog_Error = -53;
    printf("Could not open output file (%d): %s\n",Autolog_Error,logpath);
    printf("Proceeding, but writing only to screen\n");
    fprintf(proglog,"Could not open output file (%d): %s\n",Autolog_Error,logpath);
  }

  printf(          "############ ################## ################ ########################### #### ############ #################### ### ########## ###### ##### #### ####################### #################### ###\n");
  printf(          "     UTC        OBJECT_NAME          PROPID          RA             dec       AIR  INSTRUMENT        FILTERS        BIN  GRATING    EXPOS SEING  SKY        FILENAME               GroupID        ERR\n");
  printf(          "    START                                                  J2000                                                                      sec   sec  mag             \n");
  printf(          "############ ################## ################ ########################### #### ############ #################### ### ########## ###### ##### #### ####################### #################### ###\n\n");

  if(outlog){
    fprintf(outlog,"############ ################## ################ ########################### #### ############ #################### ### ########## ###### ##### #### ####################### #################### ###\n");
    fprintf(outlog,"     UTC        OBJECT_NAME          PROPID          RA             dec       AIR  INSTRUMENT        FILTERS        BIN  GRATING    EXPOS SEING  SKY        FILENAME               GroupID        ERR\n");
    fprintf(outlog,"    START                                                  J2000                                                                      sec   sec  mag             \n");
    fprintf(outlog,"############ ################## ################ ########################### #### ############ #################### ### ########## ###### ##### #### ####################### #################### ###\n");
  }


  for(ii=0; ii<filect; ii++){
    jj = data_indices[ii];

    printf("%12s %18s %16s %13s %13s %4.2f %12s %20s %3d %11s %6.1f %5.1f %4.1f %22s %20s %d\n",
	LogInfo_vec[jj].utstart,
	LogInfo_vec[jj].object,
	LogInfo_vec[jj].propid,
	LogInfo_vec[jj].ra,
	LogInfo_vec[jj].dec,
	LogInfo_vec[jj].airmass,
	LogInfo_vec[jj].instrume,
	LogInfo_vec[jj].filter,
	LogInfo_vec[jj].binning,
	LogInfo_vec[jj].grating,
	LogInfo_vec[jj].exptime,
	LogInfo_vec[jj].l1seeing,
/*	LogInfo_vec[jj].l1photom, */
	LogInfo_vec[jj].l1skybrt,
	LogInfo_vec[jj].exposure,
	LogInfo_vec[jj].groupid,
	LogInfo_vec[jj].error);

    if(outlog) 
      fprintf(outlog,"%12s %18s %16s %13s %13s %4.2f %12s %20s %3d %11s %6.1f %5.1f %4.1f %22s %20s %d\n",
	LogInfo_vec[jj].utstart,
	LogInfo_vec[jj].object,
	LogInfo_vec[jj].propid,
	LogInfo_vec[jj].ra,
	LogInfo_vec[jj].dec,
	LogInfo_vec[jj].airmass,
	LogInfo_vec[jj].instrume,
	LogInfo_vec[jj].filter,
	LogInfo_vec[jj].binning,
	LogInfo_vec[jj].grating,
	LogInfo_vec[jj].exptime,
	LogInfo_vec[jj].l1seeing,
/*	LogInfo_vec[jj].l1photom, */
	LogInfo_vec[jj].l1skybrt,
	LogInfo_vec[jj].exposure,
	LogInfo_vec[jj].groupid,
	LogInfo_vec[jj].error);
  }
  
  free(data_indices);
  free(LogInfo_vec);

  if(outlog)
    fclose(outlog);

  fclose(proglog);

  return 0;  
} 



/* Takes a filename (as a string) and a second string into which the file extention will be written. 	*
 * The method is very simplistic. Anything after the first occurence of `.' is deemed to be the 	*
 * extention. It knows nothing about subsequent dots.							*
 * A maximum of maxlen characters are written. The returned string will always be \0 terminated.	*
 * An error is retuned if no `.' was found in the filename.						*
 */
int get_ext(char *fullname,int maxlen,char *ext)
{
  char tmp_str[FILENAME_LEN+5];

  /* Copy into a temp string because strtok() modifies the input string */
  strcpy(tmp_str,fullname);

  if(  strcspn(tmp_str,".")==strlen(tmp_str) )
    return 1;
  
  strtok(tmp_str,".");
  strncpy(ext,(char *)strtok(NULL,"."),maxlen-1);
  ext[maxlen] = '\0';

  return 0;

}




/* Check if directory exists on disc */
/* Returns 1 if directory could be opened 0 otherwise */ 
int dir_exists (char dirname[]) {
  DIR *dptr;

  if( (dptr = opendir(dirname)) == NULL) 
    return 0;

  closedir(dptr);
  return 1;
}


/*
 * Command line help
 */
void echo_usage()
{
  printf("autolog <DIR name> [output_file_name]\n");
  printf("<DIR name> is string giving path to directory containing the data files.\n");
  printf("output_file_name is optional name of file into which to write the log.\n");
  printf("\tIt will be created in <DIR name>\n");
  printf("\tIf not specified, autolog will try to create a sensible default output filename.\n");
  printf("\tOutput and any error logs will be written to the same directory\n");
  printf("Create a text logfile of all the files in directory.\n");
  printf("Output is sorted by UT of the exposure\n");
}



/* 
 *   Index the elements of the array arrin[] such that
 *   arrin[indx[j]] is in ascending order for j = 0, 1, ... nn-1.
 *
 *   Input:
 *   arrin[]  : array of numbers to be sorted
 *   nn       : number of elements in arrin[]
 *
 *   Output:
 *   indx[]   : integer array containing index of elements
 *
 *   This C code written by ...  
 *     Peter & Nigel, Design Software, 42 Gubberley St, Kenmore, 4069, Australia.
 *   i.e., RJS did not put that goto there!!!!
 *
 *   Notes ...
 *   -----
 *   (1) The input quantities nn and arrin are unchanged on exit.
 *   (2) This routine has been adapted from that published in the book
 *       W.H. Press et al
 *       Numerical Recipes: The art of scientific computing.
 *       Cambridge University Press 1986
 */
int indexx_dble (unsigned int nn, double arrin[], unsigned int indx[])
{  
  int jj, ii, ir, indxt, L;
  double q;

  /* This NR function crashes if there is only one element in the array
   * to sort. I check that here and return straight away. */
  if (nn==1){
    indx[0] = 0;
    return 0;
  }

  /* Initialize the index array with consecutive integers */
  for (jj = 0; jj < nn; ++jj) indx[jj] = jj;

  L = nn / 2;
  ir = nn - 1;

  do {
    if (L > 0) {
      --L;
      indxt = indx[L];
      q = arrin[indxt];
    }
    else {
      indxt = indx[ir];
      q = arrin[indxt];
      indx[ir] = indx[0];
      --ir;
      if (ir == 0) {
	indx[0] = indxt;
	goto Finish;
      }
    }

    ii = L;
    jj = L + L + 1;

    while (jj <= ir) {
      if (jj < ir) {
        if (arrin[indx[jj]] < arrin[indx[jj+1]]) 
	  ++jj;
      }
      if (q < arrin[indx[jj]]) {
        indx[ii] = indx[jj];
        ii = jj;
	jj += jj + 1;
      }
      else
         jj = ir + 1;
    }

    indx[ii] = indxt;
  } while (1);

  Finish:
  return 0;
}  


void init_LogInfo(LogInfo *to_init){

  sprintf(to_init->object,"                  ");
  sprintf(to_init->exposure,"                          ");
  sprintf(to_init->ra,"             ");
  sprintf(to_init->dec,"             ");
  sprintf(to_init->utstart,"            ");
  to_init->mjd = 0;
  to_init->airmass = 0;
  sprintf(to_init->instrume,"            ");
  sprintf(to_init->propid,"         ");
  to_init->exptime = 0;
  sprintf(to_init->grating,"           ");
  sprintf(to_init->filter,"                        ");
  to_init->binning = 0;
  to_init->l1seeing = 999;
  to_init->l1photom = -999;
  to_init->l1skybrt = 99.9;
  to_init->error = 0;

  return;

}



/*
 *  Returns 1 if file can be opened, 0 otherwise
 */
int fileex(char *file)
{
  FILE *fptr;

  if( (fptr = fopen( file, "r")) == NULL )
    return 0;

  fclose(fptr);
  return 1;
}


