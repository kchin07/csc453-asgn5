#include<stdio.h>
#include<string.h>
#include<stdint.h>
#include <stdlib.h>
#include<getopt.h>
#include<ctype.h>
#include<unistd.h>

#define SECTIONSIZE 512
#define NUMPARTS 4
#define KILOBYTE 1024
#define BYTE510 0x55
#define BYTE511 0xAA
#define SIG510 510
#define SIG511 511
#define PTABLE_OFFSET 0x1BE
#define INACTIVE -1
#define TYPEMINIX 0x81
#define DIRECT_ZONES 7
#define MINIXMAGICNUM 0x4D5A
#define MINIXMAGICNUMREV 0x5A4D
#define SIZEOFINODE 64
#define SIZEOFDIRECTORY 60

struct disk{
   long base;
   long size;
   FILE* fp;
};

struct partition{
   unsigned char bootind;    /* boot indicator 0/ACTIVE_FLAG*/
   unsigned char start_head; /* head value for first sector */
   unsigned char start_sec;  /* sector val + cyl bits for first sector */
   unsigned char start_cyl;  /* track value for first sector */
   unsigned char type;     /* system indifcator */
   unsigned char end_head;  /* head value of last sector */
   unsigned char end_sec;   /* sector val + cyl bits for last sector */
   unsigned char end_cyl;   /* track value for last sector */
   unsigned long lFirst;     /* logical first sector */
   unsigned long size;       /* size of partition in sectors */
};

struct cmdlineinput{
   int part;
   int subpart;
   int shift;
   int inodes;
   char* imagefile;
   char* srcpath;
   char* dstpath;
};

struct superblock {
   uint32_t ninodes;       /* number of inodes in this filesystem */
   uint16_t pad1;          /* make things line up properly */
   int16_t  i_blocks;      /* # of blocks used by inode bit map */
   int16_t  z_blocks;      /* # of blocks used by zone bit map */
   uint16_t firstdata;     /* number of first data zone */
   int16_t  log_zone_size; /* log2 of blocks per zone */
   int16_t  pad2;          /* make things line up again */
   uint32_t max_file;      /* maximum file size */
   uint32_t zones;         /* number of zones on disk */
   int16_t  magic;         /* magic number */
   int16_t  pad3;          /* make things line up again */
   uint16_t blocksize;     /* block size in bytes */
   uint8_t  subversion;    /* filesystem sub–version */

   struct disk* disk_ptr;
   unsigned short first_inode_block;
   unsigned int zone_size;
   unsigned long ptrs_per_zone;
   unsigned long inodes_per_block;
   char backwards;
};

typedef union {
   unsigned char buffer[KILOBYTE];
   struct superblock sb;
} superblock;

typedef struct file {
   superblock* sb;
   struct inode* ino;
} minFile;

struct inode {
   uint16_t mode; /* mode */
   uint16_t links; /* number or links */
   uint16_t uid;
   uint16_t gid;
   uint32_t size;
   int32_t  atime;
   int32_t  mtime;
   int32_t  ctime;
   uint32_t zone[DIRECT_ZONES];
   uint32_t indirect;
   uint32_t two_indirect;
   uint32_t unused;
};

struct fileentry{
   unsigned long inode;
   char name[SIZEOFDIRECTORY];
};

#define convert_cylindar_to_num(c,s) ((((s) & 0xc0) << 2) | (c))
#define convert_sector_to_num(c,s) ((s) & 0x3f)

static int verbose_mode = 0;
/*
static struct disk* sb->sb.disk_ptr;
static unsigned short sb->sb.first_inode_block;
static unsigned int sb->sb.zone_size;
static long sb->sb.ptrs_per_zone;
static unsigned long sb->sb.inodes_per_block;
static char sb->sb.backwards;
*/
void close_disk(struct disk* disk){
   if(disk && disk->fp){
      fclose(disk->fp);
   }
}

void* safe_malloc(int size) {
   void *res;
   res = malloc(size);
   if ((res == NULL)) {
      perror("malloc");
      exit(EXIT_FAILURE);
   }
   return res;
}

