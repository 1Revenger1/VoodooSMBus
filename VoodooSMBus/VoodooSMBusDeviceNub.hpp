/*
 * VoodooSMBusDeviceNub.hpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 */


#ifndef VoodooSMBusDeviceNub_hpp
#define VoodooSMBusDeviceNub_hpp

#include <IOKit/IOService.h>

class VoodooSMBusControllerDriver;
class IOInterruptEventSource;
typedef UInt8 u8;

struct VoodooSMBusSlaveDevice {
    UInt8 addr;
    UInt8 flags;
};

#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif

class EXPORT VoodooSMBusDeviceNub : public IOService {
    OSDeclareDefaultStructors(VoodooSMBusDeviceNub);
    
public:
    bool init() override;
    bool attach(IOService* provider, UInt8 address);
    void free(void) override;

    void handleHostNotify();
    void setSlaveDeviceFlags(unsigned short flags);
    
    IOReturn writeByteData(u8 command, u8 value);
    IOReturn readByteData(u8 command);
    IOReturn readBlockData(u8 command, u8 *values);
    IOReturn writeByte(u8 value);
    IOReturn writeBlockData(u8 command, u8 length, const u8 *values);
    IOReturn wakeupController();
    
    bool createPS2Stub(IOService *ps2Trackpad);
    IOService *grabService(const char *serviceName);
    bool acidantheraTrackpadExists();
    
private:
    IOInterruptEventSource *interruptSource;
    IOWorkLoop *workloop;
    VoodooSMBusControllerDriver* controller;
    VoodooSMBusSlaveDevice slave_device;
    void handleHostNotifyGated(OSObject* owner, IOInterruptEventSource* src, int intCount);
};

#endif /* VoodooSMBusDeviceNub_hpp */
