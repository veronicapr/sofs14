/**
 *  \file soHandleFileClusters.c (implementation file)
 *
 *  \Luis Gameiro 67989
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>

#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#include "sofs_ifuncs_1.h"
#include "sofs_ifuncs_2.h"

/** \brief operation get the physical number of the referenced data cluster */
#define GET         0
/** \brief operation allocate a new data cluster and associate it to the inode which describes the file */
#define ALLOC       1
/** \brief operation free the referenced data cluster */
#define FREE        2
/** \brief operation free the referenced data cluster and dissociate it from the inode which describes the file */
#define FREE_CLEAN  3
/** \brief operation dissociate the referenced data cluster from the inode which describes the file */
#define CLEAN       4

/* allusion to internal function */

int soHandleFileCluster (uint32_t nInode, uint32_t clustInd, uint32_t op, uint32_t *p_outVal);

/**
 *  \brief Handle all data clusters from the list of references starting at a given point.
 *
 *  The file (a regular file, a directory or a symlink) is described by the inode it is associated to.
 *
 *  Several operations are available and can be applied to the file data clusters starting from the index to the list of
 *  direct references which is given.
 *
 *  The list of valid operations is
 *
 *    \li FREE:       free all data clusters starting from the referenced data cluster
 *    \li FREE_CLEAN: free all data clusters starting from the referenced data cluster and dissociate them from the
 *                    inode which describes the file
 *    \li CLEAN:      dissociate all data clusters starting from the referenced data cluster from the inode which
 *                    describes the file.
 *
 *  Depending on the operation, the field <em>clucount</em> and the lists of direct references, single indirect
 *  references and double indirect references to data clusters of the inode associated to the file are updated.
 *
 *  Thus, the inode must be in use and belong to one of the legal file types for the operations FREE and FREE_CLEAN and
 *  must be free in the dirty state for the operation CLEAN.
 *
 *  \param nInode number of the inode associated to the file
 *  \param clustIndIn index to the list of direct references belonging to the inode which is referred (it contains the
 *                    index of the first data cluster to be processed)
 *  \param op operation to be performed (FREE, FREE AND CLEAN, CLEAN)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> or the <em>index to the list of direct references</em> are out of
 *                      range or the requested operation is invalid
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
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

int soHandleFileClusters (uint32_t nInode, uint32_t clustIndIn, uint32_t op)
{
  soColorProbe (414, "07;31", "soHandleFileClusters (%"PRIu32", %"PRIu32", %"PRIu32")\n", nInode, clustIndIn, op);

  SOSuperBlock *p_sb;		//ponteiro para o superbloco
  SOInode inode;			//ponteiro para o inode a sofrer alterações
  int stat;				
  uint32_t inode_status;	//Estado do inode recebido como argumento da função
  SODataClust temp1, temp2;		// variaveis para armazenamento das tabelas de referencias e dos clusters respetivos;
  int i,j;				

   /*Obtenção do ponteiro para o superbloco*/
   if((stat = soLoadSuperBlock()) != 0) return stat;
   p_sb = soGetSuperBlock();

   /*Verificações de conformidade*/
   /*Validação do numero do inode*/
   if(nInode >= p_sb->iTotal) return -EINVAL;

   /*Verificação do index recebido (se está dentro da gama de clusters)*/
   if(clustIndIn >= MAX_FILE_CLUSTERS) return -EINVAL;

   /*Verificação se op representa uma das três operações validas*/
   if(op != FREE && op != FREE_CLEAN && op != CLEAN) return -EINVAL;

   /*Verificação de consistencia*/
   /*Como o estado do inode não é directamente recebido temos de o obter atraves da operação recebida*/
   if(op == CLEAN)
   	inode_status = FDIN; /*para a operação CLEAN deve estar livre no estado sujo*/
   else
   	inode_status = IUIN; /*para as restantes operações deve estar em uso*/

	if((stat = soReadInode(&inode, nInode, inode_status)) != 0) return stat;

	/*Segundo a informação fornecida, devemos efectuar o processamento da zona de dados a começar pelo fim, 
	isto é, pela tabela de referencias duplamente indirectas, armazenada em i2*/

	if(inode.i2 != NULL_CLUSTER){
		
		/*Load da tabela de referencias indirectas associadas a i2*/
		if((stat = soLoadSngIndRefClust(p_sb->dZoneStart + inode.i2 * BLOCKS_PER_CLUSTER)) != 0) return stat;
		temp1 = *soGetSngIndRefClust(); /*tabela de referencias duplamente indirectas*/

			if(clustIndIn < (N_DIRECT + RPC)){ /*Se clustIndIn estiver fora da tabela duplamente indirecta é preciso operar sobre todos os clusters*/
				i = 0;					
				j = 0;
			}else{		/*Caso contrario começa a operar-se apartir do indice recebido*/
				i = (clustIndIn - N_DIRECT - RPC) % RPC; 
				j = (clustIndIn - N_DIRECT - RPC) / RPC;
			}

		/*Percorrer tabela de referencias indirectas*/
		for(; j < RPC; j++){
			/*Se alguma referencia não estiver vazia, realiza operações sobre ela*/
			if(temp1.info.ref[j] != NULL_CLUSTER){
				/*carregamento da tabela de referencias directas*/
				if((stat = soLoadDirRefClust(p_sb->dZoneStart + temp1.info.ref[j] * BLOCKS_PER_CLUSTER)) != 0) return stat;
				temp2 = *soGetDirRefClust();


				/*Percorrer a tabela de referencias directas e se alguma apontar para um cluster realiza operações sobre ela*/
				for(; i < RPC; i++){
					if(temp2.info.ref[i] != NULL_CLUSTER){
						if((stat = soHandleFileCluster(nInode, (N_DIRECT + RPC + j * RPC + i), op, NULL)) != 0) return stat;
						}
 				}// end for(i)
 				i = 0; /*reset do contador para usar com um novo j*/
			}//end if temp1 != NULL_CLUSTER
		} // end for(j)	
	
	}// end if(p_inode.i2 != NULL_CLUSTER) tabela i2

	/*Analise da tabela de referencias simplesmente indirectas*/
	if(clustIndIn < (N_DIRECT + RPC) && inode.i1 != NULL_CLUSTER){
		if((stat = soLoadDirRefClust(p_sb->dZoneStart + inode.i1 * BLOCKS_PER_CLUSTER)) != 0) return stat;
		temp1 = *soGetDirRefClust();

		if(clustIndIn < N_DIRECT)	/*Se clustIndIn estiver fora da tabela simplesmente indirecta é preciso operar todos os clusters*/
			i = 0;
		else					/*Caso contrario pode operar-se apartir do index recebido*/
			i = clustIndIn - N_DIRECT;

		/*Percorrer a tabela de referencias associadas a uma posição da tabela de referencias simplesmente indirectas, se alguma posição apontar para um cluster, realiza-se operações sobre ele*/
		for(; i < RPC; i++){
			if(temp1.info.ref[i] != NULL_CLUSTER){
				if((stat = soHandleFileCluster(nInode, (N_DIRECT+i), op, NULL)) != 0) return stat;
			}
		}/*end for(i)*/
	}// end if tabela i1

	/*Analise da tabela de referencias directas*/	
	if(clustIndIn < N_DIRECT){
		for(i = clustIndIn; i < N_DIRECT; i++){
			if(inode.d[i] != NULL_CLUSTER){
				if((stat = soHandleFileCluster(nInode, i, op, NULL)) != 0) return stat;
			}
		}// end for(i)
	}//end if tabela directas	 

  return 0;
}
