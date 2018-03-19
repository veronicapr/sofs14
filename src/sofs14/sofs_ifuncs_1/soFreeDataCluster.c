/**
 *  \file soFreeDataCluster.c (implementation file)
 *
 *  \author Miguel Jesus(1ª) e Rui Oliveira(2ª)
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
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

/* Allusion to internal function */

int soDeplete (SOSuperBlock *p_sb);

/**
 *  \brief Free the referenced data cluster.
 *
 *  The cluster is inserted into the insertion cache of free data cluster references. If the cache is full, it has to be
 *  depleted before the insertion may take place. The data cluster should be put in the dirty state (the <tt>stat</tt>
 *  of the header should remain as it is), the other fields of the header, <tt>prev</tt> and <tt>next</tt>, should be
 *  put to NULL_CLUSTER. The only consistency check to carry out at this stage is to check if the data cluster was
 *  allocated.
 *
 *  Notice that the first data cluster, supposed to belong to the file system root directory, can never be freed.
 *
 *  \param nClust logical number of the data cluster
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, the <em>data cluster number</em> is out of range or the data cluster is not allocated
 *  \return -\c EDCNALINVAL, if the data cluster has not been previously allocated
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soFreeDataCluster (uint32_t nClust)
{
  soColorProbe (614, "07;33", "soFreeDataCluster (%"PRIu32")\n", nClust);

  SOSuperBlock *p_sb;
  SODataClust  dc;
  int stat;
  uint32_t clustFirstBlock;
  uint32_t dc_stat;

  //Ler super bloco e verificar erros
  if((stat = soLoadSuperBlock()) != 0) return stat;
  
  p_sb = soGetSuperBlock();
  
  //Verificar se o cluster se encontra alocado
  if((stat = soQCheckStatDC(p_sb, nClust, &dc_stat)) != 0)  return stat;
    
  //Verificar se o cluster existe a partir do numero logico
  if(nClust == 0 || nClust >= p_sb->dZoneTotal) 
    return -EINVAL; 

  if(dc_stat == FREE_CLT) return -EDCNALINVAL;

  /* verificar consistecia do header do cluster de dados*/
  if((stat = soQCheckDZ(p_sb)) != 0)
    return stat;
    if((stat = soQCheckSuperBlock(p_sb)) != 0)
    return stat;
  
  //Cálculo do número físico do primeiro bloco do cluster pretendido
  clustFirstBlock = p_sb->dZoneStart + nClust * BLOCKS_PER_CLUSTER;
		
  //Leitura do cluster com verificação de erros 
  if((stat = soReadCacheCluster(clustFirstBlock, &dc)) != 0) return stat;
  
  dc.prev = NULL_CLUSTER;
  dc.next = NULL_CLUSTER;

  //Escrita do cluster com verificação de erros
  if((stat = soWriteCacheCluster(clustFirstBlock, &dc)) != 0) return stat;

  //Verifica se a cache de inserção está cheia, se estiver, esvazia-se
  if(p_sb->dZoneInsert.cacheIdx == DZONE_CACHE_SIZE){
    
    if((stat = soDeplete(p_sb)) != 0){
      return stat;
    }
   }

   if((stat = soLoadSuperBlock()) != 0)
      return stat;
    p_sb = soGetSuperBlock();

  //Colocar cluster na cache e incremento do cache_idx
  p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx] = nClust;
  p_sb->dZoneInsert.cacheIdx++;
  p_sb->dZoneFree++;

  if((stat = soQCheckStatDC(p_sb, nClust, &dc_stat))!= 0)
    return stat;

  //Escrever alteraçoes feitas anteriormente para o super bloco 
  if((stat = soStoreSuperBlock()) != 0) return stat;

  return 0;
}

/**
 *  \brief Deplete the insertion cache of free data cluster references.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soDeplete (SOSuperBlock *p_sb){
	/* insert your code here */

	SODataClust dcluster;
	uint32_t NFClt;   /*data cluster physical position*/
	int err;
	unsigned int i;  /*usado no ciclo for */

	err = soQCheckSuperBlock(p_sb);
  if(err != 0) return err;

  /* verificar se ha tabela de clusters na lista ligada
   * se sim ligar primeira posição da cache dZoneInsert para dTail.next
   */
	if (p_sb->dTail != NULL_CLUSTER){
		/*pagina 8 notas de sofs14*/
		NFClt = p_sb->dZoneStart + p_sb->dTail * BLOCKS_PER_CLUSTER; 

		/*Read a cluster of data from the buffercache.*/	
		err  = soReadCacheCluster(NFClt, &dcluster);
		if(err !=0) return err; /*0 (zero), on success*/

		dcluster.next = p_sb->dZoneInsert.cache[0];

		/*Write a cluster of data to the buffercache.*/
		err = soWriteCacheCluster(NFClt, &dcluster);
		if(err !=0) return err; /*0 (zero), on success*/

	}

	/* percorrendo a cache de insercao*/
	for ( i = 0; i < p_sb->dZoneInsert.cacheIdx; i++){
		/*para a primeira posicao da cache*/
		if(i ==0){
			NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[0] * BLOCKS_PER_CLUSTER;
            
      err  = soReadCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
            
      dcluster.prev = p_sb->dTail;

      err  = soWriteCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
		}

    else {
      NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[i] * BLOCKS_PER_CLUSTER;
            
      err  = soReadCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
            
      dcluster.prev = p_sb->dZoneInsert.cache[i - 1];
            
			err  = soWriteCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
    }

    /*para todas as posições de cache, excepto para a primeira e última*/
    if (i != (p_sb->dZoneInsert.cacheIdx-1)){
      NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[i] * BLOCKS_PER_CLUSTER;
            
      err  = soReadCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
            
      dcluster.next = p_sb->dZoneInsert.cache[i + 1];
            
			err  = soWriteCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
    }
    else{ /*para a última posição da cache*/
      NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx-1] * BLOCKS_PER_CLUSTER;
           /* ???*/ 
      err  = soReadCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
            
      dcluster.next = NULL_CLUSTER;
            
			err  = soWriteCacheCluster(NFClt, &dcluster);
			if(err !=0)
        return err; /*0 (zero), on success*/
    } 
        /*colocar toda a cache de insercao a NULL*/
       /* p_sb->dZoneInsert.cache[i] = NULL_CLUSTER;*/

  } //end for 

	/*err = soStoreBlockInT();
	if(err !=0)
    return err;*/

	/* actualizacao do indice (insercao) da tabela de cluster livres */
	p_sb->dTail = p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx-1];

	if (p_sb->dHead == NULL_CLUSTER){
    p_sb->dHead = p_sb->dZoneInsert.cache[0];
  }

    for(i = 0; i < p_sb->dZoneInsert.cacheIdx;i++)
      p_sb->dZoneInsert.cache[i] = NULL_CLUSTER;
    /*reset do indice da cache de insercao */
  p_sb->dZoneInsert.cacheIdx = 0;

/*salvar todas as alterações no SB*/
  err  = soStoreSuperBlock();
  if(err !=0)
    return err; /*0 (zero), on success*/
	
  return 0; 
}
