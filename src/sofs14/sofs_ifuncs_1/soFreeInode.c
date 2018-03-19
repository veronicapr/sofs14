/**
 *  \file soFreeInode.c (implementation file)
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
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/**
 *  \brief Free the referenced inode.
 *
 *  The inode must be in use, belong to one of the legal file types and have no directory entries associated with it
 *  (refcount = 0).
 *  The inode is marked free in the dirty state and inserted in the list of free inodes.
 *
 *  Notice that the inode 0, supposed to belong to the file system root directory, can not be freed.
 *
 *  The only affected fields are:
 *     \li the free flag of mode field, which is set
 *     \li the <em>time of last file modification</em> and <em>time of last file access</em> fields, which change their
 *         meaning: they are replaced by the <em>prev</em> and <em>next</em> pointers in the double-linked list of free
 *         inodes.
 * *
 *  \param nInode number of the inode to be freed
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */
 
int soFreeInode (uint32_t nInode)
{
   soColorProbe (612, "07;31", "soFreeInode (%"PRIu32")\n", nInode);
 
    SOSuperBlock *sb;
    SOInode *in;
 
    uint32_t p_Blk; //Numero do bloco do inode
    uint32_t p_offset;  //offset do inode
 
    int stat;
     
 
    if((stat = soLoadSuperBlock()) != 0)
      return stat;
 
 
    sb = soGetSuperBlock();
 
    if((nInode == 0) || (nInode >= (sb->iTotal)))
        return -EINVAL;
 
    //Verificar a inode table
    if((stat = soQCheckInT(sb)))
      return stat;
 
    //Obter o indice do bloco e offset do nInode.
    if((stat = soConvertRefInT(nInode, &p_Blk, &p_offset) != 0))
      return 0;
 
    //Obter o bloco onde se localiza o nInode
    if((stat = soLoadBlockInT(p_Blk)) != 0)
        return stat;
 
    in = soGetBlockInT() + p_offset;
 
    //Verificar se inode esta em uso
    if((stat = soQCheckInodeIU(sb, in)) != 0)
        return stat;

    /* ver se o inode pertence a um tipo valido de ficheiro */
    if((in->mode & INODE_TYPE_MASK) != INODE_DIR)
      if((in->mode & INODE_TYPE_MASK) != INODE_FILE)
        if((in->mode & INODE_TYPE_MASK) != INODE_SYMLINK)
          return -EIUININVAL;
 
    //O campo refcount tem de estar a 0
    if(in->refCount != 0)
        return -EINVAL;
 
    //Alterar o mode do inode
    in->mode = (in->mode | INODE_FREE);
 
    // No caso de existirem 0 inodes livres
    if(sb->iFree == 0){
        //O previous e o next serão nulos
        in->vD2.prev = NULL_INODE;
        in->vD1.next = NULL_INODE;
 
        if((stat = soStoreBlockInT()) != 0)
            return stat;
 
        //Head e Tail da Inoode list apontam para nulo
        sb->iHead = nInode;
        sb->iTail = nInode;
    }
    else{   //caso a lista de inodes livres tenha um ou mais elementos (diferente de 0)
        //o previous será a causa da de sb e o next será nulo
        in->vD2.prev = sb->iTail;
        in->vD1.next = NULL_INODE;
 
        if((stat = soStoreBlockInT()) != 0)
            return stat;
 
        //obter o indice do bloco e offset da iTail
        if((stat = soConvertRefInT(sb->iTail, &p_Blk, &p_offset)) != 0)
            return stat;
 
        //Obter o bloco da tail
        if((stat = soLoadBlockInT(p_Blk)) != 0)
            return stat;
 
        in = soGetBlockInT() + p_offset;
 
        //o campo next ainda corresponde à tail
        in->vD1.next = nInode;
 
        if((stat = soStoreBlockInT()) != 0)
            return stat;
 
         
        //alteração do inode correspondente à itail
        sb->iTail =nInode;
         
    }
    //Incrementar o numero de free inodes
    sb->iFree++;
 
    return soStoreSuperBlock();
}
