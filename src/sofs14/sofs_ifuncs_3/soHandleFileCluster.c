/**
 *  \file soHandleFileCluster.c (implementation file)
 *
 *  \authors
 *		- João Mota 68118
 *		- Rui Oliveira 68779
 *		- Verónica Rocha 68809
 *		- David Silva 64152
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#include "sofs_ifuncs_1.h"
#include "sofs_ifuncs_2.h"

/** \brief operation get the logical number of the referenced data cluster for an inode in use */
#define GET         0
/** \brief operation allocate a new data cluster and associate it to the inode which describes the file */
#define ALLOC       1
/** \brief operation free the referenced data cluster */
#define FREE        2
/** \brief operation free the referenced data cluster and dissociate it from the inode which describes the file */
#define FREE_CLEAN  3
/** \brief operation dissociate the referenced data cluster from the inode which describes the file */
#define CLEAN       4

/* allusion to internal functions */

int soHandleDirect (SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t nClust, uint32_t op,
						   uint32_t *p_outVal);
int soHandleSIndirect (SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t nClust, uint32_t op,
							  uint32_t *p_outVal);
int soHandleDIndirect (SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t nClust, uint32_t op,
							  uint32_t *p_outVal);
int soAttachLogicalCluster (SOSuperBlock *p_sb, uint32_t nInode, uint32_t clustInd, uint32_t nLClust);
int soCleanLogicalCluster (SOSuperBlock *p_sb, uint32_t nInode, uint32_t nLClust);

/**
 *  \brief Handle of a file data cluster.
 *
 *  The file (a regular file, a directory or a symlink) is described by the inode it is associated to.
 *
 *  Several operations are available and can be applied to the file data cluster whose logical number is given.
 *
 *  The list of valid operations is
 *
 *    \li GET:        get the logical number of the referenced data cluster for an inode in use
 *    \li ALLOC:      allocate a new data cluster and associate it to the inode which describes the file
 *    \li FREE:       free the referenced data cluster
 *    \li FREE_CLEAN: free the referenced data cluster and dissociate it from the inode which describes the file
 *    \li CLEAN:      dissociate the referenced data cluster from the inode which describes the file.
 *
 *  Depending on the operation, the field <em>clucount</em> and the lists of direct references, single indirect
 *  references and double indirect references to data clusters of the inode associated to the file are updated.
 *
 *  Thus, the inode must be in use and belong to one of the legal file types for the operations GET, ALLOC, FREE and
 *  FREE_CLEAN and must be free in the dirty state for the operation CLEAN.
 *
 *  \param nInode number of the inode associated to the file
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> or the <em>index to the list of direct references</em> are out of
 *                      range or the requested operation is invalid or the <em>pointer to outVal</em> is \c NULL when it
 *                      should not be (GET / ALLOC)
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (ALLOC / FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

//	Autor: João Mota 68118
int soHandleFileCluster (uint32_t nInode, uint32_t clustInd, uint32_t op, uint32_t *p_outVal)
{
  	soColorProbe (413, "07;31", "soHandleFileCluster (%"PRIu32", %"PRIu32", %"PRIu32", %p)\n",
				nInode, clustInd, op, p_outVal);

 	int stat;
  	SOSuperBlock *p_sb;
	SOInode inode;

  	
  	if(clustInd >= MAX_FILE_CLUSTERS)
		return -EINVAL;
  	if((op == GET || op == ALLOC) && p_outVal == NULL) 
		return -EINVAL;

	if (op != GET && op != ALLOC && op != FREE && op != FREE_CLEAN  && op != CLEAN)
		return -EINVAL;

  	if((stat = soLoadSuperBlock()) != 0)
		return stat;
  	p_sb = soGetSuperBlock();

  	if(nInode >= p_sb->iTotal)
		return -EINVAL;

  	if(op == FREE || op == FREE_CLEAN || op == CLEAN)
  		p_outVal = NULL;

  	if (op == CLEAN){
		if ((stat = soReadInode(&inode, nInode, FDIN)) != 0)
			return stat;
  	} else {
		if ((stat = soReadInode(&inode, nInode, IUIN)) != 0) 
			return stat;
   	}

   	// Chamada da respetiva função hande para cada um dos casos
   	if (clustInd < N_DIRECT)
		if ((stat = soHandleDirect(p_sb, nInode, &inode, clustInd, op, p_outVal)) != 0) 
			return stat;
	
   	if ((clustInd >= N_DIRECT) && (clustInd < N_DIRECT + RPC))
		if ((stat = soHandleSIndirect(p_sb, nInode, &inode, clustInd, op, p_outVal)) != 0) 
			return stat;
   
   	if (clustInd >= N_DIRECT + RPC)
		if ((stat = soHandleDIndirect(p_sb, nInode, &inode, clustInd, op, p_outVal)) != 0) 
			return stat;

	if(op == ALLOC || op == FREE || op == FREE_CLEAN)
		if ((stat = soWriteInode(&inode, nInode, IUIN)) != 0)
			return stat;
	
	if(op == CLEAN)
		if((stat=soWriteInode(&inode, nInode, FDIN)) != 0)
			return stat;
	
	return 0;
}

/**
 *  \brief Handle of a file data cluster which belongs to the direct references list.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param p_inode pointer to a buffer which stores the inode contents
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the requested operation is invalid
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */
 
