/**
 *  \file soReadFileCluster.c (implementation file)
 *
 *  \author
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
 
#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#include "sofs_ifuncs_1.h"
#include "sofs_ifuncs_2.h"

/** \brief operation get the physical number of the referenced data cluster */
#define GET         0
/** \brief operation allocate a new data cluster and associate it to the inode which describes the file */
#define ALLOC       1
/** \brief operation free the referenced data cluster */
#define FREE        2
/** \brief operation free the referenced data cluster and dissociate it from the inode which describes the file */
#define FREE_CLEAN  3
/** \brief operation dissociate the referenced data cluster from the inode which describes the file */
#define CLEAN       4

/* allusion to internal function */

int soHandleFileCluster (uint32_t nInode, uint32_t clustInd, uint32_t op, uint32_t *p_outVal);

/**
 *  \brief Read a specific data cluster.
 *
 *  Data is read from a specific data cluster which is supposed to belong to an inode associated to a file (a regular
 *  file, a directory or a symbolic link). Thus, the inode must be in use and belong to one of the legal file types.
 *
 *  If the cluster has not been allocated yet, the returned data will consist of a cluster whose byte stream contents
 *  is filled with the character null (ascii code 0).
 *
 *  \param nInode number of the inode associated to the file
 *  \param clustInd index to the list of direct references belonging to the inode where data is to be read from
 *  \param buff pointer to the buffer where data must be read into
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> or the <em>index to the list of direct references</em> are out of
 *                      range or the <em>pointer to the buffer area</em> is \c NULL
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soReadFileCluster (uint32_t nInode, uint32_t clustInd, SODataClust *buff)
{
  soColorProbe (411, "07;31", "soReadFileCluster (%"PRIu32", %"PRIu32", %p)\n", nInode, clustInd, buff);

  int stat;
  SOInode *p_inode; 
  uint32_t clustLogicNumber;
  uint32_t offset, nBlk;
  SOSuperBlock *p_sb;
  

    /* Precisamos da informaçao contida no superbloco para clacular o endereço
   * fisico do cluster e para a verificaçao de consistencia
   */
   if((stat = soLoadSuperBlock()) != 0)
    return stat;
  
   p_sb = soGetSuperBlock();
  /* clustInd deve estar no intervalo entre 0 e MAX_FILE_CLUSTER-1 */ 
   /* buff nao pode ser Null */
   /* nInode nao pode ser maior que iTotal */
  if (clustInd >= MAX_FILE_CLUSTERS || buff == NULL || nInode >= p_sb->iTotal)
  return -EINVAL;

   /* Obter numero logico do cluster de dados */         
   if ((stat = soHandleFileCluster (nInode, clustInd, GET, &clustLogicNumber)) != 0)
    return stat;


  /*Carregamento do bloco do numero do inode passado como argumento da função*/ 
  if((stat = soConvertRefInT(nInode, &nBlk, &offset)) != 0) 
    return stat;
  if((stat = soLoadBlockInT(nBlk)) != 0) 
    return stat;

  p_inode = soGetBlockInT() + offset;

  if((stat = soQCheckInodeIU(p_sb,p_inode)) != 0) 
    return stat;

  /* ver se o inode pertence a um tipo valido de ficheiro */
  if(((p_inode->mode & INODE_TYPE_MASK) != INODE_DIR)&&((p_inode->mode & INODE_TYPE_MASK) != INODE_FILE)&&(p_inode->mode & INODE_TYPE_MASK) != INODE_SYMLINK)
    return -EIUININVAL;

  if(clustLogicNumber != NULL_CLUSTER){
    if ((stat = soReadCacheCluster (p_sb->dZoneStart + clustLogicNumber * BLOCKS_PER_CLUSTER, buff)) != 0) 
      return stat;
  
  }else{    
    memset(buff, 0, sizeof(SODataClust));
  }	

    if((stat = soStoreSuperBlock()) != 0) 
      return stat;

  return 0;
}
