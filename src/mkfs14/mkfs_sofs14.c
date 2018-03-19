/**
 *  \file mkfs_sofs14.c (implementation file)
 *
 *  \brief The SOFS14 formatting tool.
 *
 *  It stores in predefined blocks of the storage device the file system metadata. With it, the storage device may be
 *  envisaged operationally as an implementation of SOFS14.
 *
 *  The following data structures are created and initialized:
 *     \li the superblock
 *     \li the table of inodes
 *     \li the data zone
 *     \li the contents of the root directory seen as empty.
 *
 *  SINOPSIS:
 *  <P><PRE>                mkfs_sofs14 [OPTIONS] supp-file
 *
 *                OPTIONS:
 *                 -n name --- set volume name (default: "SOFS14")
 *                 -i num  --- set number of inodes (default: N/8, where N = number of blocks)
 *                 -z      --- set zero mode (default: not zero)
 *                 -q      --- set quiet mode (default: not quiet)
 *                 -h      --- print this help.</PRE>
 *
 *  \author Artur Carneiro Pereira - September 2008
 *  \author Miguel Oliveira e Silva - September 2009
 *  \author António Rui Borges - September 2010 - August 2011, September 2014
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "sofs_const.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_direntry.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/* Allusion to internal functions */

static int fillInSuperBlock (SOSuperBlock *p_sb, uint32_t ntotal, uint32_t itotal, uint32_t nclusttotal,
                             unsigned char *name);
static int fillInINT (SOSuperBlock *p_sb);
static int fillInRootDir (SOSuperBlock *p_sb);
static int fillInGenRep (SOSuperBlock *p_sb, int zero);
static int checkFSConsist (void);
static void printUsage (char *cmd_name);
static void printError (int errcode, char *cmd_name);

/* The main function */

