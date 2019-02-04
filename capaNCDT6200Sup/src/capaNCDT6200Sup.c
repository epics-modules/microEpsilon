/*
 * MicroEpsilon device support for capaNCDT6200
 *
 * Uses asynOctetSyncIO to receive messages from Unit then updates ai records.
 */

#include <string.h>
#include <initHooks.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsMutex.h>
#include <epicsExport.h>
#include <cantProceed.h>
#include <alarm.h>
#include <iocsh.h>
#include <devLib.h>
#include <asynDriver.h>
#include <asynOctet.h>
#include <asynInt32.h>
#include <asynInt32Array.h>
#include <asynFloat64.h>
#include <drvAsynIPPort.h>
#include <asynCommonSyncIO.h>
#include <asynOctetSyncIO.h>
#include "capaNCDT6200Protocol.h"

/*
 * ASYN subaddresses
 */
#define A_DISP_CHAN1     						1
#define A_DISP_CHAN2     						2
#define A_DISP_CHAN3     						3
#define A_DISP_CHAN4     						4
#define A_MEAS_RANGE_CHAN1						5
#define A_MEAS_RANGE_CHAN2						6
#define A_MEAS_RANGE_CHAN3						7
#define A_MEAS_RANGE_CHAN4						8
#define A_PV_THROTTLE							9
#define A_DATA_PACKET_GOOD_COUNT				20
#define A_DATA_PACKET_BAD_READ_COUNT        	21
#define A_DATA_PACKET_BAD_COUNT             	22
#define A_DATA_PACKET_TIMEOUT_COUNT         	23
#define A_DATA_PACKET_OUT_OF_SEQUENCE_COUNT 	24
#define A_DUPLICATE_DATA_PACKET_COUNT       	25
#define A_MISSED_DATA_PACKET_COUNT          	26
#define A_MEASURED_COUNT						27
#define A_NUM_MEAS_CHANNELS						40

/*
 * Links to lower port
 */
typedef struct portLink {
    char             *portName;
    char             *hostInfo;
    asynUser         *pasynUserCommon;
    asynUser         *pasynUserOctet;
    asynInterface    *poctet;
    int               isCommunicating;
} portLink;

/*
 * Driver private storage
 */
typedef struct drvPvt {
    /*
     * Asyn interfaces we provide
     */
    char                *portName;
    asynInterface        asynCommon;
    asynInterface        asynInt32;
    asynInterface        asynFloat64;
    void                *asynFloat64InterruptPvt;

    /*
     * Links to lower port
     */
    portLink             rbkLink;

    /*
     * Buffer storage
     */
    capaNCDT6200DataPacket  capaNCDT6200Data;
    epicsUInt32          	expectedmeasValueCounter;
    int                  	badPacketCount;

	/*
	 * Data storage
	 */
    char        cbuf[200];  /* Character stream from ASYN */

	/*
	 * Channel measuring ranges
	 */
	int			chan1MeasRange;
	int			chan2MeasRange;
	int			chan3MeasRange;
	int			chan4MeasRange;
	
    /*
     * Rate reducers
     */
    int             pvThrottle;
    int             pvThrottleCounter;
    double			dispChan1Sum;
    double			dispChan2Sum;
    double			dispChan3Sum;
    double			dispChan4Sum;
    int				dispSumCount;

    /*
     * Statistics
     */
    unsigned long        dataPacketGoodCount;
    unsigned long        dataPacketBadCount;
    unsigned long        dataPacketTimeoutCount;
    unsigned long        dataPacketBadReadCount;
    unsigned long        dataPacketOutOfSequenceCount;
    unsigned long        duplicateDataPacketCount;
    unsigned long        missedDataPacketCount;
    unsigned long		 measuredCount;
    
    /*
    * Operation Parameters
    */
    unsigned long		numMeasChansAvail;
    epicsThreadId 		readerTID;

} drvPvt;

static epicsEventId capaNCDT6200ShutdownEvent;
static volatile int capaNCDT6200ShutdownRequest;
static volatile int measCountInitialized;



