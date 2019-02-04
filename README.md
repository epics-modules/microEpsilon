microEpsilon Device Support Overview
====================================

This support module has been developed and tested with the capaNCDT6200 units from Micro-Epsilon using EPICS R3.14.12.3 and R3.14.12.5.  The device support uses a combination of Asyn and StreamDevice to communicate with the hardware.  The Micro-Epsilon hardware sends unsolicited data on port 10001 at a configurable rate, this data is handled by using AsynOctetSyncIO in the driver.  There is "throttling" built into the driver which will average data coming in at a faster rate and post updates to EPICS at a slower rate.  The Micro-Epsilon hardware accepts commands for configuration settings on port 23, this part of the driver uses StreamDevice for communication.

The current driver supports two different hardware operating modes via PV's in the databases.  One mode is using the capaNCDT6200 unit for a displacement measurement.  While the other mode is using the capaNCDT6200 unit in hydrostatic measurements.