int main (int argc, char *argv[])
{
  char *name = "SOFS14";                         /* volume name */
  uint32_t itotal = 0;                           /* total number of inodes, if kept set value automatically */
  int quiet = 0;                                 /* quiet mode, if kept set not quiet mode */
  int zero = 0;                                  /* zero mode, if kept set not zero mode */

  /* process command line options */

  int opt;                                       /* selected option */

  do
  { switch ((opt = getopt (argc, argv, "n:i:qzh")))
    { case 'n': /* volume name */
                name = optarg;
                break;
      case 'i': /* total number of inodes */
                if (atoi (optarg) < 0)
                   { fprintf (stderr, "%s: Negative inodes number.\n", basename (argv[0]));
                     printUsage (basename (argv[0]));
                     return EXIT_FAILURE;
                   }
                itotal = (uint32_t) atoi (optarg);
                break;
      case 'q': /* quiet mode */
                quiet = 1;                       /* set quiet mode for processing: no messages are issued */
                break;
      case 'z': /* zero mode */
                zero = 1;                        /* set zero mode for processing: the information content of all free
                                                    data clusters are set to zero */
                break;
      case 'h': /* help mode */
                printUsage (basename (argv[0]));
                return EXIT_SUCCESS;
      case -1:  break;
      default:  fprintf (stderr, "%s: Wrong option.\n", basename (argv[0]));
                printUsage (basename (argv[0]));
                return EXIT_FAILURE;
    }
  } while (opt != -1);
  if ((argc - optind) != 1)                      /* check existence of mandatory argument: storage device name */
     { fprintf (stderr, "%s: Wrong number of mandatory arguments.\n", basename (argv[0]));
       printUsage (basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* check for storage device conformity */

  char *devname;                                 /* path to the storage device in the Linux file system */
  struct stat st;                                /* file attributes */

  devname = argv[optind];
  if (stat (devname, &st) == -1)                 /* get file attributes */
     { printError (-errno, basename (argv[0]));
       return EXIT_FAILURE;
     }
  if (st.st_size % BLOCK_SIZE != 0)              /* check file size: the storage device must have a size in bytes
                                                    multiple of block size */
     { fprintf (stderr, "%s: Bad size of support file.\n", basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* evaluating the file system architecture parameters
   * full occupation of the storage device when seen as an array of blocks supposes the equation bellow
   *
   *    NTBlk = 1 + NBlkTIN + NTClt*BLOCKS_PER_CLUSTER
   *
   *    where NTBlk means total number of blocks
   *          NTClt means total number of clusters of the data zone
   *          NBlkTIN means total number of blocks required to store the inode table
   *          BLOCKS_PER_CLUSTER means number of blocks which fit in a cluster
   *
   * has integer solutions
   * this is not always true, so a final adjustment may be made to the parameter NBlkTIN to warrant this
   */

  uint32_t ntotal;                               /* total number of blocks */
  uint32_t iblktotal;                            /* number of blocks of the inode table */
  uint32_t nclusttotal;                          /* total number of clusters */

  ntotal = st.st_size / BLOCK_SIZE;
  if (itotal == 0) itotal = ntotal >> 3;
  if ((itotal % IPB) == 0)
     iblktotal = itotal / IPB;
     else iblktotal = itotal / IPB + 1;
  nclusttotal = (ntotal - 1 - iblktotal) / BLOCKS_PER_CLUSTER;
                                                 /* final adjustment */
  iblktotal = ntotal - 1 - nclusttotal * BLOCKS_PER_CLUSTER;
  itotal = iblktotal * IPB;

  /* formatting of the storage device is going to start */

  SOSuperBlock *p_sb;                            /* pointer to the superblock */
  int status;                                    /* status of operation */

  if (!quiet)
     printf("\e[34mInstalling a %"PRIu32"-inodes SOFS11 file system in %s.\e[0m\n", itotal, argv[optind]);

  /* open a buffered communication channel with the storage device */

  if ((status = soOpenBufferCache (argv[optind], BUF)) != 0)
     { printError (status, basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* read the contents of the superblock to the internal storage area
   * this operation only serves at present time to get a pointer to the superblock storage area in main memory
   */

  if ((status = soLoadSuperBlock ()) != 0)
     return status;
  p_sb = soGetSuperBlock ();

  /* filling in the superblock fields:
   *   magic number should be set presently to 0xFFFF, this enables that if something goes wrong during formating, the
   *   device can never be mounted later on
   */

  if (!quiet)
     { printf ("Filling in the superblock fields ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInSuperBlock (p_sb, ntotal, itotal, nclusttotal, (unsigned char *) name)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* filling in the inode table:
   *   only inode 0 is in use (it describes the root directory)
   */

  if (!quiet)
     { printf ("Filling in the inode table ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInINT (p_sb)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* filling in the contents of the root directory:
   *   the first 2 entries are filled in with "." and ".." references
   *   the other entries are kept empty
   */

  if (!quiet)
     { printf ("Filling in the contents of the root directory ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInRootDir (p_sb)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /*
   * create the general repository of free data clusters as a double-linked list where the data clusters themselves are
   * used as nodes
   * zero fill the remaining data clusters if full formating was required:
   *   zero mode was selected
   */

  if (!quiet)
     { printf ("Creating the general repository of free data clusters ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInGenRep (p_sb, zero)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* magic number should now be set to the right value before writing the contents of the superblock to the storage
     device */

  p_sb->magic = MAGIC_NUMBER;
  if ((status = soStoreSuperBlock ()) != 0)
     return status;

  /* check the consistency of the file system metadata */

  if (!quiet)
     { printf ("Checking file system metadata... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = checkFSConsist ()) != 0)
     { fprintf(stderr, "error # %d - %s\n", -status, soGetErrorMessage (p_sb, -status));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* close the unbuffered communication channel with the storage device */

  if ((status = soCloseBufferCache ()) != 0)
     { printError (status, basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* that's all */

  if (!quiet) printf ("Formating concluded.\n");

  return EXIT_SUCCESS;

} /* end of main */

/*
 * print help message
 */

static void printUsage (char *cmd_name)
{
  printf ("Sinopsis: %s [OPTIONS] supp-file\n"
          "  OPTIONS:\n"
          "  -n name --- set volume name (default: \"SOFS14\")\n"
          "  -i num  --- set number of inodes (default: N/8, where N = number of blocks)\n"
          "  -z      --- set zero mode (default: not zero)\n"
          "  -q      --- set quiet mode (default: not quiet)\n"
          "  -h      --- print this help\n", cmd_name);
}

/*
 * print error message
 */

static void printError (int errcode, char *cmd_name)
{
  fprintf(stderr, "%s: error #%d - %s\n", cmd_name, -errcode,
          soGetErrorMessage (soGetSuperBlock (),-errcode));
}

  /* filling in the superblock fields:
   *   magic number should be set presently to 0xFFFF, this enables that if something goes wrong during formating, the
   *   device can never be mounted later on
   */

static int fillInSuperBlock (SOSuperBlock *p_sb, uint32_t ntotal, uint32_t itotal, uint32_t nclusttotal, unsigned char *name){

  /* inicialização do superbloco */
  
  /*Cabeçalho*/
  p_sb->magic = 0xFFFF;             /*campo magic*/
  p_sb->version = VERSION_NUMBER;   /*campo version*/

  strncpy((char*)p_sb->name, (char*)name, PARTITION_NAME_SIZE + 1);          /* campo name */  
  p_sb->name[PARTITION_NAME_SIZE] = '\0'; 

  p_sb->nTotal = ntotal;            /*campo nTotal , tamanho em nr total de blocos*/
  p_sb->mStat = PRU;               /*campo nStat , ultima vez que foi montado!
                                    *  constant signaling the file system was properly unmounted the last time it was mounted*/

  /*Tabela de nós-i*/
  p_sb->iTableStart = 1;            /*physical number of the block where the table of inodes starts! bloco 1 para o inicio da tabela de inodes */
  p_sb->iTableSize = itotal / IPB;  /* (tamanho) nr de blocos = nr de nos total / numero de nos por bloco */ 
  p_sb->iTotal = itotal;            /*nr de elementos*/

  p_sb->iFree = itotal -1;          /*nr de nós-i presentes livres; apos a formatacao todos os nos são livres!*/
  p_sb->iHead = 1;                  /*pontos de retirada; indice incial do array */
  p_sb->iTail = itotal -1;          /*pontos de inserção de valores; indice final do array */

  /*zona de dados*/
  p_sb->dZoneStart = 1 + p_sb->iTableSize; // onde começa a zona de dados depois da tabela de i-nodes
  p_sb->dZoneTotal = nclusttotal; //tamanho em nr de elementos..
  p_sb->dZoneFree = nclusttotal -1; 
  p_sb->dHead = 1; 
  p_sb->dTail = nclusttotal -1; 

//como é o formatador - colocar as caches toda a null  
  int i; 
  /*cache de retirada*/     //colocar a cache toda a null 
  p_sb->dZoneRetriev.cacheIdx = DZONE_CACHE_SIZE; /* cacheIdx - index of the first filled/free array element 
                                                   * DZONE_CACHE_SIZE - tamanho cache */
  
  for (i = 0; i < DZONE_CACHE_SIZE; ++i){
    p_sb->dZoneRetriev.cache[i] = NULL_CLUSTER; /*NULL_CLUSTER:reference to a null data cluster*/
  }

  /*cache de insercao*/   
  p_sb->dZoneInsert.cacheIdx = 0;

  for (i = 0; i < DZONE_CACHE_SIZE; ++i){
    p_sb->dZoneInsert.cache[i] = NULL_CLUSTER; /*NULL_CLUSTER:reference to a null data cluster*/
  }

return 0;
}

/*
 * filling in the inode table:
 *   only inode 0 is in use (it describes the root directory)
 */

static int fillInINT (SOSuperBlock *p_sb)
{
  /* Validação (é necessário verificar o retorno) */
  if(p_sb == NULL)
    return -1;

  SOInode *soin;  
  uint32_t nblk, inode;
  int stat, i, j;

  for(nblk = 0; nblk < p_sb->iTableSize; nblk++){
    if((stat=soLoadBlockInT(nblk))){
      return stat;
    }
    soin = soGetBlockInT();

    for(i = 0; i < IPB; i++){
      soin[i].mode = INODE_FREE;
      soin[i].refCount = 0;
      soin[i].owner = 0;
      soin[i].group = 0;
      soin[i].size = 0;
      soin[i].cluCount = 0;
    
    
      /* Todos os indices do ponteiro d[] estão livres */
      for(j = 0; j < N_DIRECT; j++)
        soin[i].d[j] = NULL_CLUSTER;

      /* Atualização da variável inode, que nos dará o nó-i com que estamos a trabalhar */
      inode = IPB * nblk + i;

      /* Inicialização genérica dos inodes */

      soin[i].i1 = NULL_CLUSTER;
      soin[i].i2 = NULL_CLUSTER;
      soin[i].vD1.next = inode + 1;
      soin[i].vD2.prev = inode - 1;

      /* Casos específicos de inicialização */

      if(inode == 0){ /* Caso 0 */
        soin[i].mode = (INODE_DIR | INODE_RD_USR | INODE_WR_USR | INODE_EX_USR | INODE_RD_GRP | INODE_WR_GRP | INODE_EX_GRP| INODE_RD_OTH | INODE_WR_OTH | INODE_EX_OTH);
        soin[i].refCount = 2;
        soin[i].owner = getuid();
        soin[i].group = getgid();
        soin[i].size = DPC * sizeof(SODirEntry);
        soin[i].cluCount = 1;
        soin[i].vD1.aTime = time(NULL);
        soin[i].vD2.mTime = soin[i].vD1.aTime; 
        soin[i].d[0] = 0;
      }

      if(inode == 1){ /* Caso 1 */
          soin[i].vD1.next = inode + 1;
          soin[i].vD2.prev = NULL_INODE;
      }

      if(inode == p_sb->iTotal - 1){  /* Último nó-i */
          soin[i].vD1.next = NULL_INODE;
          soin[i].vD2.prev = inode - 1;
      }
    }

    if ((stat = soStoreBlockInT ()) != 0){
      return stat;
    }
  }
  
  return 0;
}

/*
 * filling in the contents of the root directory:
     the first 2 entries are filled in with "." and ".." references
     the other entries are empty
 */

static int fillInRootDir (SOSuperBlock *p_sb)
{
  int stat,i;
  SODataClust dc;
    
  /* Validation */
  if (p_sb == NULL)
    return -1;

  memset(&dc,0,CLUSTER_SIZE);
  /* init , "Cabeçalho"*/
  dc.stat = 0; 
  dc.prev = NULL_CLUSTER; 
  dc.next = NULL_CLUSTER; 
  
  /* First two entries filled with "." and ".." */
  /* "Corpo" */ 
  // char *strcpy(char *dest, const char *src)

  dc.info.de[0].nInode = 0; /* ninode = 0, first position*/ 
  strcpy((char*)dc.info.de[0].name ,(char*) ".");
  
  dc.info.de[1].nInode = 0;
  strcpy((char*)dc.info.de[1].name ,(char*) "..");

  /* Every other entrie is empty, node = NULL_INODE. */
  for(i = 2; i < DPC ; i++)
    dc.info.de[i].nInode = NULL_INODE;

  /*Writing cluster data to cache */
  if ((stat = soWriteCacheCluster(p_sb->dZoneStart,&dc)) != 0)
    return stat;

  return 0;  
}

  /*
   * create the general repository of free data clusters as a double-linked list where the data clusters themselves are
   * used as nodes
   * zero fill the remaining data clusters if full formating was required:
   *   zero mode was selected
   */

static int fillInGenRep (SOSuperBlock *p_sb, int zero)
{

 SODataClust newClust;
  int stat;
  int NLClt;
  uint32_t clustFirstBlock;
  
  if(p_sb == NULL){
   return -1;	/*Previous : Error , updated : -1*/
  }

  //cria novo cluster livre no estado limpo
  newClust.stat = NULL_INODE;

  //zero mode selecionado
  if(zero){
    memset(newClust.info.data, 0, BSLPC);
  }

  /* Lista bi-ligada e calculo do primeiro bloco de cada cluster de maneira a serem escritos para cache
   * Tanto o primeiro como o ultimo no da lista tem referencia nula (head e tail) */

  for(NLClt = 1, clustFirstBlock = p_sb->dZoneStart + BLOCKS_PER_CLUSTER; NLClt < p_sb->dZoneTotal; NLClt++, clustFirstBlock += BLOCKS_PER_CLUSTER ){

    /* calcular cluster anterior 
    * Se NLClt == 1, não existe cluster anterior, caso contrário, este é referenciado */
    if(NLClt == 1){
      newClust.prev = NULL_CLUSTER;
    }
    else {
      newClust.prev = NLClt - 1;
    }

    /* calcular cluster seguinte 
     * Se NLClt == p_sb->dZoneTotal - 1, então não existe cluster seguinte; caso contrário, este é referenciado */
    if(NLClt == p_sb->dZoneTotal - 1){ 
      newClust.next = NULL_CLUSTER	;
    }
    else {
      newClust.next = NLClt + 1;
    }
  
  
    stat = soWriteCacheCluster(clustFirstBlock, &newClust); //escrever bloco
  
    //verificar se houve sucesso na escrita do cluster para a cache 
    if((stat != 0)){ //se for 0 houve sucesso
      return stat;
    } 
  }
  
  return 0;
}

/*
   check the consistency of the file system metadata
 */

static int checkFSConsist (void)
{
  SOSuperBlock *p_sb;                            /* pointer to the superblock */
  SOInode *inode;                                /* pointer to the contents of a block of the inode table */
  int stat;                                      /* status of operation */

  /* read the contents of the superblock to the internal storage area and get a pointer to it */

  if ((stat = soLoadSuperBlock()) != 0) return stat;
  p_sb = soGetSuperBlock ();

  /* check superblock and related structures */

if ((stat = soQCheckSuperBlock (p_sb)) != 0) return stat;

  /* read the contents of the first block of the inode table to the internal storage area and get a pointer to it */

if ((stat = soLoadBlockInT (0)) != 0) return stat;
inode = soGetBlockInT ();

  /* check inode associated with root directory (inode 0) and the contents of the root directory */

if ((stat = soQCheckInodeIU (p_sb, &inode[0])) != 0) return stat;
if ((stat = soQCheckDirCont (p_sb, &inode[0])) != 0) return stat;

  /* everything is consistent */

  return 0;
}