/*
 * Process a data packet.
 * Runs in context of reader thread
 */
static void
processDataPacket(drvPvt *pdpvt, int isValid)
{
    capaNCDT6200DataPacket *dp = &pdpvt->capaNCDT6200Data;
    ELLLIST *pclientList;
    interruptNode *pnode;
    extern volatile int interruptAccept;
    int diff;

    /*
     * Check for missing measurement packets
     */
    if (!measCountInitialized) {			/* set counter to first read packet number */
    	pdpvt->expectedmeasValueCounter = dp->measValueCounter;
    	measCountInitialized = 1;
    }
    diff = dp->measValueCounter - pdpvt->expectedmeasValueCounter;
    if ((diff != 0) && pdpvt->rbkLink.isCommunicating) {
        if (diff == -1) {
            /*
             * Ignore duplicates.
             */
            pdpvt->duplicateDataPacketCount++;
            return;
        }
        if (diff > 0)
            pdpvt->missedDataPacketCount += diff;
        pdpvt->dataPacketOutOfSequenceCount++;
    }
    pdpvt->expectedmeasValueCounter = dp->measValueCounter + 1;

    /*
     * Wait until rest of EPICS is ready
     */
    if (!interruptAccept)
        return;

    if (isValid) {
        /*
         * 	Average incoming displacement data
         */
		pdpvt->dispChan1Sum += pdpvt->capaNCDT6200Data.chan1MeasValue;
		pdpvt->dispChan2Sum += pdpvt->capaNCDT6200Data.chan2MeasValue;
		pdpvt->dispChan3Sum += pdpvt->capaNCDT6200Data.chan3MeasValue;
		pdpvt->dispChan4Sum += pdpvt->capaNCDT6200Data.chan4MeasValue;
        pdpvt->dispSumCount++;
    }


    /*
     * Limit PV updates to specified rate
     */
    if (isValid && (--pdpvt->pvThrottleCounter > 0))
        return;
    pdpvt->pvThrottleCounter = pdpvt->pvThrottle;

    /*
     * Push data into records
     */
    pasynManager->interruptStart(pdpvt->asynFloat64InterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        asynFloat64Interrupt *float64Interrupt = pnode->drvPvt;
        double value;

        pnode = (interruptNode *)ellNext(&pnode->node);
        switch (float64Interrupt->addr) {

	        case A_DISP_CHAN1:	value = pdpvt->dispChan1Sum / (float) pdpvt->dispSumCount;
	        	break;
	        case A_DISP_CHAN2:	value = pdpvt->dispChan2Sum / (float) pdpvt->dispSumCount;
	        	break;
    	    case A_DISP_CHAN3:	value = pdpvt->dispChan3Sum / (float) pdpvt->dispSumCount;
    	    	break;
        	case A_DISP_CHAN4:	value = pdpvt->dispChan4Sum / (float) pdpvt->dispSumCount;
        		break;
	        default: continue;
        }
        float64Interrupt->pasynUser->auxStatus = isValid ? asynSuccess : asynError;
        float64Interrupt->callback(float64Interrupt->userPvt,
                                 float64Interrupt->pasynUser,
                                 value);
	}
	pasynManager->interruptEnd(pdpvt->asynFloat64InterruptPvt);
    pdpvt->dispChan1Sum = 0;
    pdpvt->dispChan2Sum = 0;
    pdpvt->dispChan3Sum = 0;
    pdpvt->dispChan4Sum = 0;
    pdpvt->dispSumCount = 0;
}

/*
 * Wait for a while, but quit early if we're shutting down
 */
static void
capaNCDT6200LongWait(void)
{
    int i;

	printf("capaNCDT6200LongWait called.\n");
    for (i = 0 ; i < 10 ; i++) {
        if (capaNCDT6200ShutdownRequest)
            return;
        epicsThreadSleep(3.0);
    }
}

/*
 * This thread soaks up data from the capaNCDT6200 unit.
 */