// Autor: Rui Oliveira 68779 
int soHandleDirect (SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t clustInd, uint32_t op, uint32_t *p_outVal){

  int stat; 

  switch(op) {
	case GET: 
	  /* colocar a localização onde o nr logico do conjunto de dados é para ser armazenado no array do inode que 
	   *constitui o um agrupamento de dados organizacao do conteudo de informacao do arquivo
	  */
	  *p_outVal = p_inode->d[clustInd];
	  break; 
	case ALLOC: 
	  // alocar cluster de dados

	  if(p_inode->d[clustInd]!=NULL_CLUSTER)
		return -EDCARDYIL;

	  if((stat=soAllocDataCluster(nInode,p_outVal))!=0)
		return stat;
	  
	  if((stat = soAttachLogicalCluster (p_sb ,nInode, clustInd,*p_outVal)) != 0)
	  	return stat;

	  p_inode->d[clustInd]=*p_outVal;
	  p_inode->cluCount++;


	  break;

	case FREE:
		// Libertar cluster de dados

		if(p_inode->d[clustInd]==NULL_CLUSTER)
			return -EDCNOTIL;

		if((stat = soFreeDataCluster(p_inode->d[clustInd])) != 0)
			return stat;
 
		break;

	case CLEAN:
		// Limpar cluster de dados e dissociá-lo do inode

		if(p_inode->d[clustInd]==NULL_CLUSTER)
			return -EDCNOTIL;

		if((stat = soCleanLogicalCluster(p_sb, nInode,p_inode->d[clustInd])) != 0)
			return stat;

		p_inode->d[clustInd]=NULL_CLUSTER;
		p_inode->cluCount--;

		break;

	case FREE_CLEAN: 
		// Limpar cluster de dados, libertá-lo e dissociá-lo do inode

		if(p_inode->d[clustInd]==NULL_CLUSTER)
			return -EDCNOTIL;

		if((stat = soFreeDataCluster(p_inode->d[clustInd])) != 0)
			return stat;

		if((stat = soCleanLogicalCluster(p_sb, nInode,p_inode->d[clustInd])) != 0)
			return stat;

		p_inode->d[clustInd] = NULL_CLUSTER;
		p_inode->cluCount--;

		break;

	default:
		return -EINVAL;
		break;
  }

  return 0;
}

/**
 *  \brief Handle of a file data cluster which belongs to the single indirect references list.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param p_inode pointer to a buffer which stores the inode contents
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the requested operation is invalid
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

 /* Autores:
 		- Verónica Rocha  68809
 		- David Silva	64152
 */
