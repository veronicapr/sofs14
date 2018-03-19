/**
 *  \file soAddAttDirEntry.c (implementation file)
 *
 *  \author
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>

#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_direntry.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#include "sofs_ifuncs_1.h"
#include "sofs_ifuncs_2.h"
#include "sofs_ifuncs_3.h"

/* Allusion to external functions */

int soGetDirEntryByName (uint32_t nInodeDir, const char *eName, uint32_t *p_nInodeEnt, uint32_t *p_idx);
int soCheckDirectoryEmptiness (uint32_t nInodeDir);

/** \brief operation remove a generic entry from a directory */
#define REM         0
/** \brief operation detach a generic entry from a directory */
#define DETACH      1

/**
 *  \brief Remove / detach a generic entry from a directory.
 *
 *  The entry whose name is <tt>eName</tt> is removed / detached from the directory associated with the inode whose
 *  number is <tt>nInodeDir</tt>. Thus, the inode must be in use and belong to the directory type.
 *
 *  Removal of a directory entry means exchanging the first and the last characters of the field <em>name</em>.
 *  Detachment of a directory entry means filling all the characters of the field <em>name</em> with the \c NULL
 *  character and making the field <em>nInode</em> equal to \c NULL_INODE.
 *
 *  The <tt>eName</tt> must be a <em>base name</em> and not a <em>path</em>, that is, it can not contain the
 *  character '/'. Besides there should exist an entry in the directory whose <em>name</em> field is <tt>eName</tt>.
 *
 *  Whenever the operation is removal and the type of the inode associated to the entry to be removed is of directory
 *  type, the operation can only be carried out if the directory is empty.
 *
 *  The <em>refCount</em> field of the inode associated to the entry to be removed / detached and, when required, of
 *  the inode associated to the directory are updated.
 *
 *  The file described by the inode associated to the entry to be removed / detached is only deleted from the file
 *  system if the <em>refCount</em> field becomes zero (there are no more hard links associated to it) and the operation
 *  is removal. In this case, the data clusters that store the file contents and the inode itself must be freed.
 *
 *  The process that calls the operation must have write (w) and execution (x) permissions on the directory.
 *
 *  \param nInodeDir number of the inode associated to the directory
 *  \param eName pointer to the string holding the name of the directory entry to be removed / detached
 *  \param op type of operation (REM / DETACH)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range or the pointer to the string is \c NULL or the
 *                      name string does not describe a file name or no operation of the defined class is described
 *  \return -\c ENAMETOOLONG, if the name string exceeds the maximum allowed length
 *  \return -\c ENOTDIR, if the inode type whose number is <tt>nInodeDir</tt> is not a directory
 *  \return -\c ENOENT,  if no entry with <tt>eName</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on the directory
 *  \return -\c EPERM, if the process that calls the operation has not write permission on the directory
 *  \return -\c ENOTEMPTY, if the entry with <tt>eName</tt> describes a non-empty directory
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soRemDetachDirEntry (uint32_t nInodeDir, const char *eName, uint32_t op)
{
  soColorProbe (314, "07;31", "soRemDetachDirEntry (%"PRIu32", \"%s\", %"PRIu32")\n", nInodeDir, eName, op);


    int stat,i;
    SOSuperBlock *p_sb;
    SOInode inodeEnt, inodeDir;
    SODataClust dc;
    int clustInd, entInd;
    uint32_t nInodeEnt,dirInd;
    
    
    if( (stat=soLoadSuperBlock())!=0 )
        return stat;

    p_sb=soGetSuperBlock();

    if( (op!=DETACH && op!=REM) || eName == NULL || nInodeDir > p_sb->iTotal)
        return -EINVAL; 

    if( (stat=soReadInode(&inodeDir, nInodeDir,IUIN))!=0 )
        return stat;

      //check if inodeDir is a directory
    if((inodeDir.mode & INODE_DIR) != INODE_DIR)
        return -ENOTDIR;

    if( (stat=soQCheckDirCont(p_sb, &inodeDir))!=0 )    //check if nInodeDir is a directory and its consistence
        return stat;
     //check if eName is exceeds max_name
    if(strlen(eName) > MAX_NAME)
      return -ENAMETOOLONG;
    //check execution permission
     if((stat = soAccessGranted(nInodeDir,X)) != 0)
        return -EACCES;

    //check write permission
    if((stat = soAccessGranted(nInodeDir,W)) != 0)
        return -EPERM;

    if( (stat=soGetDirEntryByName(nInodeDir, eName, &nInodeEnt, &dirInd))!=0 )
        return stat;

    if( (stat=soReadInode(&inodeEnt, nInodeEnt,IUIN))!=0 )
        return stat;
    
    //index of the cluster
    clustInd = dirInd / DPC;
  
    //position of the entry in the cluster
    entInd = dirInd % DPC;
  
    if(nInodeDir >= p_sb->iTableSize)
        return -EINVAL;

    //if the operation is REM
    if(op == REM){
        //check if is directory
        if((inodeEnt.mode & INODE_DIR) == INODE_DIR){
         //check if is empty
         if((stat = soCheckDirectoryEmptiness(nInodeEnt)) != 0)
           return stat;
        }
  
        //read the cluster of the entry we want to remove 
        if((stat = soReadFileCluster(nInodeDir,clustInd,&dc)) != 0)
          return stat;    

        //change the name of the first element with the last
        dc.info.de[entInd].name[MAX_NAME]=dc.info.de[entInd].name[0];
        dc.info.de[entInd].name[0]='\0';
    }
  
    if(op == DETACH){ //DETACH operation
        //read the cluster of the entry we want to remove
        if((stat = soReadFileCluster(nInodeDir,clustInd,&dc)) != 0)
          return stat; 

        for(i=0; i<=MAX_NAME; i++){
            //clear cluster entry name
          dc.info.de[entInd].name[i]='\0';
            //clear cluster entry inode
          dc.info.de[entInd].nInode = NULL_INODE;
        }
    }

    //write back file cluster
    if((stat = soWriteFileCluster(nInodeDir,clustInd,&dc)) != 0)
        return stat;

    //check if is directory
    if((inodeEnt.mode & INODE_DIR) == INODE_DIR){
        inodeEnt.refCount-=2;
        inodeDir.refCount--;
    }
    else
        inodeEnt.refCount--;

    //write back inodeDir
    if((stat = soWriteInode(&inodeEnt,nInodeEnt,IUIN)) != 0)
        return stat; 

    //if refCount is equal to 0 clean clusters and inode
    if((inodeEnt.refCount == 0) && op == REM){
        //free dataclusters
        if((stat = soHandleFileClusters(nInodeEnt,0,FREE)) != 0)
          return stat;

        //free inodes
        if((stat = soFreeInode(nInodeEnt)) != 0)
          return stat;
    }
  
    //write back the entry inode
    if((stat = soWriteInode(&inodeDir,nInodeDir,IUIN)) != 0)
        return stat;

  return 0;
}

