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
      "minixpath [ hostpath ]\n", name);
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

int main(int argc, char* argv[]){
   struct cmdlineinput cli;
   struct disk disk;
   struct inode ino;
   superblock sb;
   int inodeNum;
   char error = 0;
   FILE* dest;

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
      if((ino.mode & 0170000) != 0040000){
         fprintf(stderr, "%s\n", "Invalid file");
         error++;
      }
      else{
         if(cli.dstpath){
            if((dest = fopen(cli.dstpath, "w") == NULL)){
               fprintf(stderr, "%s\n", "Problem writing file");
               exit(EXIT_FAILURE);
            }
         }
         else{
            dest = stdout;
         }
         copy_file(&sb, inodeNum, dest)
      }
   }
   return error;
}