static void
readerThread(void *arg)
{
    drvPvt *pdpvt = (drvPvt *)arg;
/*	capaNCDT6200DataPacket *dp = &pdpvt->capaNCDT6200Data; */
    asynStatus status;
    int nread, tread;
    int eomReason, eopReached;
    int portOpen;
    epicsUInt32 disp1, disp2, disp3, disp4;
    asynOctet *pasynOctet = pdpvt->rbkLink.poctet->pinterface;
    char* preamble;
	int preambleOK;

	preamble = malloc(8);
	portOpen = 0;

/*   	pasynTrace->setTraceMask(pdpvt->rbkLink.pasynUserCommon, ASYN_TRACE_FLOW|ASYN_TRACE_ERROR); */
   	pasynTrace->setTraceMask(pdpvt->rbkLink.pasynUserCommon, ASYN_TRACE_ERROR);
    for (;;) {
	    if (!pdpvt->rbkLink.isCommunicating) {
        	status = pasynCommonSyncIO->connectDevice(pdpvt->rbkLink.pasynUserCommon);
        	if (status != asynSuccess) {
            	asynPrint(pdpvt->rbkLink.pasynUserCommon, ASYN_TRACE_ERROR,
                	        "%s can't connect port: %s\n",
                            pdpvt->rbkLink.portName,
                            pdpvt->rbkLink.pasynUserCommon->errorMessage);
/*                printf("%s can't connect port: %s\n", pdpvt->rbkLink.portName,
                            pdpvt->rbkLink.pasynUserCommon->errorMessage); */
            	capaNCDT6200LongWait();
/*            	return; */
        	}
        	else {
        		portOpen = 1;
        		asynPrint(pdpvt->rbkLink.pasynUserCommon, ASYN_TRACE_ERROR,
        				"%s Successfully connected to MicroEpsilon port: %s.\n",
        				pdpvt->rbkLink.portName,
        				pdpvt->rbkLink.pasynUserCommon->errorMessage);
        	}
        	if (capaNCDT6200ShutdownRequest)
            return;
        }
 
		/*
		 * Under normal circumstances we'll never get close to the timeout.
		 * We can't block forever in the read since then we'd have no way
		 * to try to reconnect to an capaNCDT6200 that had reset for some reason.
		 */
		if (portOpen) {					/* Are we connected to MicroEpsilon unit? */
			eopReached = 0;				/* reset end of packet boolean */
		}
		else {
			printf("Micro-Epsilon unit port is not open.\n");
		}
		nread = 0;
		tread = 0;
		while (!eopReached) {
			pasynManager->lockPort(pdpvt->rbkLink.pasynUserOctet);
			status = pasynOctet->read(pdpvt->rbkLink.poctet->drvPvt,
									  pdpvt->rbkLink.pasynUserOctet,
									  pdpvt->cbuf + tread,
									  sizeof(pdpvt->cbuf),
									  &nread,
									  &eomReason);
			pasynManager->unlockPort(pdpvt->rbkLink.pasynUserOctet);
			if (capaNCDT6200ShutdownRequest)
				return;
			if (status == asynSuccess) {
				tread = tread + nread;
/*				if (tread >=48) { */
				if (tread >= (32 + pdpvt->numMeasChansAvail*4)) {
					eopReached = 1;
					disp1 = disp2 = disp3 = disp4 = 0.0;
					pdpvt->capaNCDT6200Data.chan1MeasValue = 0.0;
					pdpvt->capaNCDT6200Data.chan2MeasValue = 0.0;
					pdpvt->capaNCDT6200Data.chan3MeasValue = 0.0;
					pdpvt->capaNCDT6200Data.chan4MeasValue = 0.0;
					
					pdpvt->capaNCDT6200Data.preamble = (pdpvt->cbuf[0] & 0xFF) |
							((pdpvt->cbuf[1] & 0xFF) << 8) | ((pdpvt->cbuf[2] & 0xFF) << 16) |
							((pdpvt->cbuf[3] & 0xFF) << 24);
					pdpvt->capaNCDT6200Data.orderNumber = (pdpvt->cbuf[4] & 0xFF) |
							((pdpvt->cbuf[5] & 0xFF) << 8) | ((pdpvt->cbuf[6] & 0xFF) << 16) |
						 	((pdpvt->cbuf[7] & 0xFF) << 24);
					pdpvt->capaNCDT6200Data.serialNumber = (pdpvt->cbuf[8] & 0xFF) |
							((pdpvt->cbuf[9] & 0xFF) << 8) | ((pdpvt->cbuf[10] & 0xFF) << 16) |
							((pdpvt->cbuf[11] & 0xFF) << 24);
					pdpvt->capaNCDT6200Data.channelBitField = (pdpvt->cbuf[12] & 0xFF) |
							((pdpvt->cbuf[13] & 0xFF) << 8) | ((pdpvt->cbuf[14] & 0xFF) << 16) |
							((pdpvt->cbuf[15] & 0xFF) << 24) | ((pdpvt->cbuf[16] & 0xFF) << 32) |
							((pdpvt->cbuf[17] & 0xFF) << 40) | ((pdpvt->cbuf[18] & 0xFF) << 48) |
							((pdpvt->cbuf[19] & 0xFF) << 56);
					pdpvt->capaNCDT6200Data.status = (pdpvt->cbuf[20] & 0xFF) |
							((pdpvt->cbuf[21] & 0xFF) << 8) | ((pdpvt->cbuf[22] & 0xFF) << 16) |
							((pdpvt->cbuf[23] & 0xFF) << 24);
					pdpvt->capaNCDT6200Data.frameNumberM = (pdpvt->cbuf[24] & 0xFF) |
							((pdpvt->cbuf[25] & 0xFF) << 8);
					pdpvt->capaNCDT6200Data.bytesPerFrame = (pdpvt->cbuf[26] & 0xFF) |
							((pdpvt->cbuf[27] & 0xFF) << 8);
					pdpvt->capaNCDT6200Data.measValueCounter = (pdpvt->cbuf[28] & 0xFF) |
							((pdpvt->cbuf[29] & 0xFF) << 8) | ((pdpvt->cbuf[30] & 0xFF) << 16) |
							((pdpvt->cbuf[31] & 0xFF) << 24);

					/* Check if Channel #1 is available */
					if (pdpvt->capaNCDT6200Data.channelBitField & 1) {
						disp1 = ((pdpvt->cbuf[32] & 0xFF) | ((pdpvt->cbuf[33] & 0xFF) << 8) |
							((pdpvt->cbuf[34] & 0xFF) << 16) |
							((pdpvt->cbuf[35] & 0xFF) << 24)) & 0xFFFFFFFF;
						pdpvt->capaNCDT6200Data.chan1MeasValue = (disp1 *
							(float) pdpvt->chan1MeasRange) / 
							capaNCDT6200_MEASURING_VALUE_RANGE;
					}
					
					/* Check if Channel #2 is available */
					if (pdpvt->capaNCDT6200Data.channelBitField & 5) {
						disp2 = ((pdpvt->cbuf[36] & 0xFF) | ((pdpvt->cbuf[37] & 0xFF) << 8) |
							((pdpvt->cbuf[38] & 0xFF) << 16) |
							((pdpvt->cbuf[39] & 0xFF) << 24)) & 0xFFFFFF;
						pdpvt->capaNCDT6200Data.chan2MeasValue = (disp2 *
							(float) pdpvt->chan2MeasRange) /
							capaNCDT6200_MEASURING_VALUE_RANGE;
					}
					
					/* Check if Channel #3 is available */					
					if (pdpvt->capaNCDT6200Data.channelBitField & 21) {
						disp3 = ((pdpvt->cbuf[40] & 0xFF) | ((pdpvt->cbuf[41] & 0xFF) << 8) |
							((pdpvt->cbuf[42] & 0xFF) << 16) |
							((pdpvt->cbuf[43] & 0xFF) << 24)) & 0xFFFFFF;
						pdpvt->capaNCDT6200Data.chan3MeasValue = (disp3 *
							(float) pdpvt->chan3MeasRange) /
							capaNCDT6200_MEASURING_VALUE_RANGE;
					}
					
					/* Check if Channel #4 is available */
					if (pdpvt->capaNCDT6200Data.channelBitField & 85) {
						disp4 = ((pdpvt->cbuf[44] & 0xFF) | ((pdpvt->cbuf[45] & 0xFF) << 8) |
							((pdpvt->cbuf[46] & 0xFF) << 16) |
							((pdpvt->cbuf[47] & 0xFF) << 24)) & 0xFFFFFF;
						pdpvt->capaNCDT6200Data.chan4MeasValue = (disp4 *
							(float) pdpvt->chan4MeasRange) /
							capaNCDT6200_MEASURING_VALUE_RANGE;
					}

					sprintf(preamble, "%c%c%c%c", pdpvt->cbuf[3], pdpvt->cbuf[2],
							pdpvt->cbuf[1], pdpvt->cbuf[0]);
					preambleOK = strcmp(preamble, "MEAS");
				}
			}
			if (status == asynSuccess) {
					if (eopReached & (preambleOK == 0)) {
						/* Process a valid data packet */
						pdpvt->dataPacketGoodCount++;
						processDataPacket(pdpvt, 1);
						pdpvt->badPacketCount = 0;
					}
					if (eopReached & (preambleOK != 0)) {
						pdpvt->dataPacketBadCount++;
						pdpvt->badPacketCount++;
						asynPrint(pdpvt->rbkLink.pasynUserOctet, ASYN_TRACE_ERROR,
                                          "%s: Could not find correct Preamble string: %s\n",
                                               pdpvt->rbkLink.portName,
                                               pdpvt->rbkLink.pasynUserCommon->errorMessage);
					}
			}
			else {
				asynPrint(pdpvt->rbkLink.pasynUserOctet, ASYN_TRACE_ERROR,
							"%s: port read failed: %s\n", pdpvt->rbkLink.portName,
							pdpvt->rbkLink.pasynUserOctet->errorMessage);
				pdpvt->badPacketCount++;
				if (status == asynTimeout) {
					pdpvt->dataPacketTimeoutCount++;
					pdpvt->rbkLink.pasynUserOctet->timeout = 30.0;
				}
				else {
					capaNCDT6200LongWait();
					if (capaNCDT6200ShutdownRequest)
						return;
					pdpvt->dataPacketBadReadCount++;
				}
			}
			if (pdpvt->badPacketCount == 0) {
				pdpvt->rbkLink.isCommunicating = 1;
				pdpvt->rbkLink.pasynUserOctet->timeout = 5.0;
			}	
			else if (pdpvt->badPacketCount >= 2) {
				pdpvt->rbkLink.isCommunicating = 0;
				 /* Process SCAN=I/O Intr records to set STAT/SEVR. */
				printf("Process SCAN=I/O Intr records to set STAT/SEVR.\n");
				processDataPacket(pdpvt, 0);
			}
			if (!pdpvt->rbkLink.isCommunicating) {
        		status = pasynCommonSyncIO->disconnectDevice(pdpvt->rbkLink.pasynUserCommon);
        		if (status != asynSuccess) {
            		asynPrint(pdpvt->rbkLink.pasynUserCommon, ASYN_TRACE_ERROR,
                        "%s can't disconnect: %s\n",
                            pdpvt->rbkLink.portName,
                            pdpvt->rbkLink.pasynUserCommon->errorMessage);
                }
                else {
                	eopReached = 1;
                	portOpen = 0;
        			printf("Successfully disconnected MicroEpsilon port: %s.\n",
        				pdpvt->rbkLink.portName);
        			capaNCDT6200LongWait();
        		}
		    }
    	}
    }
}