void* read_sector(struct disk* disk, int sector, void* buffer){
   void* returnVal;
   long offset;
   if(!disk->fp || !buffer){
        fprintf(stderr, "%s\n", "no disk fp or buffer");
        returnVal = NULL;
   }
   else{
      offset = disk->base + sector * SECTIONSIZE;
      fprintf(stderr, " offset: %lu\n", offset);
      fprintf(stderr, "disk->fp: %p\n", disk->fp);
      if(fseek(disk->fp, offset, SEEK_SET) == -1){
         perror("problem: ");
         fprintf(stderr, "%s\n", "fseek failure");
         returnVal = NULL;
      }
      else{
         if(fread(buffer, SECTIONSIZE, 1, disk->fp) != 1){
            fprintf(stderr, "%s\n", "fread failure");
            returnVal = NULL;
         }
         else{
            returnVal = buffer;
         }
      }
   }
   return returnVal;
}


void* write_sector(struct disk* disk, int sector, void* buffer){
   void* returnVal;
   long offset;
   if(!disk->fp || !buffer){
      returnVal = NULL;
   }
   else{
      offset = disk->base + sector * SECTIONSIZE;
      if(fseek(disk->fp, offset, SEEK_SET) == -1){
         returnVal = NULL;
      }
      else{
         if(fwrite(buffer, SECTIONSIZE, 1, disk->fp) != 1){
            returnVal = NULL;
         }
         else{
            returnVal = buffer;
         }
      }
   }
   return returnVal;
}

void* read_block(struct disk* disk, int block, int blockSize, void* buffer){
   void* returnVal;
   long offset;
   int result;

   if(!disk->fp || !buffer){
        returnVal = NULL;
   }
   else{
      offset = disk->base + block * blockSize;
      if(fseek(disk->fp, offset, SEEK_SET) == -1){
         returnVal = NULL;
      }
      else{
         if((result = fread(buffer, blockSize, 1, disk->fp)) != 1){
            returnVal = NULL;
         }
         else{
            returnVal = buffer;
         }
      }
   }
   return returnVal;
}

void* write_block(struct disk* disk, int block, int blockSize, void* buffer){
   void* returnVal;
   long offset;
   int result;

   if(!disk->fp || !buffer){
      returnVal = NULL;
   }
   else{
      offset = disk->base + block * blockSize;
      if(fseek(disk->fp, offset, SEEK_SET) == -1){
         returnVal = NULL;
      }
      else{
         if((result = fwrite(buffer, blockSize, 1, disk->fp)) != 1){
            returnVal = NULL;
         }
         else{
            returnVal = buffer;
         }
      }
   }
   return returnVal;
}

void print_ptable(struct partition ptable[]){
   int i;
char* headers = " boot start_head start_sec start_cyl sys_ind end_head"
                " end_sec end_cyl log_first size\n";
   fprintf(stderr," ----Start---- ------End-----\n");
   fprintf(stderr, "%s\n", headers);
   for(i=0; i < NUMPARTS; i++){
      fprintf(stderr, " 0x%02x %4u %4u %4u 0x%02x %4u %4u %4u %10lu %10lu\n",
         ptable[i].bootind,
         ptable[i].start_head,
         convert_sector_to_num(ptable[i].start_cyl, ptable[i].start_sec),
         convert_cylindar_to_num(ptable[i].start_cyl, ptable[i].start_sec),
         ptable[i].type,
         ptable[i].end_head,
         convert_sector_to_num(ptable[i].end_cyl, ptable[i].end_sec),
         convert_cylindar_to_num(ptable[i].end_cyl, ptable[i].end_sec),
         ptable[i].lFirst,
         ptable[i].size
      );
   }
}

