TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#=============================
#BUILD IOC SUPPORT LIB
LIBRARY_IOC += microEpsilon

# Library source files
microEpsilon_SRCS += capaNCDT6200Sup.c

# Link with asyn
microEpsilon_LIBS += asyn

# Install dbd files
DBD += capaNCDT6200.dbd

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