/*
 * Create a new lower port and set up a link to it
 */
static asynStatus
setLink(portLink *link, drvPvt *pdpvt, const char *ext, const char *hostInfo, int priority)
{
    asynStatus status;

    link->portName = callocMustSucceed(1, strlen(pdpvt->portName)+strlen(ext)+1, "dpscConfigure");
    sprintf(link->portName, "%s%s", pdpvt->portName, ext);
    link->hostInfo = epicsStrDup(hostInfo);
    /* No autoconnect, No process EOS */
    drvAsynIPPortConfigure(link->portName, link->hostInfo, priority, 1, 1);
    status = pasynCommonSyncIO->connect(link->portName, -1,
                                       &link->pasynUserCommon, NULL);
    if (status != asynSuccess) {
        printf("Can't set asynCommonSyncIO for port \"%s\".\n", link->portName);
        return asynError;
    }
    link->pasynUserOctet= pasynManager->createAsynUser(NULL, NULL);
    status = pasynManager->connectDevice(link->pasynUserOctet, link->portName, -1);
    if (status != asynSuccess) {
        printf("Can't find asyn port \"%s\".\n", link->portName);
        return asynError;
    }
    link->poctet = pasynManager->findInterface(link->pasynUserOctet, asynOctetType, 0);
    if (link->poctet == NULL) {
        printf("Can't find octet interface for \"%s\".\n", link->portName);
        return asynError;
    }
    link->pasynUserOctet->timeout = 5.0;
    return asynSuccess;
}

