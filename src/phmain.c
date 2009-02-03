/*

    File: phmain.c

    Copyright (C) 1998-2009 Christophe GRENIER <grenier@cgsecurity.org>

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdarg.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>	/* unlink, ftruncate */
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <ctype.h>      /* tolower */
#ifdef HAVE_LOCALE_H
#include <locale.h>	/* setlocale */
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <errno.h>
#include "types.h"
#include "common.h"
#include "intrf.h"
#include "fnctdsk.h"
#ifdef HAVE_NCURSES
#include "intrfn.h"
#else
#include <stdio.h>
#endif
#ifdef HAVE_JPEGLIB_H
#include <jpeglib.h>
#endif
#include "dir.h"
#include "filegen.h"
#include "photorec.h"
#include "hdcache.h"
#include "ewf.h"
#include "log.h"
#include "hdaccess.h"
#include "sudo.h"
#include "phcfg.h"
#include "misc.h"
#include "ext2_dir.h"
#include "ntfs_dir.h"
#include "pdisksel.h"
extern const arch_fnct_t arch_i386;
extern const arch_fnct_t arch_mac;
extern const arch_fnct_t arch_none;
extern const arch_fnct_t arch_sun;
extern const arch_fnct_t arch_xbox;
extern file_enable_t list_file_enable[];

