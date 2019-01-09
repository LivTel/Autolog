/*   
    Copyright 2006, Astrophysics Research Institute, Liverpool John Moores University.

    This file is part of autolog.c.

    autolog.c is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    autolog.c is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with autolog.c; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/*
This scans the directory INCOMING_DIR for any LT files, reads the header information
from the FITS and creates a text file output giving basic observational parameters.

*/

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>		/* File permissions */
#include <dirent.h>		/* Directory access */

#include "fitsio.h"
#include "lt_filenames.h"
#include "autolog.h"


/* GLOBAL error code */
int Autolog_Error;

int check_header_flag(char keyword[9], char* task, FILE* proglog, fitsfile* fitsin);
int fileex(char *file);

int main(int argc, char**argv)
{
  /* Misc. admin variables, counters etc */
  FILE *proglog,*outlog;
  unsigned int badfilect,filect;
  int fits_stat,skip_this_file,no_dprt,ii,jj;
  char comment[FC],value[FC],tmp_str[1024];
  /* int sla_stat=0 */

  LogInfo *LogInfo_vec;

  fitsfile *fitsin;
  DIR *pwd;
  struct dirent *pwd_ls;

  double *data_to_sort;
  unsigned int *data_indices;
  
  char cur_fits[200],tmp_fits[200],logpath[200],ext[5];
  LTFileName cur,tmp_cur;
  int red_level;

  int date_year,date_month,date_day,degree,hour,minute,isecond;
  char year[5];
  double second,frac_day,mjdate;
  /* mjday is an integer whereas mjdate includes a fractional part to specify the time*/
  unsigned int mjday;
  time_t timer;
  
  char fits_str1[FIELD_LEN],fits_str2[FIELD_LEN];
  int fits_int;

  char keyword[9], task[FC];
  char instrument[80],filters[80];
  int status;

  /* Misc variable initialisation */
  Autolog_Error = 0;		/* Value returned on exit */
  LogInfo_vec = NULL;		/* Set to NULL so first call to realloc does not cause a crash */

  badfilect = 0;		/* Number of files in the designated directory which were rejected and not read */
  filect = 0;			/* Number of files for which data if currently held in *LogInfo_vec */
  fitsin = NULL;		/* FITSIO FITS file pointer */


  /* Check there are the correct number of command line parameters */
  if(argc != 2){
    echo_usage();
    Autolog_Error = -10;
    exit(Autolog_Error);
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
    sprintf(logpath,"%s/red_report.log",argv[1]);
    proglog = fopen(logpath,"w");
    if(proglog==0){
      Autolog_Error = -23;
      printf("Could not open progress log (%d): %s\n",Autolog_Error,logpath);
      printf("Proceding no further\n");
      exit(Autolog_Error);
    }

    /* Loop over all the files in the directory, reading one at a time */
    while (pwd_ls=readdir(pwd)){
      if( strcmp(pwd_ls->d_name,".") && strcmp(pwd_ls->d_name,"..")){
	/* Deconstruct standard LT filename into a set of flags. If it is not
	 * a valid LT filename, give and error and proceeed to next file */
	      printf("%s ",pwd_ls->d_name); fflush(NULL);
        if(chop_filename(pwd_ls->d_name,&cur)!=0){
	  Autolog_Error = 31;
	  fprintf(proglog,"Not an LT file name (%d): %s\n",Autolog_Error,pwd_ls->d_name);
	  printf("Not an LT file name (%d): %s\n",Autolog_Error,pwd_ls->d_name);
	  badfilect++;
	}				/* Flow returns to while (pwd_ls=readdir(pwd)) */
	else{
	  /* Ignore non FITS files. There could be reduced data products in the directory which 
	   * have valid LT names, but are not FITS */
          if(strcmp(cur.ext,"fits")==0){
	    fits_stat = 0;
            /*printf("current exposure : %s\n",cur.exposure); 
            fprintf(proglog,"current exposure : %s\n",cur.exposure); */

	    /* Several FITS header keywords are set by Dp(RT). If the current file is unreduced,
	     * we first check to see if a reduced version exists. If it does, we bale out and ignore the
	     * unreduced version. the reduced one will get read in turn. If no reduced version exists,
	     * we do read it, but error messages will crop up in the logs 			*/
	    skip_this_file = 0;
	    no_dprt = 0;		/* Initially assume there is dp(rt) output */
	    if(cur.p[0] == '0'){
	      tmp_cur = cur;
	      tmp_cur.p[0] = '1';
	      construct_filename(&tmp_cur,tmp_fits);
	      fprintf(proglog,"File %s may not have been reduced. Checking to see if %s exists\n",cur.exposure,tmp_fits);
	      if(fileex(tmp_fits)){
		fprintf(proglog,"Reduced data is available, so we will ignore this file.\n");
		skip_this_file = 1;		/* Used later to ignore errors on this file. We know why it failed */
		fits_stat = 1;			/* Don't run any FITSIO commands on this file, since it never gets opened */
	      }
	      else{
		fprintf(proglog,"No obviously reduced data exists, so we will give this file a try.\n");
		no_dprt = 1;		/* Informational flag that none of the dp(rt) data will be available */
	      }

	    }

	    if(!skip_this_file){

	      /* Open FITS file */	    
	      sprintf(cur_fits,"%s/%s.fits",argv[1],cur.exposure);
	      fits_stat = 0;
              fits_open_file(&fitsin,cur_fits,READONLY,&fits_stat);
              if(fits_stat){
	        Autolog_Error = 41;
	        fprintf(proglog,"Failed to open FITS (%d)- %s\n",Autolog_Error,cur.exposure);
	        printf("Failed to open FITS (%d)- %s\n",Autolog_Error,cur.exposure);
	        badfilect++;
	        LogInfo_vec[filect].error = -1;
	        fits_stat = 1;		/* This will prevent any FITSIO commands being executed */
	      }
	    }


	    /* This would be a bit safer if I read it into another string and then copied over a 
	     * max of 14 chars into LogInfo_vec[filect]->ra. Never mind. */
	    ffgkys(fitsin,"INSTRUME",instrument,comment,&fits_stat);
	    printf("(%s) ",instrument);

	    /*
	    ffgky(fitsin,TINT,"L1STATOV",&status,comment,&fits_stat);
	    switch(status){
	      case 1:
		printf("Overscan subtracted\n");
		fprintf(proglog,"Overscan subtracted\n");
		break;
	      case -1:
		printf("Overscan subtraction not requested\n");
		fprintf(proglog,"Overscan subtraction not requested\n");
		break;
	      case 0:
		printf("Overscan subtraction pending. This seems improbable. Is it an error?\n");
		fprintf(proglog,"Overscan subtraction pending. This seems improbable. Is it an error?\n");
		break;
	      default:
		if(status>0){
		  printf("Non-");
		  fprintf(proglog, "Non-");
		}
		printf("Critical error while overscan subtracting: %d",status);
		fprintf(proglog,"Critical error while overscan subtracting: %d",status); 
		break;
	    }
	    */
	    sprintf(keyword , "L1STATOV");
	    sprintf(task , "Overscan subtraction");
	    check_header_flag(keyword,task,proglog,fitsin);

	    sprintf(keyword , "L1STATDA");
	    sprintf(task , "Dark frame subtraction");
	    check_header_flag(keyword,task,proglog,fitsin);

	    sprintf(keyword , "L1STATFL");
	    sprintf(task , "Flatfielding");
	    check_header_flag(keyword,task,proglog,fitsin);

	    /* FILTERS */
	    ffgkys(fitsin,"FILTERI1",fits_str1,comment,&fits_stat);
	    ffgkys(fitsin,"FILTERI2",fits_str2,comment,&fits_stat);
	    if(!fits_stat)
	      sprintf(filters,"%s %s",fits_str1,fits_str2);
	    printf("Filters: %s\n",filters);

	    /* Read everything I want. Close the file and clean up */
            fits_close_file(fitsin,&fits_stat);

	    if(fits_stat && !skip_this_file){
	      LogInfo_vec[filect].error = fits_stat;
	      fprintf(proglog,"A FITSIO error has occured: %d\n",fits_stat);
	    }

	    if(!skip_this_file){
	      fprintf(proglog,"Finished with %s\n",cur.exposure);
	      filect++;
	    }


	  }  /* End of if(cur.ext=="fits") */
	  else printf("Extension is %s\n",cur.ext);

	}  /* End of `this is a valid filename' */ 
      } /* End of `this is not . or .. */

    } /* End of while(readdir)) for incoming directory */
    closedir(pwd);
  }

  fprintf(proglog,"%5d files successfully read into log\n",filect);
  fprintf(proglog,"%5d bad files not read\n",badfilect);


  free(data_indices);
  free(LogInfo_vec);

  if(outlog)
    fclose(outlog);

  fclose(proglog);

  
} 