static void
capaNCDT6200Shutdown(void *arg)
{
    capaNCDT6200ShutdownRequest = 1;
    epicsEventSignal(capaNCDT6200ShutdownEvent);
}

/*
 * We want the capaNCDT6200Shutdown to be one of the earlier atExit callbacks, so
 * register it after all capaNCDT6200Configure commands are sure to have been done.
 */
static void
capaNCDT6200ShutdownSetup(initHookState state)
{
    static int beenHere;
    
    if (!beenHere && (state == initHookAfterIocRunning)) {
        epicsAtExit(capaNCDT6200Shutdown, NULL);
        beenHere = 1;
    }
}

/*
 * asynCommon methods
 */
static void
report(void *pvt, FILE *fp, int details)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    fprintf(fp, " capaNCDT6200 RBK %scommunicating\n", pdpvt->rbkLink.isCommunicating ? "" : "NOT ");
}

static asynStatus
connect(void *pvt, asynUser *pasynUser)
{
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus
disconnect(void *pvt, asynUser *pasynUser)
{
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}
static asynCommon commonMethods = { report, connect, disconnect };

/*
 * asynFloat64 methods
 */
/* static asynStatus
float64Read(void *pvt, asynUser *pasynUser, epicsFloat64 *value)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    asynStatus status;
    int address;

    if ((status = pasynManager->getAddr(pasynUser, &address)) != asynSuccess)
        return status;

    if (!pdpvt->rbkLink.isCommunicating) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                   "Readbacks not available");
        return asynError;
    }
} */

static asynFloat64 float64Methods = { }; /* All records are SCAN=I/O Intr */

/*
 * asynInt32 methods
 */
static asynStatus
int32Write(void *pvt, asynUser *pasynUser, epicsInt32 value)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    asynStatus status;
    int address;
    unsigned int readerThreadPriority;
    int priority;

    if ((status = pasynManager->getAddr(pasynUser, &address)) != asynSuccess)
        return status;

    switch (address) {

		case A_MEAS_RANGE_CHAN1:	pdpvt->chan1MeasRange = value;
									return asynSuccess;
	    case A_MEAS_RANGE_CHAN2:	pdpvt->chan2MeasRange = value;
	    							return asynSuccess;
    	case A_MEAS_RANGE_CHAN3:	pdpvt->chan3MeasRange = value;
							    	return asynSuccess;
        case A_MEAS_RANGE_CHAN4:	pdpvt->chan4MeasRange = value;
							        return asynSuccess;
		case A_PV_THROTTLE:			pdpvt->pvThrottle = value;
									return asynSuccess;
		case A_NUM_MEAS_CHANNELS:	pdpvt->numMeasChansAvail = value;
						if (pdpvt->readerTID == 0) {
							/*
							* Start the reader thread.
							*/
	    					epicsThreadLowestPriorityLevelAbove(priority, &readerThreadPriority);
    						pdpvt->readerTID = epicsThreadCreate(pdpvt->portName,
                            						readerThreadPriority,
                            						epicsThreadGetStackSize(epicsThreadStackMedium),
						                            readerThread,
						                            pdpvt);
						    if (!pdpvt->readerTID) {
						        printf("Can't set up %s reader thread!\n", pdpvt->portName);
						        return asynError;
							}
						}
						return asynSuccess;

	    default:
		    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                   "Bad int32 write address");
    	    return asynError;
    }
}

