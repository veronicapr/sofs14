/**
 *  \file soRenameDirEntry.c (implementation file)
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

/**
 *  \brief Rename an entry of a directory.
 *
 *  The directory entry whose name is <tt>oldName</tt> has its <em>name</em> field changed to <tt>newName</tt>. Thus,
 *  the inode associated to the directory must be in use and belong to the directory type.
 *
 *  Both the <tt>oldName</tt> and the <tt>newName</tt> must be <em>base names</em> and not <em>paths</em>, that is,
 *  they can not contain the character '/'. Besides an entry whose <em>name</em> field is <tt>oldName</tt> should exist
 *  in the directory and there should not be any entry in the directory whose <em>name</em> field is <tt>newName</tt>.
 *
 *  The process that calls the operation must have write (w) and execution (x) permissions on the directory.
 *
 *  \param nInodeDir number of the inode associated to the directory
 *  \param oldName pointer to the string holding the name of the direntry to be renamed
 *  \param newName pointer to the string holding the new name
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range or either one of the pointers to the strings are
 *                      \c NULL or the name strings do not describe file names
 *  \return -\c ENAMETOOLONG, if one of the name strings exceeds the maximum allowed length
 *  \return -\c ENOTDIR, if the inode type is not a directory
 *  \return -\c ENOENT,  if no entry with <tt>oldName</tt> is found
 *  \return -\c EEXIST,  if an entry with the <tt>newName</tt> already exists
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on the directory
 *  \return -\c EPERM, if the process that calls the operation has not write permission on the directory
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soRenameDirEntry (uint32_t nInodeDir, const char *oldName, const char *newName)
{
  soColorProbe (315, "07;31", "soRenameDirEntry (%"PRIu32", \"%s\", \"%s\")\n", nInodeDir, oldName, newName);

  
  SOInode inode; //vai armazenr nInodeDir
  uint32_t nClust, idx;
  int stat;
  char aux[MAX_NAME+1]; //para armazenar newName ou oldName
  SODataClust dc; 
  
  /* verificar consistencia do inode associado 
   *  a nInodeDir (tem de estar em uso) */
   if((stat = soReadInode(&inode, nInodeDir, IUIN)) != 0)
    return stat;
	
	 /* verificar se o inode tem um diretorio associado*/
   if((inode.mode & INODE_DIR) == 0)
    return -ENOENT;
	 
   /* verificaçao das permissoes  */
	if( (stat=soAccessGranted(nInodeDir,X)) != 0)
		return -EACCES;
	if( (stat=soAccessGranted(nInodeDir,W)) != 0)
		return -EPERM;

  /* verificaçao de parametros */
  if((oldName == NULL) || (newName == NULL))
  	return -EINVAL;
  
   /* verificar se o novo nome nao ultrapassa o tamanho maximo */
  if(strlen(newName) > MAX_NAME )
  	return -ENAMETOOLONG;

  /* Verifica que nao existe '/' nos nomes, 
   * porque e um separador de diretorios*/ 
  strcpy(aux, (char*) oldName);
  if(strcmp (oldName, basename (aux)) != 0)
  	return -EINVAL;
  
  strcpy(aux, (char*) newName);
  if(strcmp (newName, basename (aux)) != 0)
  	return  -EINVAL;

   /* Verifica se encontra uma entrada com oldName */
  if((stat = soGetDirEntryByName(nInodeDir, oldName, NULL, &idx)) != 0) 
  	return stat;

  /* verifica de encontra uma entrada com o newName */
  if((stat = soGetDirEntryByName(nInodeDir, newName, NULL, NULL)) == 0) // &idx
   	return -EEXIST;  
  

   nClust = idx / DPC; //calculo do numero do cluster que conntem o nome
    

   if((stat = soReadFileCluster(nInodeDir, nClust, &dc)) != 0)
   	return stat;

   /* muda o nome do diretorio */
   memset((char*)dc.info.de[idx].name,'\0', MAX_NAME);
   strcpy((char*)dc.info.de[idx].name, newName);

   /* Aplicar as alteraçoes no cluster */
   if((stat = soWriteFileCluster(nInodeDir, nClust, &dc)) != 0)
   	return stat;
   	
  return 0;
}