struct partition* read_ptable(struct disk* disk, struct cmdlineinput* cli, 
                              struct partition* ptable){
   unsigned char buffer[KILOBYTE];

   if(!read_sector(disk, 0, buffer)){
      fprintf(stderr, "%s\n", "couldn't read sector");
      return NULL;
   }
   if((buffer[SIG510] != BYTE510) || (buffer[SIG511] != BYTE511)){
      fprintf(stderr, "%s\n", "Invalid partition table\n");
      return NULL;
   }
   else{
      return memcpy(ptable, buffer + PTABLE_OFFSET, 
                    NUMPARTS*sizeof(struct partition));
   }
}

static long file_size(char* fileName){
   FILE* fp;
   long returnVal;

   fp = fopen(fileName, "r");
   if(!fp){
      returnVal = 0;
   }
   else{
      if(fseek(fp, 0, SEEK_END) == -1){
         returnVal = 0;
      }
      else{
         returnVal = ftell(fp);
         if(returnVal == -1){
            returnVal = 0;
         }
         fclose(fp);
      }
   }
   return returnVal;
}

struct disk* open_disk(char* fileName, char* mode, struct cmdlineinput* cli,
   struct disk* disk){

   struct partition ptable[NUMPARTS];

   if((disk->fp = fopen(fileName, mode)) == NULL){
      fprintf(stderr, "%s\n", "Can't even open file and mode");
      return NULL;
   }
   disk->base = 0;
   disk->size = file_size(fileName);

   if(cli->part != INACTIVE){
      if(!read_ptable(disk, cli, ptable)){
         fprintf(stderr, "%s\n", "Couldn't read page table for partition");
         close_disk(disk);
         return NULL;
      }
      if(verbose_mode){
         fprintf(stderr, "\n%s\n", "Partition table:");
         print_ptable(ptable);
      }
      if(ptable[cli->part].type == TYPEMINIX){
         disk->base = ptable[cli->part].lFirst * SECTIONSIZE;
         disk->size = ptable[cli->part].size * SECTIONSIZE;
      }
      else{
         fprintf(stderr, "%s\n", "Invalid partition, not of Minix type");
         close_disk(disk);
         return NULL;
      }
      if(cli->subpart != INACTIVE){
         if(!read_ptable(disk, cli, ptable)){
            fprintf(stderr, "%s\n", "couldn't read page table for sub");
            close_disk(disk);
            return NULL;
         }
         if(verbose_mode){
            fprintf(stderr, "\n%s\n", "subpartition table:");
            print_ptable(ptable);
         }
         if(ptable[cli->subpart].type == TYPEMINIX){
            disk->base = ptable[cli->subpart].lFirst * SECTIONSIZE;
            disk->size = ptable[cli->subpart].size * SECTIONSIZE;
         }
         else{
            fprintf(stderr, "%s\n", "Invalid subpartition, not of Minix type");
            close_disk(disk);
            return NULL;
         }
      }
   }
   return disk;
}

minFile* new_minFile(superblock* sb, struct inode* ino){
   minFile* mf = safe_malloc(sizeof(struct file));
   mf->sb = sb;
   mf->ino = ino;
   return mf;
}

void* read_zone(superblock* sb, int zone, char* buffer){
   int i;
   int base;
   void* returnVal = buffer;
   if(zone){
      base = zone << sb->sb.log_zone_size;
      for(i=0; i < (1 << sb->sb.log_zone_size); i++){
         if(read_block(sb->sb.disk_ptr, base + i, sb->sb.blocksize,
            buffer + (sb->sb.blocksize * i)) == NULL){
            returnVal = NULL;
            break;
         }
      }
   }
   else{
      memset(buffer, 0, sb->sb.zone_size);
   }
   return returnVal;
}