int main( int argc, char **argv )
{
  int i;
  int use_sudo=0;
  int help=0, version=0, verbose=0;
  int create_log=TD_LOG_NONE;
  int run_setlocale=1;
  int testdisk_mode=TESTDISK_O_RDONLY|TESTDISK_O_READAHEAD_32K;
  const char *recup_dir=NULL;
  list_disk_t *list_disk=NULL;
  list_disk_t *element_disk;
  char *cmd_device=NULL;
  char *cmd_run=NULL;
  const char *logfile="photorec.log";
#ifdef TARGET_SOLARIS
  const arch_fnct_t *arch=&arch_sun;
#elif defined __APPLE__
  const arch_fnct_t *arch=&arch_mac;
#else
  const arch_fnct_t *arch=&arch_i386;
#endif
#ifdef HAVE_SIGACTION
  struct sigaction action, old_action;
#endif
  FILE *log_handle=NULL;
  /* random (weak is ok) is need fot GPT */
  srand(time(NULL));
#ifdef HAVE_SIGACTION
  /* set up the signal handler for SIGHUP */
  action.sa_handler  = sighup_hdlr;
  action.sa_flags = 0;
  if(sigaction(SIGHUP, &action, &old_action)==-1)
  {
    printf("Error on SIGACTION call\n");
    return -1;
  }
#endif
  printf("PhotoRec %s, Data Recovery Utility, %s\nChristophe GRENIER <grenier@cgsecurity.org>\nhttp://www.cgsecurity.org\n",VERSION,TESTDISKDATE);
  for(i=1;i<argc;i++)
  {
    if((strcmp(argv[i],"/logname")==0) ||(strcmp(argv[i],"-logname")==0))
    {
      if(i+2>=argc)
	help=1;
      else
	logfile=argv[++i];
    }
    else if((strcmp(argv[i],"/log")==0) ||(strcmp(argv[i],"-log")==0))
    {
      if(create_log==TD_LOG_NONE)
        create_log=TD_LOG_APPEND;
    }
    else if((strcmp(argv[i],"/debug")==0) || (strcmp(argv[i],"-debug")==0))
    {
      verbose++;
      if(create_log==TD_LOG_NONE)
        create_log=TD_LOG_APPEND;
    }
    else if(((strcmp(argv[i],"/d")==0)||(strcmp(argv[i],"-d")==0)) &&(i+1<argc))
    {
      int len=strlen(argv[i+1]);
      if(argv[i+1][len-1]=='\\' || argv[i+1][len-1]=='/')
      {
        char *new_recup_dir=(char *)MALLOC(len+strlen(DEFAULT_RECUP_DIR)+1);
        strcpy(new_recup_dir,argv[i+1]);
        strcat(new_recup_dir,DEFAULT_RECUP_DIR);
        recup_dir=new_recup_dir;	/* small memory leak */
      }
      else
        recup_dir=argv[i+1];
      i++;
    }
    else if((strcmp(argv[i],"/all")==0) || (strcmp(argv[i],"-all")==0))
      testdisk_mode|=TESTDISK_O_ALL;
    else if((strcmp(argv[i],"/direct")==0) || (strcmp(argv[i],"-direct")==0))
      testdisk_mode|=TESTDISK_O_DIRECT;
    else if((strcmp(argv[i],"/help")==0) || (strcmp(argv[i],"-help")==0) || (strcmp(argv[i],"--help")==0) ||
      (strcmp(argv[i],"/h")==0) || (strcmp(argv[i],"-h")==0))
      help=1;
    else if((strcmp(argv[i],"/version")==0) || (strcmp(argv[i],"-version")==0) || (strcmp(argv[i],"--version")==0) ||
      (strcmp(argv[i],"/v")==0) || (strcmp(argv[i],"-v")==0))
      version=1;
    else if((strcmp(argv[i],"/nosetlocale")==0) || (strcmp(argv[i],"-nosetlocale")==0))
      run_setlocale=0;
    else if(strcmp(argv[i],"/cmd")==0)
    {
      if(i+2>=argc)
        help=1;
      else
      {
        disk_t *disk_car;
        cmd_device=argv[++i];
        cmd_run=argv[++i];
        /* There is no log currently */
        disk_car=file_test_availability(cmd_device,verbose,arch,testdisk_mode);
        if(disk_car==NULL)
        {
          printf("\nUnable to open file or device %s\n",cmd_device);
          help=1;
        }
        else
          list_disk=insert_new_disk(list_disk,disk_car);
      }
    }
    else
    {
      disk_t *disk_car=file_test_availability(argv[i],verbose,arch,testdisk_mode);
      if(disk_car==NULL)
      {
        printf("\nUnable to open file or device %s\n",argv[i]);
        help=1;
      }
      else
        list_disk=insert_new_disk(list_disk,disk_car);
    }
  }
  if(version!=0)
  {
    printf("\n");
    printf("Version: %s\n", VERSION);
    printf("Compiler: %s\n", get_compiler());
    printf("ext2fs lib: %s, ntfs lib: %s, ewf lib: %s, libjpeg: ",
	td_ext2fs_version(), td_ntfs_version(), td_ewf_version());
#if defined(HAVE_LIBJPEG)
#if defined(JPEG_LIB_VERSION)
    printf("%u", JPEG_LIB_VERSION);
#else
    printf("yes");
#endif
#else
    printf("none");
#endif
    printf("\n");
    printf("OS: %s\n" , get_os());
    return 0;
  }
  if(help!=0)
  {
    printf("\nUsage: photorec [/log] [/debug] [/d recup_dir] [file.dd|file.e01|device]\n"\
	"       photorec /version\n" \
        "\n" \
        "/log          : create a photorec.log file\n" \
        "/debug        : add debug information\n" \
        "\n" \
        "PhotoRec searches various file formats (JPEG, Office...), it stores them\n" \
        "in recup_dir directory.\n" \
        "\n" \
        "If you have problems with PhotoRec or bug reports, please contact me.\n");
    return 0;
  }
  if(create_log!=TD_LOG_NONE)
    log_handle=log_open(logfile, create_log);
#ifdef HAVE_SETLOCALE
  if(run_setlocale>0)
  {
    const char *locale;
    locale = setlocale (LC_ALL, "");
    if (locale==NULL) {
      locale = setlocale (LC_ALL, NULL);
      log_error("Failed to set locale, using default '%s'.\n", locale);
    } else {
      log_info("Using locale '%s'.\n", locale);
    }
  }
#endif
  if(create_log!=TD_LOG_NONE && log_handle==NULL)
    log_handle=log_open_default(logfile, create_log);
#ifdef HAVE_NCURSES
  /* ncurses need locale for correct unicode support */
  if(start_ncurses("PhotoRec", argv[0]))
    return 1;
  {
    const char*filename=logfile;
    while(create_log!=TD_LOG_NONE && log_handle==NULL)
    {
      filename=ask_log_location(filename);
      if(filename!=NULL)
	log_handle=log_open(filename, create_log);
      else
	create_log=TD_LOG_NONE;
    }
  }
  aff_copy(stdscr);
  wmove(stdscr,5,0);
  wprintw(stdscr, "Please wait...\n");
  wrefresh(stdscr);
#endif
  if(log_handle!=NULL)
  {
    time_t my_time;
#ifdef HAVE_DUP2
    dup2(fileno(log_handle),2);
#endif
    my_time=time(NULL);
    log_info("\n\n%s",ctime(&my_time));
    log_info("Command line: PhotoRec");
    for(i=1;i<argc;i++)
      log_info(" %s", argv[i]);
    log_info("\n\n");
    log_flush();
  }
  log_info("PhotoRec %s, Data Recovery Utility, %s\nChristophe GRENIER <grenier@cgsecurity.org>\nhttp://www.cgsecurity.org\n", VERSION, TESTDISKDATE);
  log_info("OS: %s\n" , get_os());
  log_info("Compiler: %s\n", get_compiler());
  log_info("ext2fs lib: %s, ntfs lib: %s, ewf lib: %s, libjpeg: ",
      td_ext2fs_version(), td_ntfs_version(), td_ewf_version());
#if defined(HAVE_LIBJPEG)
#if defined(JPEG_LIB_VERSION)
  log_info("%u", JPEG_LIB_VERSION);
#else
  log_info("yes");
#endif
#else
  log_info("none");
#endif
  log_info("\n");
#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(DJGPP)
#else
#ifdef HAVE_GETEUID
  if(geteuid()!=0)
  {
    log_warning("User is not root!\n");
  }
#endif
#endif
  screen_buffer_reset();
  /* Scan for available device only if no device or image has been supplied in parameter */
  if(list_disk==NULL)
    list_disk=hd_parse(list_disk,verbose,arch,testdisk_mode);
  hd_update_all_geometry(list_disk,0,verbose);
  /* Activate the cache, even if photorec has its own */
  for(element_disk=list_disk;element_disk!=NULL;element_disk=element_disk->next)
  {
    element_disk->disk=new_diskcache(element_disk->disk, testdisk_mode);
  }
  /* save disk parameters to rapport */
  log_info("Hard disk list\n");
  for(element_disk=list_disk;element_disk!=NULL;element_disk=element_disk->next)
  {
    disk_t *disk=element_disk->disk;
    if(disk->model==NULL)
      log_info("%s, sector size=%u\n",
	  disk->description(disk), disk->sector_size);
    else
      log_info("%s, sector size=%u - %s\n",
	  disk->description(disk), disk->sector_size, disk->model);
  }
  log_info("\n");
  file_options_load(list_file_enable);
  use_sudo=do_curses_photorec(verbose, recup_dir, list_disk, list_file_enable, cmd_device, &cmd_run);
#ifdef HAVE_NCURSES
  end_ncurses();
#endif
  delete_list_disk(list_disk);
  log_info("PhotoRec exited normally.\n");
  if(log_close()!=0)
  {
    printf("PhotoRec: Log file corrupted!\n");
  }
  else
  {
    printf("PhotoRec exited normally.\n");
  }
#ifdef SUDO_BIN
  if(use_sudo>0)
    run_sudo(argc, argv);
#endif
  return 0;
}