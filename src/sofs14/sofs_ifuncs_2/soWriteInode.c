/**
 *  \file soWriteInode.c (implementation file)
 *
 *  \author
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "sofs_probe.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/**
 *  \brief Write specific inode data to the table of inodes.
 *
 *  The inode must be in use and belong to one of the legal file types.
 *  Upon writing, the <em>time of last file modification</em> and <em>time of last file access</em> fields are set to
 *  current time, if the inode is in use.
 *
 *  \param p_inode pointer to the buffer containing the data to be written from
 *  \param nInode number of the inode to be written into
 *  \param status inode status (in use / free in the dirty state)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>buffer pointer</em> is \c NULL or the <em>inode number</em> is out of range or the
 *                      inode status is invalid
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soWriteInode (SOInode *p_inode, uint32_t nInode, uint32_t status)
{
  soColorProbe (512, "07;31", "soWriteInode (%p, %"PRIu32", %"PRIu32")\n", p_inode, nInode, status);

  /* insert your code here */
  int stat;
  SOSuperBlock *p_sb;
  uint32_t offset, Blk; //offset e numero do bloco do inode
  SOInode *po_inode;

  if((stat = soLoadSuperBlock()) != 0)
  	return stat;

  p_sb = soGetSuperBlock();

  //Validação do nInode
  if(p_inode == NULL)
  	return -EINVAL;
  if(nInode >= p_sb->iTotal) 
  	return -EINVAL;
  // Verificar se está em uso ou sujo
  if(status != IUIN && status != FDIN)
  	return -EINVAL;

  //Verificar o estado do inode
  if(status == IUIN){
	if((stat = soQCheckInodeIU(p_sb, p_inode))!= 0)
	return stat;
  }
  else{
	if((stat = soQCheckFDInode(p_sb, p_inode)) != 0)
	return stat;
  }
  //Identificação do bloco e offset do inode
  if((stat = soConvertRefInT(nInode, &Blk, &offset)) != 0)
  	return stat;
  // Load do bloco
  if((stat = soLoadBlockInT(Blk)) != 0)
  	return stat;

  po_inode = soGetBlockInT() + offset;

  //Copiar toda a informação do ponteiro
  po_inode->mode = p_inode->mode;
  po_inode->refCount = p_inode->refCount;
  po_inode->owner = p_inode->owner;
  po_inode->group = p_inode->group;
  po_inode->size = p_inode->size;
  po_inode->cluCount = p_inode->cluCount;
  po_inode->vD1.next = p_inode->vD1.next;
  po_inode->vD2.prev = p_inode->vD2.prev;

  int i;
  //é necessário percorrer o d, pois trata-se de um array
  for(i = 0; i < N_DIRECT; i++){
  	po_inode->d[i] = p_inode->d[i];
  }

  po_inode->i1 = p_inode->i1;
  po_inode->i2 = p_inode->i2;

  if(status == IUIN){
  	po_inode->vD1.aTime = time(NULL);
  	po_inode->vD2.mTime = po_inode->vD1.aTime;
  }
  return soStoreBlockInT();
}