int zone_matching(minFile* file, int numOfZones){
   unsigned long* indirectZone = safe_malloc(file->sb->sb.zone_size);
   unsigned long* doubeIndirectZone = safe_malloc(file->sb->sb.zone_size);
   int returnVal = -1;
   int indirectIndex;
   int blockIndex;

   if(numOfZones < DIRECT_ZONES){
      returnVal = file->ino->zone[numOfZones];
   }
   else{
      numOfZones -= DIRECT_ZONES;
      if(numOfZones < file->sb->sb.ptrs_per_zone){
         read_zone(file->sb, file->ino->indirect, (void*)indirectZone);
         returnVal = indirectZone[numOfZones];
      }
      else{
         numOfZones -= file->sb->sb.ptrs_per_zone;
         read_zone(file->sb, file->ino->two_indirect,
            (void*)doubeIndirectZone);
         indirectIndex = numOfZones / file->sb->sb.ptrs_per_zone;
         blockIndex = numOfZones % file->sb->sb.ptrs_per_zone;
         read_zone(file->sb, doubeIndirectZone[indirectIndex],
            (void*)indirectZone);
         returnVal = indirectZone[blockIndex];
      }
   }
   return returnVal;
}

int read_superblock(struct disk* disk, superblock* sb){
   int returnVal = 1;

   sb->sb.backwards = 1;
   if(read_block(disk, 1, KILOBYTE, sb) == NULL){
      returnVal = 0;
   }
   else if ((sb->sb.magic != MINIXMAGICNUM) &&
      sb->sb.magic != MINIXMAGICNUMREV){
      fprintf(stderr, "%s\n", "Magic number doesn't match");
      returnVal = 0;
   }
   if(returnVal){
      if(sb->sb.magic == MINIXMAGICNUM){
         sb->sb.backwards = 0;
      }
      else{
         sb->sb.backwards = 1;
         fprintf(stderr, "%s\n", "sb->sb.backwards endiness");
         returnVal = 0;
      }

      sb->sb.disk_ptr = disk;
      sb->sb.first_inode_block = 2 + sb->sb.i_blocks + sb->sb.z_blocks;
      sb->sb.zone_size = sb->sb.blocksize << sb->sb.log_zone_size;
      sb->sb.ptrs_per_zone = sb->sb.zone_size/sizeof(unsigned long);
      sb->sb.inodes_per_block = sb->sb.blocksize/sizeof(struct inode);
   }
   return returnVal;
}

void print_superblock(superblock* sb){
   fprintf(stderr, "%s\n", "Super block stored data:");
   fprintf(stderr, "\tninodes       %6lu\n", sb->sb.ninodes);
   fprintf(stderr, "\ti_blocks      %6u\n", sb->sb.i_blocks);
   fprintf(stderr, "\tz_blocks      %6u\n", sb->sb.z_blocks);
   fprintf(stderr, "\tfirstdata     %6u\n", sb->sb.firstdata);
   fprintf(stderr, "\tlog_sb->sb.zone_size %6u (zone size: %0u)\n", 
      sb->sb.log_zone_size, sb->sb.blocksize<< sb->sb.log_zone_size);
   fprintf(stderr, "\tmax_file      %10lu\n", sb->sb.max_file);
   fprintf(stderr, "\tmagic         0x%04x\n", sb->sb.magic);
   fprintf(stderr, "\tzones         %6lu\n", sb->sb.zones);
   fprintf(stderr, "\tblocksize     %6u\n", sb->sb.blocksize);
   fprintf(stderr, "\tsubversion    %6u\n", sb->sb.subversion);
   fprintf(stderr, "Other data:\n");
   fprintf(stderr, "\tsb->sb.first_inode_block  %6u\n", sb->sb.first_inode_block);
   fprintf(stderr, "\tsb->sb.zone_size          %6u\n", sb->sb.zone_size);
   fprintf(stderr, "\tsb->sb.ptrs_per_zone      %6lu\n", sb->sb.ptrs_per_zone);
   fprintf(stderr, "\tsb->sb.inodes_per_block   %6lu\n", sb->sb.inodes_per_block);
   fprintf(stderr, "\tsb->sb.backwards          %6d\n", sb->sb.backwards);
}

void* read_zone_to_static_buff(superblock* sb, int numOfZones){
   char* zone = safe_malloc(sb->sb.zone_size);
   return read_zone(sb, numOfZones, zone);
}


