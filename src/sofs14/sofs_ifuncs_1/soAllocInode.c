/**
 *  \file soAllocInode.c (implementation file)
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
#define  CLEAN_INODE
#ifdef CLEAN_INODE
#include "sofs_ifuncs_2.h"
#endif

/**
 *  \brief Allocate a free inode.
 *
 *  The inode is retrieved from the list of free inodes, marked in use, associated to the legal file type passed as
 *  a parameter and generally initialized. It must be free and if is free in the dirty state, it has to be cleaned
 *  first.
 *
 *  Upon initialization, the new inode has:
 *     \li the field mode set to the given type, while the free flag and the permissions are reset
 *     \li the owner and group fields set to current userid and groupid
 *     \li the <em>prev</em> and <em>next</em> fields, pointers in the double-linked list of free inodes, change their
 *         meaning: they are replaced by the <em>time of last file modification</em> and <em>time of last file
 *         access</em> which are set to current time
 *     \li the reference fields set to NULL_CLUSTER
 *     \li all other fields reset.

 *  \param type the inode type (it must represent either a file, or a directory, or a symbolic link)
 *  \param p_nInode pointer to the location where the number of the just allocated inode is to be stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>type</em> is illegal or the <em>pointer to inode number</em> is \c NULL
 *  \return -\c ENOSPC, if the list of free inodes is empty
 *  \return -\c EFININVAL, if the free inode is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAllocInode (uint32_t type, uint32_t* p_nInode)
{
   soColorProbe (611, "07;31", "soAllocInode (%"PRIu32", %p)\n", type, p_nInode);
   SOSuperBlock *p_sb; //ponteiro para o superbloco
   int stat,i;
   SOInode *inode;
   uint32_t nBlk,offset;
  
   /*Validação dos parametros de entrada */
   if (p_nInode == NULL || (type != INODE_DIR && type != INODE_FILE && type != INODE_SYMLINK))
        return -EINVAL;

   /*Load do superbloco*/
   if((stat = soLoadSuperBlock()) != 0) return stat;  
   p_sb = soGetSuperBlock();

   /*Verificação do superbloco e da tabela de Inodes*/
   if ((stat = soQCheckSuperBlock (p_sb)) !=0) return stat;
   if ((stat = soQCheckInT (p_sb)) != 0) return stat; 

   /*Se a lista de nós-i livres estiver vazia*/
   if(p_sb->iFree == 0) return -ENOSPC;

    /*Colocar no ponteiro passado como parametro da função, o numero do inode a reservar*/ 
    *p_nInode = p_sb->iHead;

    /*calculo do bloco em que está o primeiro inode livre e o seu offset*/
    if ((stat = soConvertRefInT(p_sb->iHead, &nBlk, &offset)) != 0) return stat;
    if ((stat = soLoadBlockInT(nBlk)) != 0) return stat;


    inode = soGetBlockInT() + offset; /*inode aponta para o inode a reservar*/
 
    /* check if the inode is dirty */
    if ((stat = soQCheckFCInode (inode)) != 0)
    { /* it is, clean it */
      if ((stat = soCleanInode(*p_nInode)) != 0)
        return stat;
      if ((stat = soLoadBlockInT (nBlk)) != 0)
        return stat;
      inode = soGetBlockInT() + offset;
    }


     
     p_sb->iHead = inode->vD1.next;//?
     //-------------------------------
    
    /*Alteração dos parametros do inode a reservar*/
    inode->mode = type;        
    inode->owner = getuid();
    inode->group = getgid();
    inode->vD1.aTime = time(NULL);
    inode->vD2.mTime = inode->vD1.aTime;
    inode->refCount = 0;
    inode->size = 0; 
    inode->cluCount = 0;
    inode->i1 = NULL_CLUSTER;
    inode->i2 = NULL_CLUSTER;

    /*Preenchimento da tabela de referencias para cada data cluster. Inodes livres logo ponteiro = NULL_CLUSTER*/
    for(i=0; i < N_DIRECT; i++)
      inode -> d[i] = NULL_CLUSTER;

   /*Gravar bloco de inodes onde se fez alterações*/
    if((stat = soStoreBlockInT()) != 0) return stat;

    /*Actualização do iHead da lista de nós-i livres*/  
    if (p_sb-> iFree == 1)
        p_sb->iHead = p_sb->iTail = NULL_INODE;
    else{
         if((stat = soConvertRefInT(p_sb->iHead, &nBlk, &offset)) != 0) return stat;
         if((stat = soLoadBlockInT(nBlk)) != 0) return stat;
         inode = soGetBlockInT() + offset;

         inode->vD2.prev = NULL_INODE; /*colocar a null o .prev do Head*/
         
         if((stat = soStoreBlockInT()) != 0) return stat;
    }
    /*Menos um  elemento livre na lista*/
    p_sb->iFree -= 1; 

   //-------------------------------------  

   /*Gravar alterações no superbloco*/
    if((stat= soStoreSuperBlock()) != 0) return stat;

   return 0;
}