static asynStatus
int32ReadBadAddress(asynUser *pasynUser)
{
    asynStatus status;
    int address;

    if ((status = pasynManager->getAddr(pasynUser, &address)) != asynSuccess)
        return status;
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                       "Bad int32 read address %d", address);
    return asynError;
}

/*
 * Input records and initial readback of output records
 */
static asynStatus
int32Read(void *pvt, asynUser *pasynUser, epicsInt32 *value)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    asynStatus status;
    int address;

    if ((status = pasynManager->getAddr(pasynUser, &address)) != asynSuccess)
        return status;
    if (!pdpvt->rbkLink.isCommunicating && (address <= 0)) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                   "Readbacks not available");
        return asynError;
    }
    switch (address) {
        case A_DATA_PACKET_GOOD_COUNT:
            *value = pdpvt->dataPacketGoodCount;
            return asynSuccess;
        case A_DATA_PACKET_BAD_READ_COUNT:
            *value = pdpvt->dataPacketBadReadCount;
            return asynSuccess;
        case A_DATA_PACKET_BAD_COUNT:
            *value = pdpvt->dataPacketBadCount;
            return asynSuccess;
        case A_DATA_PACKET_TIMEOUT_COUNT:
            *value = pdpvt->dataPacketTimeoutCount;
            return asynSuccess;
        case A_DATA_PACKET_OUT_OF_SEQUENCE_COUNT:
            *value = pdpvt->dataPacketOutOfSequenceCount;
            return asynSuccess;
        case  A_DUPLICATE_DATA_PACKET_COUNT:
            *value = pdpvt->duplicateDataPacketCount;
            return asynSuccess;
        case  A_MISSED_DATA_PACKET_COUNT:
            *value = pdpvt->missedDataPacketCount;
            return asynSuccess;
        case  A_MEASURED_COUNT:
        	*value = pdpvt->capaNCDT6200Data.measValueCounter;
        	return asynSuccess;
        default:
            return int32ReadBadAddress(pasynUser);
    }
}