void* write_zone(superblock* sb, int zone, char* buffer){
   int i;
   int base = zone << sb->sb.log_zone_size;
   void* returnVal = buffer;
   for(i=0; i < (1 << sb->sb.log_zone_size); i++){
      if(write_block(sb->sb.disk_ptr, base + i, sb->sb.blocksize,
         buffer + (sb->sb.blocksize * i)) == NULL){
         returnVal = NULL;
         break;
      }
   }
   return returnVal;
}

static char* get_permissions(unsigned int mode){
   static char permissions_buffer[10];
   if((mode & 0170000) == 0040000){
      permissions_buffer[0] = 'd';
   }
   else{
      permissions_buffer[0] = '-';
   }
   if(mode & 0x100){
      permissions_buffer[1] = 'r';
   }
   else{
      permissions_buffer[1] = '-';
   }
   if(mode & 0x80){
      permissions_buffer[2] = 'w';
   }
   else{
      permissions_buffer[2] = '-';
   }
   if(mode & 0x40){
      permissions_buffer[3] = 'x';
   }
   else{
      permissions_buffer[3] = '-';
   }
   if(mode & 0x20){
      permissions_buffer[4] = 'r';
   }
   else{
      permissions_buffer[4] = '-';
   }
   if(mode & 0x10){
      permissions_buffer[5] = 'w';
   }
   else{
      permissions_buffer[5] = '-';
   }
   if(mode & 0x8){
      permissions_buffer[6] = 'x';
   }
   else{
      permissions_buffer[6] = '-';
   }
   if(mode & 0x4){
      permissions_buffer[7] = 'r';
   }
   else{
      permissions_buffer[7] = '-';
   }
   if(mode & 0x2){
      permissions_buffer[8] = 'w';
   }
   else{
      permissions_buffer[8] = '-';
   }
   if(mode & 0x1){
      permissions_buffer[9] = 'x';
   }
   else{
      permissions_buffer[9] = '-';
   }
   return permissions_buffer;
}

struct inode* read_inode(superblock* sb, int inode_num, struct inode* inode){
   struct inode* inode_table = safe_malloc(sb->sb.inodes_per_block * sizeof(inode));
   struct inode* returnVal = NULL;
   int block_num = (inode_num - 1) / sb->sb.inodes_per_block;
   int node_num = (inode_num - 1) % sb->sb.inodes_per_block;

   if(read_block(sb->sb.disk_ptr, sb->sb.first_inode_block + block_num, sb->sb.blocksize,
      inode_table) != NULL){
      *inode = inode_table[node_num];
      returnVal = inode;
   }
   return returnVal;
}

void print_inode(struct inode* inode){
   int i;
   fprintf(stderr, "%s\n", "Inode info:");
   fprintf(stderr, "\tmode     0x%04x (%s)\n", inode->mode,
          get_permissions(inode->mode));
   fprintf(stderr, "\tlinks %6u\n", inode->links);
   fprintf(stderr, "\tuid %6u\n", inode->uid);
   fprintf(stderr, "\tgid %6u\n", inode->gid);
   fprintf(stderr, "\tsize %6u\n", inode->size);
   fprintf(stderr, "\tatime %6u\n", inode->atime);
   fprintf(stderr, "\tmtime %6u\n", inode->mtime);
   fprintf(stderr, "\tctime %6u\n", inode->ctime);
   fprintf(stderr, "\tDirect Zones:\n");
   for(i=0; i < DIRECT_ZONES; i++){
      fprintf(stderr, "\t\tzone %d:   %10lu\n", i, inode->zone[i]);
   }
   fprintf(stderr, "\tindirect %6u\n", inode->indirect);
   fprintf(stderr, "\tdouble %6u\n", inode->two_indirect);
}

