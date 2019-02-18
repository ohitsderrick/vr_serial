/*
Derrick Simeon
Feb 16, 2019
*/

#include "vr_serial.h"
#include "fancy_print.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static VrSerPkt_t *_createPkt(const void *pSrc, int nChars);
static void _deletePkt(VrSerPkt_t **ppPkt);
static int _consumePktData(VrSerPkt_t *pPkt, int nChars);
static VrSerPkt_t *_xferPktData(VrSerPkt_t **ppPkt, int nChars);

static VrSerPkt_t *_createPkt(const void *pSrc, int nChars)
{
  VrSerPkt_t *pPkt;
  
  assert((pSrc != NULL) && (nChars > 0));
  
  // Allocate memory and copy data.
  pPkt = malloc(sizeof(*pPkt));
  pPkt->pData = malloc(nChars);
  pPkt->nChars = nChars;
  memcpy(pPkt->pData, pSrc, nChars);
  
  return pPkt;
}

static void _deletePkt(VrSerPkt_t **ppPkt)
{
  VrSerPkt_t *pObsPkt;
  
  assert(ppPkt != NULL);
  
  // Remove packet from the list and deallocate.
  pObsPkt = *ppPkt;
  *ppPkt = (*ppPkt)->pNext;
  free(pObsPkt);
}

static int _consumePktData(VrSerPkt_t *pPkt, int nChars)
{
  void *pObsData;
  int nConsume;
  
  assert(pPkt != NULL);
  
  // Determine how many chars to consume.
  nConsume = (nChars <= pPkt->nChars) ? nChars : pPkt->nChars;
  
  // Consume the data.
  pObsData = pPkt->pData;
  pPkt->nChars -= nConsume;
  
  if(pPkt->nChars > 0)
  {
    // Allocate smaller memory section.
    pPkt->pData = malloc(nConsume);
    memcpy(pPkt->pData, &((char *) pObsData)[nConsume],
      pPkt->nChars);  
  }
  else
  {
    pPkt->pData = NULL;
  }
  
  // Free the obsolete buffer.
  free(pObsData);
  
  return nConsume;
}

static VrSerPkt_t *_xferPktData(VrSerPkt_t **ppPkt, int nChars)
{
  VrSerPkt_t *pNewPkt;
  
  assert(ppPkt != NULL);
  
  if(nChars == 0)
  {
    pNewPkt = NULL;
  }
  else if(nChars >= (*ppPkt)->nChars)
  {
    // Transfer entire packet.
    pNewPkt = *ppPkt;

    // Remove from original queue.
    *ppPkt = (*ppPkt)->pNext;
    
    // Recursive call.
    pNewPkt->pNext = _xferPktData(ppPkt, nChars - pNewPkt->nChars);
  }
  else
  {
    // Transfer portion of the packet.
    pNewPkt = _createPkt((*ppPkt)->pData, nChars);
    _consumePktData(*ppPkt, nChars);
  }
  
  return pNewPkt;
}  

VrSerDev_t *VR_SERIAL_initDev(const char *pName, int bps)
{
  VrSerDev_t *pDev;
  
  pDev = malloc(sizeof(*pDev));
  pDev->pName = pName;
  pDev->bps = bps;
  pDev->pTxQ = NULL;
  pDev->pRxQ = NULL;
  pDev->pNext = NULL;
  
  return pDev;
}

int VR_SERIAL_devTx(VrSerDev_t *pDev, const void *pSrc, int nChars)
{
  VrSerPkt_t **ppPktItr;
  
  ppPktItr = &pDev->pTxQ;
  
  while(*ppPktItr != NULL)
  {
    ppPktItr = &(*ppPktItr)->pNext;
  }
  
  *ppPktItr = _createPkt(pSrc, nChars);

  char pStr[512];
  sprintf(pStr, "%s tx %i bytes: %s", pDev->pName, (*ppPktItr)->nChars,
    (*ppPktItr)->pData);
  printTimestamped(pStr);
  
  return nChars;
}

VrSerLine_t *VR_SERIAL_initLine(void)
{
  VrSerLine_t *pLine;
  
  pLine = malloc(sizeof(*pLine));
  pLine->pDevList = NULL;
  
  return pLine;
}

void VR_SERIAL_addDev(VrSerLine_t *pSerLine, VrSerDev_t *pSerDev)
{
  VrSerDev_t **ppDevItr;
  
  ppDevItr = &pSerLine->pDevList;
  
  while(*ppDevItr != NULL)
  {
    ppDevItr = &(*ppDevItr)->pNext;
  }
  
  *ppDevItr = pSerDev;
}

int _passPktString(int (*pfnPrint)(char *), VrSerPkt_t *pPkt)
{
  VrSerPkt_t *pPktItr;
  char *pStr;
  int nChars = 0, nCopy = 0;
    
  for(pPktItr = pPkt; pPktItr != NULL; pPktItr = pPktItr->pNext)
  {
    nChars += pPktItr->nChars;
  }
  
  pStr = malloc(nChars + 1);
  
  for(pPktItr = pPkt; pPktItr != NULL; pPktItr = pPktItr->pNext)
  {
    memcpy(&pStr[nCopy], pPktItr->pData, pPktItr->nChars);
    nCopy += pPktItr->nChars;
  }
  
  // Terminate with NULL.
  pStr[nCopy] = 0;
  
  return pfnPrint(pStr);
}

void VR_SERIAL_procLineTask(VrSerLine_t *pLine, float seconds)
{
  VrSerDev_t **ppDevItr;
  int nChars;
  
  for(ppDevItr = &pLine->pDevList; *ppDevItr != NULL;
    ppDevItr = &(*ppDevItr)->pNext)
  {
    // Determine number of chars to Tx.
    nChars = (*ppDevItr)->bps * seconds;

    _passPktString(&printTimestamped, (*ppDevItr)->pTxQ);
    
    char pStr[64];
    sprintf(pStr, "tx %u bytes over line", nChars);
    printTimestamped(pStr);
    
    // Transfer from Tx queue to Rx queue.
    (*ppDevItr)->pRxQ = _xferPktData(&(*ppDevItr)->pTxQ, nChars);

    _passPktString(&printTimestamped, (*ppDevItr)->pTxQ);
    _passPktString(&printTimestamped, (*ppDevItr)->pRxQ);
    
//     sprintf(buf, "%s tx %i bytes", (*ppDevItr)->pName, nChars);
//     printTimestamped(buf);    
//     passHexString(&printTimestamped, buf, nChars);
  }
}