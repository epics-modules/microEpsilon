#!../../bin/linux-x86_64/mms

< envPaths

epicsEnvSet("STREAM_PROTOCOL_PATH","${TOP}/db")
epicsEnvSet(P,"$(P=MMS:S27:)")

cd ${TOP}

## Register all support components
dbLoadDatabase "dbd/mms.dbd"
mms_registerRecordDeviceDriver pdbbase


# Configure Asyn TCP port for eDrive Temp laser control system - laser room
# drvAsynIPPortConfigure(portName, hostInfo, priority, noAutoConnect, noProcessEos)
# modbusInterposeConfig(portName,linkType,timeoutMsec)
# drvModbusAsynConfigure(portName,tcpPortName,slaveAddress,modbusFunction,modbusStartAddress,modbusLenght,dataType,pollMsec,plcType)

#drvAsynIPPortConfigure("rtds27","10.6.28.xxx:502",0,1,1)
drvAsynIPPortConfigure("rtds27","164.54.1.14:502",0,1,1)
modbusInterposeConfig("rtds27",0,2000)
drvModbusAsynConfigure("ch0rtd","rtds27",1,4,0,1,1,100,"Temp")
drvModbusAsynConfigure("ch1rtd","rtds27",1,4,1,1,1,100,"Temp")
drvModbusAsynConfigure("ch2rtd","rtds27",1,4,2,1,1,100,"Temp")
drvModbusAsynConfigure("ch3rtd","rtds27",1,4,3,1,1,100,"Temp")
drvModbusAsynConfigure("ch4rtd","rtds27",1,4,4,1,1,100,"Temp")
drvModbusAsynConfigure("ch5rtd","rtds27",1,4,5,1,1,100,"Temp")
asynSetTraceIOMask("rtds27",0,0x2)
asynSetTraceMask("rtds27",0,0x1)

## Set up ASYN ports
## drvAsynIPPortConfigure port ipInfo priority noAutoconnect noProcessEos
##
## Displacement Measurement System
## MMD1 10.6.28.17
drvAsynIPPortConfigure("L0","10.6.28.17:23",0,0,0)
asynSetTraceIOMask("L0",-1,0x2)
asynSetTraceMask("L0",-1,0x1)
capaNCDT6200Configure("L1","10.6.28.17",10001)
asynSetTraceIOMask("L1",-1,0x2)
asynSetTraceMask("L1",-1,0x1)

## Hydrostatic Leveling System - Stream Device & ASYN Port Driver
## MMD4 10.6.28.18
drvAsynIPPortConfigure("L2","10.6.28.18:23",0,0,0)
asynSetTraceIOMask("L2",-1,0x2)
asynSetTraceMask("L2",-1,0x1)
capaNCDT6200Configure("L3","10.6.28.18",10001)
asynSetTraceIOMask("L3",-1,0x2)
asynSetTraceMask("L3",-1,0x1)

## Hydrostatic Leveling Systems - Stream Device & ASYN Port Driver
## MMD5 10.6.28.20
drvAsynIPPortConfigure("L4","10.6.28.20:23",0,0,0)
asynSetTraceIOMask("L4",-1,0x2)
asynSetTraceMask("L4",-1,0x1)
capaNCDT6200Configure("L5","10.6.28.20",10001)
asynSetTraceIOMask("L5",-1,0x2)
asynSetTraceMask("L5",-1,0x1)

###############################################################################
# Load record instances
dbLoadRecords("db/xxCapaNCDT6200.vdb", "dev=MMS:S27:MMD1,Link=L0")
dbLoadRecords("db/xxCapaMeas.vdb", "dev=MMS:S27:MMD1,PORT=L1")
dbLoadRecords("db/xxHydrostaticConfig.vdb", "dev=MMS:S27:MMD4,Link=L2")
dbLoadRecords("db/xxHydrostaticMeas.vdb", "dev=MMS:S27:MMD4,PORT=L3")
dbLoadRecords("db/xxHydrostaticConfig.vdb", "dev=MMS:S27:MMD5,Link=L4")
dbLoadRecords("db/xxHydrostaticMeas.vdb", "dev=MMS:S27:MMD5,PORT=L5")
dbLoadRecords("db/xxMMS_top.vdb", "sec=S27")
dbLoadRecords("db/moxaRtd.vdb","dev=MMS:S27")

# Load ASYN records for debugging
dbLoadRecords("db/asynRecord.db","P=$(P),R=asynL0,PORT=L0,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("db/asynRecord.db","P=$(P),R=asynL1,PORT=L1_RBK,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("db/asynRecord.db","P=$(P),R=asynL2,PORT=L2,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("db/asynRecord.db","P=$(P),R=asynL3,PORT=L3_RBK,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("db/asynRecord.db","P=$(P),R=asynL4,PORT=L4,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("db/asynRecord.db","P=$(P),R=asynL5,PORT=L5_RBK,ADDR=0,OMAX=0,IMAX=0")

###############################################################################
# Start IOC
cd ${TOP}/iocBoot/${IOC}
iocInit

cd ${TOP}/iocBoot/${IOC}
#=====================================================================
