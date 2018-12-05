#include<stdio.h>
#include<string.h>
#include<stdint.h>
#include <stdlib.h>
#include<getopt.h>
#include<ctype.h>
#include<unistd.h>
#include "helpers.c"

void print__minls_cli_opts(char* name){
   fprintf(stderr, "usage: %s [ -v ] [ -p num [ -s num ] ] imagefile" 
      "[ path ]\n", name);
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "\t-p\t part       --- select partition for filesystem" 
      "(default: none)\n"); 
   fprintf(stderr, "\t-s\t sub        --- select partition for filesystem" 
      "(default: none)\n"); 
   fprintf(stderr, "\t-h\t help       --- print usage information and exit\n"); 
   fprintf(stderr, "\t-v\t verbose    --- increase verbosity level\n");
   exit(EXIT_SUCCESS);
}

void check_partition(char* end, char* argv, struct cmdlineinput* cli){
   if(*end){
      fprintf(stderr, "%s\n", "not an integer");
      print_cli_opts(argv);
   }
   else if(cli->part < 0 || cli->part > 3){
      fprintf(stderr, "%s\n", "out of range, must be 0-3");
      print_cli_opts(argv);
   }
}

int parse_cmdline(int argc, char* argv[], struct cmdlineinput* cli){
   int cmdCounter = 1;
   int c;
   char* end;
   cli->part =INACTIVE;
   cli->subpart = INACTIVE;
   cli->imagefile = NULL;
   cli->srcpath = NULL;
   cli->dstpath = NULL;

   while((c = getopt(argc, argv, "p:s:vh")) > 0){
      switch(c){
         case 'h':
            print_cli_opts(argv[0]);
            cmdCounter++;
            break;
         case 'p':
            cli->part = strtol(optarg, &end, 0);
            check_partition(end, argv[0], cli);
            cmdCounter+=2;  
            break;
         case 'v':
            verbose_mode++;
            cmdCounter++;
            break;
         case 's':
            cli->subpart = strtol(optarg, &end, 0);
            cmdCounter+=2;  
            check_partition(end, argv[0], cli);
      }
   }
   if(cli->part == INACTIVE && cli->subpart != INACTIVE){
      fprintf(stderr, "%s\n", "Must have a partition to have a subpartition");
      print_cli_opts(argv[0]);
   }
   if(cmdCounter < argc){
      cli->imagefile = argv[cmdCounter];
      cmdCounter++;
   }
   else{
      fprintf(stderr, "Something went wrong...\n");
      print_cli_opts(argv[0]);
   }
   if(cmdCounter < argc){
      cli->srcpath = argv[cmdCounter];
      cmdCounter++;
   }
   else{
      print_cli_opts(argv[0]);
   }
   if(cmdCounter < argc){
      print_cli_opts(argv[0]);
   }
   return cmdCounter;
}  

void print_cli(struct cmdlineinput* cli){
   fprintf(stderr, "\topt->part      %d\n", cli->part);
   fprintf(stderr, "\topt->subpart   %d\n", cli->subpart);
   if(cli->imagefile){
      fprintf(stderr, "\topt->imagefile %s\n", cli->imagefile);
   }
   else{
      fprintf(stderr, "\topt->imagefile (null)\n");
   }
   if(cli->srcpath){
      fprintf(stderr, "\topt->srcpath   %s\n", cli->srcpath);
   }
   else{
      fprintf(stderr, "\topt->srcpath   (null)\n");
   }
   if(cli->dstpath){
      fprintf(stderr, "\topt->dstpath   %s\n", cli->dstpath);
   }
   else{
      fprintf(stderr, "\topt->dstpath   (null)\n");
   }
}

int main(int argc, char* argv[]){
   struct cmdlineinput cli;
   struct disk disk;
   struct inode ino;
   superblock sb;
   int inodeNum;
   char error = 0;

   parse_cmdline(argc, argv, &cli);

   if(verbose_mode > 0){
      fprintf(stderr, "\nOptions:\n");
      print_cli(&cli);
   }
   if(!open_disk(cli.imagefile, "r", &cli, &disk)){
      fprintf(stderr, "%s\n", "Failed to open disk image");
      exit(EXIT_FAILURE);
   }
   if(!read_superblock(&disk, &sb)){
      fprintf(stderr, "%s\n", "This doesn't look like a MINIX filesystem");
      exit(EXIT_FAILURE);
   }
   if(verbose_mode){
      fprintf(stderr, "%s\n", "Superblock Contents:");
      print_superblock(&sb);
   }
   if(cli.srcpath == NULL){
      cli.srcpath = "/";
   }

   inodeNum = get_inode_path(&sb, cli.srcpath);
   if(!inodeNum){
      fprintf(stderr, "%s\n", "File not found");
      error++;
   }
   else{
      read_inode(&sb, inodeNum, &ino);
      if(verbose_mode){
         fprintf(stderr, "%s\n", "File inode:");
         print_inode(&ino);
      }
      if((ino.mode & 0170000) == 0040000){
         printf("%s\n", cli.srcpath);
         print_dir(&sb, inodeNum, stdout);
      }
      else{
         print_file(&sb, inodeNum, cli.srcpath, 0, stdout);
      }
   }
   return error;
}