int soHandleSIndirect (SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t clustInd, uint32_t op,
                              uint32_t *p_outVal)
{
	uint32_t status, i;
	SODataClust *p_dirrefdc; 
	
	// Caso i1 não alocado
	if (p_inode->i1 == NULL_CLUSTER) {
    	switch (op) {
			case GET:
	        	*p_outVal = NULL_CLUSTER;                
	        	return 0; // i1 não foi alocado

	      	case ALLOC:
	      		// Alocar cluster para i1

		        if ((status = soAllocDataCluster (nInode, p_outVal)) != 0)
		        	return status;
		        p_inode->cluCount ++;
		        p_inode->i1 = *p_outVal;

		        if ((status = soLoadDirRefClust (p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) !=0)
		        	return status;
		        
		        p_dirrefdc = soGetDirRefClust ();

		        for (i = 0; i < RPC; i++)
		        	p_dirrefdc->info.ref[i] = NULL_CLUSTER;

	        
		        if ((status = soStoreDirRefClust ()) !=0)
		        	return status;

		        if ((status = soAllocDataCluster (nInode, p_outVal)) != 0) 
		        	return status;

	        	if((status = soAttachLogicalCluster(p_sb, nInode, clustInd, *p_outVal)) != 0)
	        		return status;

		        if ((status = soLoadDirRefClust (p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) !=0)
		        	return status;
		        p_dirrefdc = soGetDirRefClust ();

	        	p_inode->cluCount++;
	        	p_dirrefdc->info.ref[clustInd - N_DIRECT] = *p_outVal;                        

	        	// Guardar alterações
	        	if ((status = soStoreDirRefClust ()) !=0)
	        		return status;

				return 0;
	      
		    case FREE:
		    case FREE_CLEAN:
		    case CLEAN:
		    	return -EDCNOTIL; // operações não possíveis com i1 == NULL_CLUSTER
    	}
  	} else {

  		// Estando i1 alocado, continuar
		if ((status = soLoadDirRefClust (p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) !=0)
			return status;
		p_dirrefdc = soGetDirRefClust ();

		switch (op) {
			case GET:
				*p_outVal = p_dirrefdc->info.ref[clustInd - N_DIRECT];
				return 0;

			case ALLOC:
				// Verificar se o cluster já não foi referenciado
				if ((p_dirrefdc->info.ref[clustInd - N_DIRECT]) != NULL_CLUSTER)
					return -EDCARDYIL;

				if ((status = soAllocDataCluster (nInode, p_outVal)) != 0)
					return status;
    
				if((status = soAttachLogicalCluster(p_sb, nInode, clustInd, *p_outVal)) != 0)
					return status;

				if ((status = soLoadDirRefClust (p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) !=0)
					return status;
        
        		p_dirrefdc = soGetDirRefClust ();

				p_dirrefdc->info.ref[clustInd - N_DIRECT] = *p_outVal; 
				p_inode->cluCount ++;

				// Guardar alterações
				if ((status = soStoreDirRefClust ()) !=0)
					return status;

				return 0;
  
			case FREE:
			case FREE_CLEAN:
			case CLEAN:
				if (p_dirrefdc->info.ref[clustInd - N_DIRECT] == NULL_CLUSTER)
					return -EDCNOTIL;

				if (op != CLEAN)
					if ((status = soFreeDataCluster (p_dirrefdc->info.ref[clustInd - N_DIRECT])) != 0)
							return status;

				if (op == FREE) return 0;

				if ((status = soCleanLogicalCluster (p_sb, nInode, p_dirrefdc->info.ref[clustInd - N_DIRECT])) != 0)
					return status;
				
				p_dirrefdc->info.ref[clustInd - N_DIRECT] = NULL_CLUSTER;
				p_inode->cluCount --;

				if ((status = soStoreDirRefClust ()) !=0)
					return status;

				// Verificar se está tudo limpo
				for (i = 0; i < RPC; i++) {
					if (p_dirrefdc->info.ref[i] != NULL_CLUSTER)
						return 0; // parar, caso não esteja
				}

				if ((status = soFreeDataCluster (p_inode->i1)) != 0)
					return status;
          
				if ((status = soCleanLogicalCluster (p_sb, nInode, p_inode->i1)) != 0)
					return status;

				p_inode->i1 = NULL_CLUSTER;
				p_inode->cluCount--;
		}
    	return 0;
    }
	return -EINVAL;
}


/**
 *  \brief Handle of a file data cluster which belongs to the double indirect references list.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param p_inode pointer to a buffer which stores the inode contents
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the requested operation is invalid
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

/* Autores:
	- João Mota	68118
	- David Silva	64152
*/
int soHandleDIndirect (SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t clustInd, uint32_t op,
							  uint32_t *p_outVal)
{
	uint32_t status, i;
	SODataClust *p_dirrefdc, *p_indrefdc;
	uint32_t nLClust;
	uint32_t dirref;
	uint32_t indirref_idx = (clustInd - N_DIRECT - RPC) / RPC;
  	uint32_t dirref_idx = (clustInd - N_DIRECT - RPC) % RPC;

    switch(op) {
    	case GET:
    		if(p_inode->i2 == NULL_CLUSTER){
    			// Every ref. is NULL, table of indirect refs wasn't initialized
    			*p_outVal = NULL_CLUSTER;
    			break;
    		}

    		// Load single ind ref data cluster
    		if((status = soLoadSngIndRefClust (p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
    			return status;
    		p_dirrefdc=soGetSngIndRefClust();

    		if(p_dirrefdc->info.ref[indirref_idx] == NULL_CLUSTER) {
    			*p_outVal = NULL_CLUSTER;
    			break;
    		}

    		// Load double ind ref data cluster
    		if((status = soLoadDirRefClust(p_sb->dZoneStart + p_dirrefdc->info.ref[indirref_idx] * BLOCKS_PER_CLUSTER)) != 0)
    			return status;
    		p_indrefdc = soGetDirRefClust();

    		// return logical number of the requested data cluster
    		*p_outVal = p_indrefdc->info.ref[dirref_idx];

    		break;

    	case ALLOC:
    		// Alloc a data claster for the ind. ref table
    		if(p_inode->i2==NULL_CLUSTER) {
    			if((status=soAllocDataCluster(nInode, &nLClust)) != 0)
    				return status;

    			p_inode->i2=nLClust;
    			p_inode->cluCount++;

    			// Allocs data clusters on this data cluster

    			if((status = soLoadSngIndRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
    				return status;
    			p_indrefdc = soGetSngIndRefClust();

    			for(i=0; i<RPC; i++)
    				p_indrefdc->info.ref[i] = NULL_CLUSTER;

    			if((status = soStoreSngIndRefClust()) != 0)
    				return status;
    		}
    			
    		if((status = soLoadSngIndRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
    			return status;

    		p_indrefdc = soGetSngIndRefClust();

    		// Allocs a data cluster for the ind ref cluster if not already done
    		if(p_indrefdc->info.ref[indirref_idx] == NULL_CLUSTER) {
    			if ((status = soAllocDataCluster(nInode, &nLClust)) != 0)
    				return status;

    			p_inode->cluCount++;
    			dirref = nLClust;

    			if((status = soLoadDirRefClust(p_sb->dZoneStart +nLClust * BLOCKS_PER_CLUSTER))!= 0)
    				return status;

    			p_dirrefdc = soGetDirRefClust();

    			for(i = 0; i < RPC; i++)
    				p_dirrefdc->info.ref[i] = NULL_CLUSTER;
    			
    			if((status=soStoreDirRefClust()) != 0)
    				return status;
    		} else { dirref = p_indrefdc->info.ref[indirref_idx]; }
    		
    		// Load direct references cluster
    		if((status = soLoadDirRefClust(p_sb->dZoneStart + dirref * BLOCKS_PER_CLUSTER)) != 0)
    			return status;

    		p_dirrefdc = soGetDirRefClust();

    		// Alloc the direct reference cluster if not done already
    		if(p_dirrefdc->info.ref[dirref_idx] == NULL_CLUSTER) {
    			if((status=soAllocDataCluster(nInode, &nLClust)) != 0)
    				return status;
    			
    			if((status=soAttachLogicalCluster(p_sb, nInode, clustInd, nLClust)) != 0)
    				return status;

    			if((status = soLoadDirRefClust(p_sb->dZoneStart + dirref * BLOCKS_PER_CLUSTER)) != 0)
    				return status;

    			p_dirrefdc = soGetDirRefClust();
    			p_inode->cluCount++;
    			p_dirrefdc->info.ref[dirref_idx] = nLClust;
    			*p_outVal=p_dirrefdc->info.ref[dirref_idx];

    			// save changes
    			if ((status=soStoreDirRefClust()) != 0)
    				return status;

    			if((status=soLoadSngIndRefClust(p_sb->dZoneStart+p_inode->i2*BLOCKS_PER_CLUSTER))!=0)
    				return status;

    			p_indrefdc = soGetSngIndRefClust();

    			p_indrefdc->info.ref[indirref_idx] = dirref;

    			if((status=soStoreSngIndRefClust()) != 0)
    				return status;
    		} else {
    			return -EDCARDYIL;
    		}

    		break;

    	case FREE:
    	case FREE_CLEAN:
    	case CLEAN:
    		if(p_inode->i2 == NULL_CLUSTER)
    			return -EDCNOTIL;

    		if((status = soLoadSngIndRefClust(p_sb->dZoneStart+p_inode->i2*BLOCKS_PER_CLUSTER)) != 0)
    			return status;

    		p_indrefdc=soGetSngIndRefClust();

        	if(p_indrefdc->info.ref[indirref_idx]==NULL_CLUSTER)
            	return -EDCNOTIL;

            if((status=soLoadDirRefClust(p_sb->dZoneStart+p_indrefdc->info.ref[indirref_idx]*BLOCKS_PER_CLUSTER)) != 0)
            	return status;

            p_dirrefdc=soGetDirRefClust();

            if(p_dirrefdc->info.ref[dirref_idx]==NULL_CLUSTER)
            	return -EDCNOTIL;

            // Free data cluster when requested
            if(op==FREE || op == FREE_CLEAN)
            	if((status=soFreeDataCluster(p_dirrefdc->info.ref[dirref_idx])) != 0)
            		return status;

            if (op == FREE) break;	// end of operation

            // Clean Logical Cluster associated with the inode
            if((status=soCleanLogicalCluster(p_sb,nInode,p_dirrefdc->info.ref[dirref_idx]))!=0)
            	return status;

            p_dirrefdc->info.ref[dirref_idx]=NULL_CLUSTER;
            p_inode->cluCount--;

            // Save changes
            if((status=soStoreDirRefClust())!=0)
            	return status;

            // Check if everything is ok
            for(i = 0; i < RPC; i++)
            	if(p_dirrefdc->info.ref[i] != NULL_CLUSTER)
            		return 0;

            // Free indirect ref. cluster
            if((status=soFreeDataCluster(p_indrefdc->info.ref[indirref_idx]))!=0)
            	return status;

            // Clear association with inode
            if((status=soCleanLogicalCluster(p_sb,nInode,p_indrefdc->info.ref[indirref_idx]))!=0)
            	return status;

            p_indrefdc->info.ref[indirref_idx]=NULL_CLUSTER;
            p_inode->cluCount--;

            // Save changes
            if((status = soStoreSngIndRefClust()) != 0)
            	return status;

            // check if everything is ok
            for(i = 0; i < RPC; i++)
            	if(p_indrefdc->info.ref[i] != NULL_CLUSTER)
            		return 0;

            if((status = soFreeDataCluster(p_inode->i2)) != 0)
            	return status;

            if((status=soCleanLogicalCluster(p_sb,nInode,p_inode->i2))!=0)
            	return status;

            p_inode->i2 = NULL_CLUSTER;
            p_inode->cluCount--;

            break;

        default:
        	return -EINVAL;
        	break;
    }

    return 0;
}



/**
 *  \brief Attach a file data cluster whose index to the list of direct references and logical number are known.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param nLClust logical number of the data cluster
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

//	Autor: João Mota 68118
int soAttachLogicalCluster (SOSuperBlock *p_sb, uint32_t nInode, uint32_t clustInd, uint32_t nLClust)
{
	SODataClust dc;
   int stat;
   uint32_t nLClustNext,nLClustPrev;
  
   nLClustNext = nLClustPrev = NULL_CLUSTER;

  
   if((stat = soReadCacheCluster(BLOCKS_PER_CLUSTER * nLClust + p_sb->dZoneStart, &dc)) != 0){
	  return stat;
   }
   if(dc.stat != nInode){
	  return -EWGINODENB;
   }
   if(clustInd != 0){
	  if( (stat = soHandleFileCluster(nInode,clustInd-1,GET,&nLClustPrev)) != 0){
		 return stat;
	  }
	  dc.prev = nLClustPrev;
   }
   if(clustInd != MAX_FILE_CLUSTERS){
	  if((stat = soHandleFileCluster(nInode,clustInd+1,GET,&nLClustNext)) != 0){
		 return stat;
	  }
	  dc.next = nLClustNext;
   }
   
   if((stat = soWriteCacheCluster(BLOCKS_PER_CLUSTER * nLClust + p_sb->dZoneStart, &dc)) != 0 ){
	  return stat;
   }
   if(nLClustPrev != NULL_CLUSTER){
	   /* Fazer a ligação do cluster anterior ao nLClust*/
	   if((stat = soReadCacheCluster(BLOCKS_PER_CLUSTER * nLClustPrev + p_sb->dZoneStart, &dc)) != 0){
		  return stat;
	   }
	   dc.next = nLClust;
	   if((stat = soWriteCacheCluster(BLOCKS_PER_CLUSTER * nLClustPrev + p_sb->dZoneStart, &dc)) != 0){
		  return stat;
	   }
	}
	if(nLClustNext != NULL_CLUSTER){
		/* Fazer a ligação do cluster a seguir ao nLClust*/
	   if((stat = soReadCacheCluster(BLOCKS_PER_CLUSTER * nLClustNext + p_sb->dZoneStart, &dc)) != 0){
		 return stat;
	   }
	   dc.prev = nLClust;
	   if((stat = soWriteCacheCluster(BLOCKS_PER_CLUSTER * nLClustNext + p_sb->dZoneStart, &dc)) != 0){
		  return stat;
	   }
   }
   return 0;
} 

/**
 *  \brief Clean a file data cluster whose logical number is known.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param nLClust logical number of the data cluster
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

 //	Autor: João Mota 68118

int soCleanLogicalCluster (SOSuperBlock *p_sb, uint32_t nInode, uint32_t nLClust)
{
  int stat;
  uint32_t status;
  SODataClust dc;
  
  // Ler cluster de dados
  if ((stat = soReadCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER,&dc)) != 0)
	return stat;

  // Verificar o estado do cluster: não pode estar alocado
  if((stat = soQCheckStatDC(p_sb,nLClust,&status)) != 0)
	return stat;  

  if(dc.stat != nInode)
	return -EWGINODENB;

  // Corta a ligacao entre cluster e inode
  dc.stat = NULL_INODE;
 
  // Reescreve o cluster
  if((stat = soWriteCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER,&dc)) != 0)
	return stat;

  return 0;
}