static asynInt32 int32Methods = { int32Write, int32Read };

static void
capaNCDT6200Configure(const char *portName, const char *IPaddress, const char *IPport)
{
    drvPvt *pdpvt;
    asynStatus status;
/*    unsigned int readerThreadPriority;
    epicsThreadId tid; */
    char *host;
    int IPaddressLen;
    int priority;
    static int beenHere;

    /*
     * Handle defaults
     */
    priority = epicsThreadPriorityMedium;
    if ((portName == NULL) || (*portName == '\0')
      || (IPaddress == NULL) || (*IPaddress == '\0')
      || (IPport == NULL) || (*IPport == '\0')) {
			printf("Required argument not present\n");
        	return;
    }
    
    IPaddressLen = strlen(IPaddress);
    host = (char *)callocMustSucceed(1, IPaddressLen + 15, "capaNCDT6200Conf");
    sprintf(host, "%s:%d TCP", IPaddress, capaNCDT6200_PROTOCOL_TCP_PORT);

    /*
     * Set up local storage
     */
    pdpvt = (drvPvt *)callocMustSucceed(1, sizeof(drvPvt), "capaNCDT6200Conf");
    pdpvt->portName = epicsStrDup(portName);
    pdpvt->pvThrottle = 5;

    if (!beenHere) {
        capaNCDT6200ShutdownEvent = epicsEventMustCreate(epicsEventEmpty);
        initHookRegister(capaNCDT6200ShutdownSetup);
        beenHere = 1;
    }

    /*
     * Create the port that we'll use for the data stream.
     */
    status = setLink(&pdpvt->rbkLink, pdpvt, "_RBK", host, priority);
    if (status != asynSuccess)
        return;
    /*
     * Create our port
     */
    status = pasynManager->registerPort(pdpvt->portName,
                                        ASYN_CANBLOCK|ASYN_MULTIDEVICE,
                                        1, 0, 0);
    if(status != asynSuccess) {
        printf("registerPort failed\n");
        return;
    }
    pdpvt->asynCommon.interfaceType = asynCommonType;
    pdpvt->asynCommon.pinterface  = &commonMethods;
    pdpvt->asynCommon.drvPvt = pdpvt;
    status = pasynManager->registerInterface(pdpvt->portName, &pdpvt->asynCommon);
    if (status != asynSuccess) {
        printf("registerInterface failed\n");
        return;
    }

    pdpvt->asynFloat64.interfaceType = asynFloat64Type;
    pdpvt->asynFloat64.pinterface  = &float64Methods;
    pdpvt->asynFloat64.drvPvt = pdpvt;
    status = pasynFloat64Base->initialize(pdpvt->portName, &pdpvt->asynFloat64);
    if (status != asynSuccess) {
        printf("pasynFloat64Base->initialize failed\n");
        return;
    }
    pasynManager->registerInterruptSource(pdpvt->portName, &pdpvt->asynFloat64,
                                                &pdpvt->asynFloat64InterruptPvt);

    pdpvt->asynInt32.interfaceType = asynInt32Type;
    pdpvt->asynInt32.pinterface  = &int32Methods;
    pdpvt->asynInt32.drvPvt = pdpvt;
    status = pasynInt32Base->initialize(pdpvt->portName, &pdpvt->asynInt32);
    if (status != asynSuccess) {
        printf("pasynInt32Base->initialize failed\n");
        return;
    }

    /*
     * Start the reader thread.
     * Run the reader thread just above ASYN port thread.
     *//*
    epicsThreadLowestPriorityLevelAbove(priority, &readerThreadPriority);
    tid = epicsThreadCreate(portName,
                            readerThreadPriority,
                            epicsThreadGetStackSize(epicsThreadStackMedium),
                            readerThread,
                            pdpvt);
    if (!tid) {
        printf("Can't set up %s reader thread!\n", portName);
        return;
    } */
    measCountInitialized = 0;		/* Set flag to grab initial measurement counter value */
    pdpvt->dataPacketGoodCount = 0;
    pdpvt->dataPacketBadCount = 0;
    pdpvt->dataPacketTimeoutCount = 0;
    pdpvt->dataPacketBadReadCount = 0;
    pdpvt->dataPacketOutOfSequenceCount = 0;
    pdpvt->duplicateDataPacketCount = 0;
    pdpvt->missedDataPacketCount = 0;
    pdpvt->measuredCount = 0;
    printf("capaNCDT6200:  Data port created -> host = %s\n", host);
}

/*
 * IOC shell command registration
 */
static const iocshArg capaNCDT6200ConfigureArg0 = { "port",			iocshArgString};
static const iocshArg capaNCDT6200ConfigureArg1 = { "IPaddress",	iocshArgString};
static const iocshArg capaNCDT6200ConfigureArg2 = { "IPport",		iocshArgString};
static const iocshArg *capaNCDT6200ConfigureArgs[] = {
                    &capaNCDT6200ConfigureArg0, &capaNCDT6200ConfigureArg1,
                    &capaNCDT6200ConfigureArg2};
static const iocshFuncDef capaNCDT6200ConfigureFuncDef =
                                          {"capaNCDT6200Configure", 3, capaNCDT6200ConfigureArgs};
static void capaNCDT6200ConfigureCallFunc(const iocshArgBuf *args)
{
    capaNCDT6200Configure(args[0].sval, args[1].sval, args[2].sval);
}

static void
capaNCDT6200_RegisterCommands(void)
{
    iocshRegister(&capaNCDT6200ConfigureFuncDef, capaNCDT6200ConfigureCallFunc);
}
epicsExportRegistrar(capaNCDT6200_RegisterCommands);