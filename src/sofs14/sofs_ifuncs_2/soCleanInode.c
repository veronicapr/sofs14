/**
 *  \file soCleanInode.c (implementation file)
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
#define  CLEAN_INODE 
#ifdef CLEAN_INODE
#include "sofs_ifuncs_3.h"
#endif

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/* allusion to internal function */

int soReadInode (SOInode *p_inode, uint32_t nInode, uint32_t status);

/**
 *  \brief Clean an inode.
 *
 *  The inode must be free in the dirty state.
 *  The inode is supposed to be associated to a file, a directory, or a symbolic link which was previously deleted.
 *
 *  This function cleans the list of data cluster references.
 *
 *  Notice that the inode 0, supposed to belong to the file system root directory, can not be cleaned.
 *
 *  \param nInode number of the inode
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soCleanInode (uint32_t nInode)
{
  soColorProbe (513, "07;31", "soCleanInode (%"PRIu32")\n", nInode);

  SOSuperBlock *p_sb;
  SOInode inode;
  int status;

  p_sb = soGetSuperBlock();
  // erro em iTableSize 
  if (nInode == 0 || nInode > p_sb->iTotal) {
    return -EINVAL;
  }

  if ((status = soLoadSuperBlock()) != 0) {
  	return status;
  }

  /*
  * Read specific inode data from the table of inodes.
  * The inode may be either in use and belong to one of the legal file types or be free in the dirty state.
  * Upon reading, the time of last file access field is set to current time, if the inode is in use.
  *
  * FDIN: free inode in dirty state status 
  */

  if ((status = soReadInode(&inode, nInode, FDIN)) != 0)
  	return status;

  //Quick check of a free inode in the dirty state.
  if ((status = soQCheckFDInode(p_sb, &inode)) != 0)
  	return status;

  // Atualizado de acordo com o ifuncs3
  // CLEAN = 4 ? 
  if ((status = soHandleFileClusters (nInode, 0, CLEAN)) != 0)
  	return status;


   /* Valida e actualiza o bloco do inode para o dispositivo */
  if ((status = soStoreBlockInT ()) != 0)
    return status;

  // Armazenamento do superbloco
  if ((status = soStoreSuperBlock ()) != 0 )
    return status;

  return 0;
}