int check_header_flag(char keyword[9], char* task, FILE* proglog, fitsfile* fitsin)
{
  int fits_stat,status;
  char comment[FC];

  printf("%s : ",task);
  ffgky(fitsin,TINT,keyword,&status,comment,&fits_stat);
  /* If FITS keyword does not exist, this file has not been reduced */
  if(fits_stat==VALUE_UNDEFINED){
    printf("File has not been reduced\n");
    fprintf(proglog,"File has not been reduced\n");
    fits_stat = 0;
  }
  else {
    switch(status){
      case 1:
        printf("Done\n");
        fprintf(proglog,"Done\n");
        break;
      case -1:
        printf("Not requested\n");
        fprintf(proglog,"Not requested\n");
        break;
      case 0:
        printf("Pending. This seems improbable. Is it an error?\n");
        fprintf(proglog,"Pending. This seems improbable. Is it an error?\n");
        break;
      default:
        if(status>0){
          printf("Non-");
          fprintf(proglog, "Non-");
        }
        printf("Critical error during operation : %d",status);
        fprintf(proglog,"Critical error during operation : %d",status); 
        break;
    }
  }

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
  printf("autolog <DIR name> \n");
  printf("<DIR name> is string giving path to directory containing the data files.\n");
  printf("\tOutput and any error logs wilbe written to the same directory\n");
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

  sprintf(to_init->object,"            ");
  sprintf(to_init->exposure,"                          ");
  sprintf(to_init->ra,"             ");
  sprintf(to_init->dec,"             ");
  sprintf(to_init->utstart,"            ");
  to_init->mjd = 0;
  to_init->airmass = 0;
  sprintf(to_init->instrume,"        ");
  sprintf(to_init->obstype,"         ");
  to_init->exptime = 0;
  sprintf(to_init->grating,"    ");
  sprintf(to_init->filter,"                        ");
  to_init->l1seeing = 0;
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