int copy_file(superblock* sb, int inode_num, FILE* dest){
   struct inode ino;
   int i;
   int num;
   int bytes;
   minFile* file;
   char* zone;
   int zoneNum;

   if(read_inode(sb, inode_num, &ino) != NULL){
      if((ino.mode & 170000) != 0100000){
         fprintf(stderr, "Inode is not a valid file\n" );
         return -1;
      }
      else{
         file = new_minFile(sb, &ino);
         bytes = ino.size;
         for(i=0; i < bytes; i++){
            zoneNum = zone_matching(file, i);
            if(sb->sb.zone_size < bytes){
               num = sb->sb.zone_size;
            }
            else{
               num = bytes;
            }
            if(zoneNum){
               zone = read_zone_to_static_buff(sb, zoneNum);
               fwrite(zone, 1, num, dest);   
            }
            else{
               if(fseek(dest, num, SEEK_CUR) == -1){
                  zone = read_zone_to_static_buff(sb, zoneNum);
                  fwrite(zone, 1, num, dest);
               }
            }
            bytes -= num;
         }
      }
   }
   else{
      fprintf(stderr, "%s\n", "Cannot read inode");
      return -1;
   }
   return 0;
}

void print_file(superblock* sb, int inodeNum, char* name, int limit, 
   FILE* dest){
   struct inode ino;
   read_inode(sb, inodeNum, &ino);
   if(limit){
      fprintf(dest, "%s %9lu %.*s\n", get_permissions(ino.mode),
         ino.size, SIZEOFDIRECTORY, name);
   }
   else{
      fprintf(dest, "%s %9lu %s\n", get_permissions(ino.mode),
         ino.size, name);   
   }
}

void print_dir(superblock* sb, int inodeNum, FILE* dest){
   int numOfEntries = sb->sb.zone_size / sizeof(struct fileentry);
   int bytes;
   int zone;
   int i;
   int zoneNum;
   struct fileentry* entry;
   struct inode ino;
   minFile* file;

   if(read_inode(sb, inodeNum, &ino)){
      file = new_minFile(sb, &ino);
      zone = 0;
      zoneNum = zone_matching(file, zone);
      entry = read_zone_to_static_buff(sb, zoneNum);
      for(bytes = ino.size; bytes > 0;){
         for(i=0; bytes && (i < numOfEntries); i++){
            if(entry[i].inode){
               print_file(sb, entry[i].inode, entry[i].name, SIZEOFDIRECTORY,
                  dest);
            }
            else if(verbose_mode > 1){
               fprintf(dest, "%s\n", entry[i].name);
            }
            bytes -= sizeof(struct fileentry);
         }
         if(bytes){
            zoneNum = zone_matching(file, ++zone);
            entry = read_zone_to_static_buff(sb, zoneNum);
         }
      }
   }
   else{
      fprintf(stderr, "%s\n", "Failed to read inode");
   }
}

static int find_dir(superblock* sb, int inodeNum, char* name){
   int numOfEntries = sb->sb.zone_size/sizeof(struct fileentry);
   int bytes;
   int zone;
   int i;
   int zoneNum;
   struct fileentry* entry;
   struct inode ino;
   minFile* file;


   if(read_inode(sb, inodeNum, &ino)){
      file = new_minFile(sb, &ino);
      zone = 0;
      zoneNum = zone_matching(file, zone);
      entry = read_zone_to_static_buff(sb, zoneNum);
      for(bytes = ino.size; bytes > 0;){
         for(i=0; bytes && i < numOfEntries; i++){
            if(entry[i].inode){
               if(!strncmp(name, entry[i].name, SIZEOFDIRECTORY)){
                  return entry[i].inode;
               }
               bytes -= sizeof(struct fileentry);
            }
            if(bytes){
               zoneNum = zone_matching(file, ++zone);
               entry = read_zone_to_static_buff(sb, zoneNum);
            }
         }
      }
   }
   else{
      fprintf(stderr, "%s\n", "Failed to read inode");
   }
   return 0;
}

