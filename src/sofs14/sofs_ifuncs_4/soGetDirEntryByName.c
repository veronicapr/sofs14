/**
 *  \file soGetDirEntryByName.c (implementation file)
 *
 *  \author Rui Oliveira
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

/**
 *  \brief Get an entry by name.
 *
 *  The directory contents, seen as an array of directory entries, is parsed to find an entry whose name is
 *  <tt>eName</tt>. Thus, the inode associated to the directory must be in use and belong to the directory type.
 *
 *  The <tt>eName</tt> must also be a <em>base name</em> and not a <em>path</em>, that is, it can not contain the
 *  character '/'.
 *
 *  The process that calls the operation must have execution (x) permission on the directory.
 *
 *  \param nInodeDir number of the inode associated to the directory
 *  \param eName pointer to the string holding the name of the directory entry to be located
 *  \param p_nInodeEnt pointer to the location where the number of the inode associated to the directory entry whose
 *                     name is passed, is to be stored
 *                     (nothing is stored if \c NULL)
 *  \param p_idx pointer to the location where the index to the directory entry whose name is passed, or the index of
 *               the first entry that is free in the clean state, is to be stored
 *               (nothing is stored if \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range or the pointer to the string is \c NULL or the
 *                      name string does not describe a file name
 *  \return -\c ENAMETOOLONG, if the name string exceeds the maximum allowed length
 *  \return -\c ENOTDIR, if the inode type is not a directory
 *  \return -\c ENOENT,  if no entry with <tt>name</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on the directory
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soGetDirEntryByName (uint32_t nInodeDir, const char *eName, uint32_t *p_nInodeEnt, uint32_t *p_idx){
	soColorProbe (312, "07;31", "soGetDirEntryByName (%"PRIu32", \"%s\", %p, %p)\n", nInodeDir, eName, p_nInodeEnt, p_idx);
  
    int stat;
    SOSuperBlock *p_sb;
    SOInode inode;
    SODataClust dc; 
    uint32_t idxCluster,idxDirEntry,flag = 0; 

    // load superblock
    if ((stat = soLoadSuperBlock()) != 0)
        return stat;
    p_sb = soGetSuperBlock();

    // validacao de parametros
    if (eName == NULL || (strlen(eName)) == 0 || nInodeDir >= p_sb->iTotal || strchr(eName, '/') )
        return -EINVAL;

    if (strlen(eName) > MAX_NAME)
        return -ENAMETOOLONG;

    //reading directory inode
    if ((stat = soReadInode(&inode, nInodeDir, IUIN)) != 0)
        return stat;
    // check if inode type is a directory
    if ((inode.mode & INODE_TYPE_MASK) != INODE_DIR) 
        return -ENOTDIR;
    //check dir
    if((stat = soQCheckDirCont(p_sb,&inode))!=0)
        return stat;
    // checking permissions
    if ((stat = soAccessGranted(nInodeDir, X)) != 0)
        return stat;

    for (idxCluster = 0; idxCluster < (inode.size/(DPC*sizeof(SODirEntry))); idxCluster++){
        if ((stat = soReadFileCluster(nInodeDir, idxCluster, &dc)) != 0)
            return stat;
        
        for (idxDirEntry = 0; idxDirEntry < DPC; idxDirEntry++){
            if (strcmp((char *)(dc.info.de[idxDirEntry].name), eName) == 0){
                if(p_nInodeEnt != NULL) 
                    *p_nInodeEnt = (dc.info.de[idxDirEntry].nInode);
                if(p_idx != NULL) 
                    *p_idx =((idxCluster*DPC)+idxDirEntry);
                return 0;
            }                
                       
            //Check if entry idxDirEntry is free
            if((dc.info.de[idxDirEntry].name[0] == '\0') && (dc.info.de[idxDirEntry].name[MAX_NAME] == '\0') && flag==0)
            {
                flag = 1;
                if(p_idx != NULL)
                    *p_idx = ((idxCluster*DPC)+idxDirEntry);
            }
           } 
        }

   if((p_idx != NULL) && (flag == 0)) 
        *p_idx = (inode.cluCount)*DPC;
    return -ENOENT;


   return 0;   
}

