/**
 *  \file soGetDirEntryByPath.c (implementation file)
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

/* Allusion to internal function */

int soTraversePath (const char *ePath, uint32_t *p_nInodeDir, uint32_t *p_nInodeEnt);

/** \brief Number of symbolic links in the path */

static uint32_t nSymLinks = 0;

/** \brief Old directory inode number */

static uint32_t oldNInodeDir = 0; 

/**
 *  \brief Get an entry by path.
 *
 *  The directory hierarchy of the file system is traversed to find an entry whose name is the rightmost component of
 *  <tt>ePath</tt>. The path is supposed to be absolute and each component of <tt>ePath</tt>, with the exception of the
 *  rightmost one, should be a directory name or symbolic link name to a path.
 *
 *  The process that calls the operation must have execution (x) permission on all the components of the path with
 *  exception of the rightmost one.
 *
 *  \param ePath pointer to the string holding the name of the path
 *  \param p_nInodeDir pointer to the location where the number of the inode associated to the directory that holds the
 *                     entry is to be stored
 *                     (nothing is stored if \c NULL)
 *  \param p_nInodeEnt pointer to the location where the number of the inode associated to the entry is to be stored
 *                     (nothing is stored if \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the pointer to the string is \c NULL
 *  \return -\c ENAMETOOLONG, if the path or any of the path components exceed the maximum allowed length
 *  \return -\c ERELPATH, if the path is relative and it is not a symbolic link
 *  \return -\c ENOTDIR, if any of the components of <tt>ePath</tt>, but the last one, is not a directory
 *  \return -\c ELOOP, if the path resolves to more than one symbolic link
 *  \return -\c ENOENT,  if no entry with a name equal to any of the components of <tt>ePath</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on any of the components
 *                      of <tt>ePath</tt>, but the last one
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */


 
int soGetDirEntryByPath (const char *ePath, uint32_t *p_nInodeDir, uint32_t *p_nInodeEnt)
{
  soColorProbe (311, "07;31", "soGetDirEntryByPath (\"%s\", %p, %p)\n", ePath, p_nInodeDir, p_nInodeDir);
 
  uint32_t InodeDir, InodeEnt;  //numero do inode associado ao diretorio, numero do inode de entreda
  int stat;    
 
 
  //verifica se o ponteiro para o epath é nulo ou está vazio
  if(ePath == NULL || (*ePath == '\0'))
        return -EINVAL;
 
    //verifica se o epath e absoluto
    if(ePath[0] != '/')
        return -ERELPATH;
 
    //Verifica se epath excede o numero de carateres máximo
    if(strlen(ePath) > MAX_PATH)
        return -ENAMETOOLONG;
 
    //oldNInodeDir = 0;
    //nSymLinks = 0;
 
    //percurso na arvore de diretorios
    if((stat = soTraversePath(ePath, &InodeDir, &InodeEnt)) != 0)
        return stat;
 
    //no caso de p_nInodeDir não ser null, passa a ter o valor do InodeDir
    if(p_nInodeDir != NULL)
        *p_nInodeDir = InodeDir;
 
    //no caso de o p_nInodeEnt não ser null, passa a ter o valor do InodeEnt
    if(p_nInodeEnt != NULL)
        *p_nInodeEnt = InodeEnt;
 
    return 0;
}
 
/**
 *  \brief Traverse the path.
 *
 *  \param ePath pointer to the string holding the name of the path
 *  \param p_nInodeDir pointer to the location where the number of the inode associated to the directory that holds the
 *                     entry is to be stored
 *  \param p_nInodeEnt pointer to the location where the number of the inode associated to the entry is to be stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c ENAMETOOLONG, if any of the path components exceed the maximum allowed length
 *  \return -\c ERELPATH, if the path is relative and it is not a symbolic link
 *  \return -\c ENOTDIR, if any of the components of <tt>ePath</tt>, but the last one, is not a directory
 *  \return -\c ELOOP, if the path resolves to more than one symbolic link
 *  \return -\c ENOENT,  if no entry with a name equal to any of the components of <tt>ePath</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on any of the components
 *                      of <tt>ePath</tt>, but the last one
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */
 
int soTraversePath (const char *ePath, uint32_t *p_nInodeDir, uint32_t *p_nInodeEnt)
{
    int stat;
    SOInode inode;
 
    uint32_t InodeDir, InodeEnt;    //numero do inode associado ao diretorio, numero do inode de entreda

    uint32_t oldNInodeDir=0; 


    char path[MAX_PATH+1];    //auxiliar para o string copy
    char name[MAX_PATH+1];    //auxiliar para o string copy
    char *path_char; 
    char *name_char; 
    SOSuperBlock *p_sb;

    strcpy(path, ePath);   
    strcpy(name, ePath);  
    path_char = dirname(path);  //dirname de epath para path
    name_char = basename(name);    //basename de epath para name

    //uint32_t nSymLinks=0;

    //condicao de paragem para atalhos
    if (strcmp(path, ".") == 0) { 
        if (nSymLinks) {
            *p_nInodeEnt = oldNInodeDir;
            nSymLinks--;
            if ((stat = soGetDirEntryByName(*p_nInodeEnt, name, p_nInodeEnt, NULL)) != 0) {
                return stat;
            }
        }
        return 0;
    }


    //verificar se o epath está vazio
    if(ePath == NULL)
        return -EINVAL;
 
    //o tamanho do nome não pode ultrapassar o maximo estabelecido
    if(strlen(name) > MAX_NAME)
        return -ENAMETOOLONG;
 
 
    //verifica se o diretorio é root
    if(strcmp(path_char, "/") == 0){
        if (strcmp(name_char, "/") ==0)
            strcpy(name_char,"."); 
        InodeDir = 0; 
    }
    else{
        if((stat = soTraversePath(path_char, &InodeDir, &InodeEnt)) != 0)
            return stat;
        InodeDir = InodeEnt;
    }


    //lê inode  do diretorio de entrada se este estiver em uso
    if((stat = soReadInode(&inode, InodeDir, IUIN))!= 0)
        return stat;
 

    if((stat=soLoadSuperBlock()) != 0) //verification of the consisty of superblock            
        return stat;
        
    p_sb = soGetSuperBlock();


    if ((stat = soQCheckDirCont(p_sb,&inode)) != 0) // Quick check directory content 
        return stat;

    //verificar se tem acesso e permissoes de execucao
    if((stat = soAccessGranted(InodeDir, X)) != 0)
        return stat;

    //procurar o diretorio atraves do nome e obter o numero do inode
    if((stat = soGetDirEntryByName(InodeDir, name_char, &InodeEnt, NULL)) !=0)
        return stat;

    if((stat = soReadInode(&inode, InodeEnt, IUIN)) != 0)
        return stat;

    //lê caminho
    //if((stat = soReadFileCluster(*p_nInodeEnt, 0, aux)) != 0)
    //    return stat;

 
    //no caso de p_nInodeDir não ser null, passa a ter o valor do InodeDir
    if(p_nInodeDir != NULL)
        *p_nInodeDir = InodeDir;
 
    //no caso de o p_nInodeEnt não ser null, passa a ter o valor do InodeEnt
    if(p_nInodeEnt != NULL)
        *p_nInodeEnt = InodeEnt;

    return 0;   
}