int get_inode_path(superblock* sb, char* path){
   int inodeNum;
   char* tempPath;
   char* name;
   char* str;

   tempPath = safe_malloc(strlen(path) + 1);
   strcpy(tempPath, path);
   for(str=tempPath; *str == '/'; str++);

   name = strtok(str, "/");
   inodeNum = 1;
   while(inodeNum && name){
      inodeNum = find_dir(sb, inodeNum, name);
      name = strtok(NULL, "/");
   }
   return inodeNum;
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

void print_minget_cli_opts(char* name){
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

void print_minls_cli_opts(char* name){
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

void minls_check_partition(char* end, char* argv, struct cmdlineinput* cli){ 
   if(*end){ 
      fprintf(stderr, "%s\n", "not an integer"); 
      print_minls_cli_opts(argv); 
   } 
   else if(cli->part < 0 || cli->part > 3){ 
      fprintf(stderr, "%s\n", "out of range, must be 0-3"); 
      print_minls_cli_opts(argv); 
   } 
} 

void minget_check_partition(char* end, char* argv, struct cmdlineinput* cli){
   if(*end){
      fprintf(stderr, "%s\n", "not an integer");
      print_minget_cli_opts(argv);
   }
   else if(cli->part < 0 || cli->part > 3){
      fprintf(stderr, "%s\n", "out of range, must be 0-3");
      print_minget_cli_opts(argv);
   }
}

int minget_parse_cmdline(int argc, char* argv[], struct cmdlineinput* cli){
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
            print_minget_cli_opts(argv[0]);
            cmdCounter++;
            break;
         case 'p':
            cli->part = strtol(optarg, &end, 0);
            minget_check_partition(end, argv[0], cli);
            cmdCounter+=2;
            break;
         case 'v':
            verbose_mode++;
            cmdCounter++;
            break;
         case 's':
            cli->subpart = strtol(optarg, &end, 0);
            cmdCounter+=2;
            minget_check_partition(end, argv[0], cli);
      }
   }
   if(cli->part == INACTIVE && cli->subpart != INACTIVE){
      fprintf(stderr, "%s\n", "Must have a partition to have a subpartition");
      print_minget_cli_opts(argv[0]);
   }
   if(cmdCounter < argc){
      cli->imagefile = argv[cmdCounter];
      cmdCounter++;
   }
   else{
      fprintf(stderr, "1. Something went wrong...\n");
      print_minget_cli_opts(argv[0]);
   }
   if(cmdCounter < argc){
      cli->srcpath = argv[cmdCounter];
      cmdCounter++;
   }
   if(cmdCounter < argc){
      fprintf(stderr, "3. Something went wrong...\n");
      print_minget_cli_opts(argv[0]);
   }
   return cmdCounter;
}

int minls_parse_cmdline(int argc, char* argv[], struct cmdlineinput* cli){
   extern int optind;
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
            print_minls_cli_opts(argv[0]);
            break;
         case 'p':
            cli->part = strtol(optarg, &end, 0);
            
            if(*end){
               fprintf(stderr, "%s\n", "not an integer");
               print_minls_cli_opts(argv[0]);
            }
            else if(cli->part < 0 || cli->part > 3){
               fprintf(stderr, "%s\n", "partition out of range");
               print_minls_cli_opts(argv[0]);
            }
            break;
         case 'v':
            verbose_mode++;
            break;
         case 's':
            cli->subpart = strtol(optarg, &end, 0);
            if(*end){
               fprintf(stderr, "%s\n", "not an integer");
               print_minls_cli_opts(argv[0]);
            }
            else if(cli->part < 0 || cli->part > 3){
               fprintf(stderr, "%s\n", "sub partition out of range");
               print_minls_cli_opts(argv[0]);
            }
      }
   }
   if(cli->part == INACTIVE && cli->subpart != INACTIVE){
      fprintf(stderr, "%s\n", "Must have a partition to have a subpartition");
      print_minls_cli_opts(argv[0]);
   }
   if(optind < argc){
      cli->imagefile = argv[optind++];
   }
   else{
      fprintf(stderr, "1. Something went wrong...\n");
      print_minls_cli_opts(argv[0]);
   }
   if(optind < argc){
      cli->srcpath = argv[optind++];
   }
   if(optind != argc){
      fprintf(stderr, "3. Something went wrong...\n");
      print_minls_cli_opts(argv[0]);
   }
   return optind;
}

