/**
 *  \file soReadInode.c (implementation file)
 *
 *  \author
 */

/* #define CLEAN_INODE */

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
#ifdef CLEAN_INODE
#include "sofs_ifuncs_3.h"
#endif

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/**
 *  \brief Read specific inode data from the table of inodes.
 *
 *  The inode may be either in use and belong to one of the legal file types or be free in the dirty state.
 *  Upon reading, the <em>time of last file access</em> field is set to current time, if the inode is in use.
 *
 *  \param p_inode pointer to the buffer where inode data must be read into
 *  \param nInode number of the inode to be read from
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

int soReadInode (SOInode *p_inode, uint32_t nInode, uint32_t status)
{
  soColorProbe (511, "07;31", "soReadInode (%p, %"PRIu32", %"PRIu32")\n", p_inode, nInode, status);

  SOSuperBlock *p_sb;
  SOInode *p_inodeTemp;
  int stat;
  uint32_t nBlk,offset;

  /*Validação dos parametros de entrada*/
  /*Validação do ponteiro para a região de armazenamento do inode tem que ser diferente de null*/
  if(p_inode == NULL) return -EINVAL;

  /*Load do superbloco*/
  if((stat = soLoadSuperBlock()) != 0 ) return stat;
  p_sb = soGetSuperBlock();

  /*Validação do numero do inode - Não pode estar fora do maximo*/
  if(nInode >= p_sb->iTotal) return -EINVAL;
  /*Validação do status de leitura - livre ou no estado sujo*/
  if(status != IUIN && status != FDIN) return -EINVAL;

 /*Carregamento do bloco do numero do inode passado como argumento da função*/	
  if((stat = soConvertRefInT(nInode, &nBlk, &offset)) != 0) return stat;
  if((stat = soLoadBlockInT(nBlk)) != 0) return stat;

  p_inodeTemp = soGetBlockInT() + offset;

 /*Validação de consistencia - O inode referenciado tem de estar em uso e associado a um tipo valido ou estar livre no estado sujo*/
  if(status == IUIN){
  	if((stat = soQCheckInodeIU(p_sb,p_inodeTemp)) != 0) return stat; 
  }else
  	if((stat = soQCheckFDInode(p_sb,p_inodeTemp)) != 0) return stat;	

/*Actualização do tempo do ultimo acesso ao ficheiro*/
  if(status == IUIN)
  	p_inodeTemp->vD1.aTime = time(NULL);

  *p_inode = *p_inodeTemp;

  /*Gravação das alterações ao bloco que contem o Inode a ser lido*/
  if((stat = soStoreBlockInT()) != 0) return stat;
  /*Gravação das alterações ao superbloco*/
  if((stat = soStoreSuperBlock()) != 0) return stat;

  return 0;
}
