HOSTTYPE = i386-linux

#LT_HOME is defined in the generic lt_environment.csh
DEVINC_DIR = ${LT_HOME}/src/include/
DEVLIB_DIR = ${LT_HOME}/bin/lib/$(HOSTTYPE)/
BINDIR = $(LT_HOME)/bin/$(HOSTTYPE)/
#CFITSIOINCDIR is set by lt_environment.csh

CC = gcc

#Optimisation flags
# Heavy
OPTFLAGS = -O3 

# Libraries to link for cFITSIO. Originally this would just have been ${FITSIOLIB} but
# some other dependecies have since been added. 
FITSIOLIB = -lcfitsio -lpthread

#Compile as shared library (Gets set in lt_environment anyway)
#CCSHAREDFLAG = -shared
#CCSTATICFLAG = -static

#Name of platform dependent time library (Gets set in lt_environment anyway)
#TIMELIB = -lrt

#CJM-esque obsessive compiler check flags
CCHECKFLAG = -ansi -pedantic -Wall

#
# Libraries and objects
#

${DEVLIB_DIR}liblt_filenames.a : lt_filenames.c lt_filenames.h
	${CC} -c ${CCSTATICFLAG} -o ${DEVLIB_DIR}liblt_filenames.a lt_filenames.c  ${CCHECKFLAG} ${OPTFLAG}


#
# Executables
#

autolog : autolog.c ${DEVLIB_DIR}liblt_filenames.a
	cc -o ${BINDIR}autolog autolog.c ${CCSTATICFLAG} ${OPTFLAGS} ${CCHECKFLAG} -I${CFITSIOINCDIR} -L${DEVLIB_DIR} -llt_filenames ${FITSIOLIB} -lm 

red_report : red_report.c ${DEVLIB_DIR}liblt_filenames.a 
	cc -o ${BINDIR}red_report red_report.c ${CCSTATICFLAG} ${OPTFLAGS} ${CCHECKFLAG} -I${CFITSIOINCDIR} -L${DEVLIB_DIR} -llt_filenames ${FITSIOLIB} -lm ${PLATFORM_LIBS} 










#
