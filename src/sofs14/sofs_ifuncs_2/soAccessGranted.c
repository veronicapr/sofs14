/**
 *  \file soAccessGranted.c (implementation file)
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
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/** \brief performing a read operation */
#define R  0x0004
/** \brief performing a write operation */
#define W  0x0002
/** \brief performing an execute operation */
#define X  0x0001

/* allusion to internal function */

int soReadInode (SOInode *p_inode, uint32_t nInode, uint32_t status);

/**
 *  \brief Check the inode access rights against a given operation.
 *
 *  The inode must to be in use and belong to one of the legal file types.
 *  It checks if the inode mask permissions allow a given operation to be performed.
 *
 *  When the calling process is <em>root</em>, access to reading and/or writing is always allowed and access to
 *  execution is allowed provided that either <em>user</em>, <em>group</em> or <em>other</em> have got execution
 *  permission.
 *
 *  \param nInode number of the inode
 *  \param opRequested operation to be performed:
 *                    a bitwise combination of R, W, and X
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if <em>buffer pointer</em> is \c NULL or no operation of the defined class is described
 *  \return -\c EACCES, if the operation is denied
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAccessGranted (uint32_t nInode, uint32_t opRequested)
{
 	soColorProbe (514, "07;31", "soAccessGranted (%"PRIu32", %"PRIu32")\n", nInode, opRequested);

	uint32_t owner = getuid();
  	uint32_t group = getgid();
	int stat;
 	SOInode inode;

 	/*Validaçao da opçao escolhida*/
 	if(opRequested < 1 || opRequested > 7)
 		return -EINVAL;

 	/* Fazer load ao Super Bloco com validaçao */
 	if((stat = soLoadSuperBlock()) != 0) return stat;
 	SOSuperBlock *p_sb = soGetSuperBlock();

 	/* Validaçao do Inode */
 	if(nInode >= p_sb->iTotal) return -EINVAL;

 	/*Validação de consistencia do nInode(tem de estar em uso)*/
 	if((stat = soReadInode(&inode, nInode, IUIN)) != 0) return stat;

 	/* Verificar se user é root */
 	if(geteuid() == 0) {
		if (opRequested & X)			// X - permissao para executar
		{
			//Óu o owner, ou o group ou others tem que ter permissao para executar
			if ((inode.mode & (INODE_EX_USR | INODE_EX_GRP | INODE_EX_OTH)) == 0)
				return -EACCES;
		}
		
		return 0; // Acesso garantido
	}


	if (owner == inode.owner)				//testar as condições para o  owner
	{
		if (opRequested & R)		// R - permissao para ler
		{
			if ((inode.mode & INODE_RD_USR) != INODE_RD_USR)
				return -EACCES;
		}
		if (opRequested & W)			// W - permissao para escrever
		{
			if ((inode.mode & INODE_WR_USR) != INODE_WR_USR)
				return -EACCES;
		}
		if (opRequested & X)			// X - permissao para executar
		{
			if ((inode.mode & INODE_EX_USR) != INODE_EX_USR)
				return -EACCES;
		}
	}
	else if (group == inode.group)			//testar as condições para o group
	{
		if (opRequested & R)			// R - permissao para ler
		{
			if ((inode.mode & INODE_RD_GRP) != INODE_RD_GRP)
				return -EACCES;
		}
		if (opRequested & W)			// W - permissao para escrever
		{
			if ((inode.mode & INODE_WR_GRP) != INODE_WR_GRP)
				return -EACCES;
		}
		if (opRequested & X)			// X - permissao para executar
		{
			if ((inode.mode & INODE_EX_GRP) != INODE_EX_GRP)
				return -EACCES;
		}
	}
	else									        //testar as condições para others
	{
		if (opRequested & R)			// R - permisssao para ler
		{ 
			if ((inode.mode & INODE_RD_OTH) != INODE_RD_OTH)
				return -EACCES;
		}
		if (opRequested & W)			// W - permissao para escrever
		{
			if ((inode.mode & INODE_WR_OTH) != INODE_WR_OTH)
				return -EACCES;
		}
		if (opRequested & X)			// X - permissao para executar
		{
			if ((inode.mode & INODE_EX_OTH) != INODE_EX_OTH)
				return -EACCES;
		}
	}
	
	/* Faz store do bloco de inodes */
	if((stat = soStoreBlockInT()) != 0) return stat;

	return 0;	


}
