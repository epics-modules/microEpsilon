#ifndef _capaNCDT6200_PROTOCOL_H_
#define _capaNCDT6200_PROTOCOL_H_

# include <epicsTypes.h>

#define capaNCDT6200_PROTOCOL_TCP_PORT			10001
#define capaNCDT6200_MEASURING_VALUE_RANGE		16777215.0 /* 0xFFFFFF */
/* #define capaNCDT6200_MEASURING_VALUE_RANGE		16777216.0 /* 0x1000000 */

/*
 * capaNCDT6200 to IOC data packet
  */
typedef struct capaNCDT6200DataPacket {
    epicsUInt32     		preamble;
    epicsUInt32     		orderNumber;
    epicsUInt32     		serialNumber;
    unsigned long long		channelBitField;
    epicsUInt32     		status;					/* Not used by device */
    epicsUInt16     		frameNumberM;
    epicsUInt16				bytesPerFrame;
    epicsUInt32				measValueCounter;
    double					chan1MeasValue;
    double					chan2MeasValue;
    double					chan3MeasValue;
    double					chan4MeasValue;
} capaNCDT6200DataPacket;

#endif /* _capaNCDT6200_PROTOCOL_H_ */
