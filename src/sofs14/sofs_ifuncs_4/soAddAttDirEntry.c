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

/* Allusion to external function */

int soGetDirEntryByName (uint32_t nInodeDir, const char *eName, uint32_t *p_nInodeEnt, uint32_t *p_idx);

/** \brief operation add a generic entry to a directory */
#define ADD         0
/** \brief operation attach an entry to a directory to a directory */
#define ATTACH      1

/**
 *  \brief Add a generic entry / attach an entry to a directory to a directory.
 *
 *  In the first case, a generic entry whose name is <tt>eName</tt> and whose inode number is <tt>nInodeEnt</tt> is added
 *  to the directory associated with the inode whose number is <tt>nInodeDir</tt>. Thus, both inodes must be in use and
 *  belong to a legal type, the former, and to the directory type, the latter.
 *
 *  Whenever the type of the inode associated to the entry to be added is of directory type, the directory is initialized
 *  by setting its contents to represent an empty directory.
 *
 *  In the second case, an entry to a directory whose name is <tt>eName</tt> and whose inode number is <tt>nInodeEnt</tt>
 *  is attached to the directory, the so called <em>base directory</em>, associated to the inode whose number is
 *  <tt>nInodeDir</tt>. The entry to be attached is supposed to represent itself a fully organized directory, the so
 *  called <em>subsidiary directory</em>. Thus, both inodes must be in use and belong to the directory type.
 *
 *  The <tt>eName</tt> must be a <em>base name</em> and not a <em>path</em>, that is, it can not contain the
 *  character '/'. Besides there should not already be any entry in the directory whose <em>name</em> field is
 *  <tt>eName</tt>.
 *
 *  The <em>refcount</em> field of the inode associated to the entry to be added / updated and, when required, of the
 *  inode associated to the directory are updated. This may also happen to the <em>size</em> field of either or both
 *  inodes.
 *
 *  The process that calls the operation must have write (w) and execution (x) permissions on the directory.
 *
 *  \param nInodeDir number of the inode associated to the directory
 *  \param eName pointer to the string holding the name of the entry to be added / attached
 *  \param nInodeEnt number of the inode associated to the entry to be added / attached
 *  \param op type of operation (ADD / ATTACH)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if any of the <em>inode numbers</em> are out of range or the pointer to the string is \c NULL
 *                      or the name string does not describe a file name or no operation of the defined class is described
 *  \return -\c ENAMETOOLONG, if the name string exceeds the maximum allowed length
 *  \return -\c ENOTDIR, if the inode type whose number is <tt>nInodeDir</tt> (ADD), or both the inode types (ATTACH),
 *                       are not directories
 *  \return -\c EEXIST, if an entry with the <tt>eName</tt> already exists
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on the directory where the
 *                      entry is to be added / attached
 *  \return -\c EPERM, if the process that calls the operation has not write permission on the directory where the entry
 *                     is to be added / attached
 *  \return -\c EMLINK, if the maximum number of hardlinks in either one of inodes has already been attained
 *  \return -\c EFBIG, if the directory where the entry is to be added / attached, has already grown to its maximum size
 *  \return -\c ENOSPC, if there are no free data clusters
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCMINVAL, if the mapping association of the data cluster is invalid
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAddAttDirEntry (uint32_t nInodeDir, const char *eName, uint32_t nInodeEnt, uint32_t op){
    soColorProbe (313, "07;31", "soAddAttDirEntry (%"PRIu32", \"%s\", %"PRIu32", %"PRIu32")\n", nInodeDir, eName, nInodeEnt, op);
 
       int status, i, k, j, type;
    uint32_t idx;
    SOSuperBlock *p_sb;
    SOInode InodeDir, InodeEnt;
    SODataClust dc, dcEnt;

 
    //Load do superbloco
    if((status = soLoadSuperBlock()) != 0)
        return status;
    //Load do superbloco
    p_sb = soGetSuperBlock(); 
    
    //Validações do nInodeDir e do nInodeEnt
    if(nInodeDir < 0 || nInodeDir > p_sb->iTotal)       
        return -EINVAL; 
   
    if(nInodeEnt < 0 || nInodeEnt > p_sb->iTotal)       
       return -EINVAL; 
    
    /* verificação se o ponteiro que passa o nome não é null */
    if(eName == NULL)                       
        return -EINVAL;  
   
   /* Validação*/
    if(strlen(eName) > MAX_NAME)           
        return -ENAMETOOLONG; 
   
   //ler inode
    if((status = soReadInode(&InodeDir, nInodeDir, IUIN)) != 0)
        return status;
    /*validação de consistência*/
    if((InodeDir.mode & INODE_DIR) != INODE_DIR) 
        return -ENOTDIR;

    if(InodeDir.refCount >= 65536 - 2)
        return -EMLINK;

    /* verificação de permissões do inodeDir */
    if((status = soAccessGranted(nInodeDir,X)) != 0)       
        return status;
    
    if((status = soAccessGranted(nInodeDir,W)) != 0) 
        return status;
    
    if(soGetDirEntryByName(nInodeDir, eName, NULL, &idx) == 0)
        return -EEXIST;
 
    switch(op)
    {
        case ADD:
        {
            if((status = soReadInode(&InodeEnt, nInodeEnt, IUIN)) != 0)
                return status;

             type = InodeEnt.mode & INODE_TYPE_MASK;        

            switch (type)
            {
                case INODE_DIR:
                {
    
                    strncpy((char *) dcEnt.info.de[0].name, ".", MAX_NAME+1);
                    dcEnt.info.de[0].nInode = nInodeEnt;
 
                    strncpy((char *) dcEnt.info.de[1].name, "..", MAX_NAME+1);
                    dcEnt.info.de[1].nInode = nInodeDir;
 
                    for(i = 2; i < DPC; i++){
                        memset(dcEnt.info.de[i].name, '\0' , MAX_NAME+1);
                        dcEnt.info.de[i].nInode = NULL_INODE;
                    }
                    
                    if((status = soWriteFileCluster(nInodeEnt, 0, &dcEnt)) != 0)
                        return status;
                    
                    if((status = soReadInode(&InodeEnt, nInodeEnt, IUIN)) != 0)
                        return status;
                                                        
                    InodeEnt.refCount += 2;
                    InodeEnt.size = sizeof(SODirEntry)*DPC;
                    InodeDir.refCount ++;
                    break;
                }//end case INODE_DIR
                case INODE_FILE:
                case INODE_SYMLINK:
                {
                    InodeEnt.refCount ++;
                    break;
                }
                default:
                {
                    return -EIUININVAL;
                    break;
                }
            }//end switch(type)
            break;
        }//end case ADD:
        case ATTACH:
        {
            
            if((status = soReadInode(&InodeEnt, nInodeEnt, IUIN)) != 0) 
                return status;
 
            type = InodeEnt.mode & INODE_TYPE_MASK;
            switch(type)
            {
                case INODE_DIR:
                {
                    if((InodeEnt.mode & INODE_DIR) != INODE_DIR)       
                       return -ENOTDIR;
                   
                    if(InodeEnt.refCount >= 65200 - 2)              
                        return -EMLINK;
                    
                    if((status = soReadFileCluster(nInodeEnt, 0, &dcEnt)) != 0)
                       return status;
                                                    
                    strncpy((char *) dcEnt.info.de[0].name, ".", MAX_NAME+1);
                    dcEnt.info.de[0].nInode = nInodeEnt;
 
                    strncpy((char *) dcEnt.info.de[1].name, "..", MAX_NAME+1);
                    dcEnt.info.de[1].nInode = nInodeDir;
 
                    
                    if((status = soWriteFileCluster(nInodeEnt, 0, &dcEnt)) != 0)
                        return status;
                    
                    if((status = soReadInode(&InodeEnt, nInodeEnt, IUIN)) != 0) 
                        return status;
                                                        
                    InodeEnt.refCount += 2;
                    InodeDir.refCount ++;
                    break;
                }//end case INODE_DIR
                case INODE_FILE:
                case INODE_SYMLINK:
                {
                    InodeEnt.refCount ++;
                    break;
                }
                default:
                {
                    return -EIUININVAL;
                    break;
                }
            }//end switch(type)
            break;
        }//end case ATTACH:
        default:
        {
            return -EINVAL;
            break;
        }
    }//end switch(op)
   
    i = idx / DPC; //indice do dataCluster
    
    k = idx % DPC; //indice do directorio
    
    if((status = soReadFileCluster(nInodeDir, i, &dc)) != 0)       
        return status;
 
                                            
    strncpy((char *) dc.info.de[k].name, eName, MAX_NAME+1);            
    dc.info.de[k].nInode = nInodeEnt;
 
    
    if(k == 0 && i > 0)                             
    {
        for(j = 1; j < DPC; j++)
        {
            memset(dc.info.de[j].name, '\0' , MAX_NAME+1);
            dc.info.de[j].nInode = NULL_INODE;
        }
        InodeDir.size += sizeof(SODirEntry)*DPC; 
    }
    
    if((status = soWriteInode(&InodeDir,nInodeDir, IUIN)) != 0)       
        return status;
    
    if((status = soWriteFileCluster(nInodeDir, i, &dc)) != 0)       
        return status;
    
    if((status = soWriteInode(&InodeEnt,nInodeEnt, IUIN)) != 0)   
        return status;

 
    return 0;
}