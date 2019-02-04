TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#=============================
# Build the IOC application

PROD_IOC = capaNCDT6200test
# capaNCDT6200test.dbd will be created and installed
DBD += capaNCDT6200test.dbd

# capaNCDT6200.dbd will be made up from these files:
capaNCDT6200test_DBD += base.dbd

# Include dbd files from all support modules:
#capaNCDT6200test_DBD += devmicroEpsilon.dbd
capaNCDT6200test_DBD += capaNCDT6200.dbd
capaNCDT6200test_DBD += stream.dbd
capaNCDT6200test_DBD += asyn.dbd
capaNCDT6200test_DBD += drvAsynIPPort.dbd
capaNCDT6200test_DBD += asynRecord.dbd

# Link in the code from the support libraries
capaNCDT6200test_LIBS += microEpsilon
capaNCDT6200test_LIBS += stream
capaNCDT6200test_LIBS += asyn

# capaNCDT6200_registerRecordDeviceDriver.cpp derives from capaNCDT6200.dbd
capaNCDT6200test_SRCS += capaNCDT6200test_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
capaNCDT6200test_SRCS_DEFAULT += capaNCDT6200testMain.cpp
capaNCDT6200test_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#capaNCDT6200test_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
capaNCDT6200test_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
