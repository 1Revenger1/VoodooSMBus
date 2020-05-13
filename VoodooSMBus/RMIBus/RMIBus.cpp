//
//  RMIBus.c
//  VoodooSMBus
//
//  Created by Sheika Slate on 4/30/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#include "RMIBus.hpp"

OSDefineMetaClassAndStructors(RMIBus, VoodooSMBusSlaveDeviceDriver)

#define super IOService

bool RMIBus::init(OSDictionary *dictionary) {
    page_mutex = IOLockAlloc();
    mapping_table_mutex = IOLockAlloc();
    return super::init(dictionary);
}

RMIBus * RMIBus::probe(IOService *provider, SInt32 *score) {
    IOLog("Probe");
    if (!super::probe(provider, score))
        return NULL;
    
    device_nub = OSDynamicCast(VoodooSMBusDeviceNub, provider);
    
    if (!device_nub) {
        IOLog("%s Could not et VoodooSMBus device nub instance\n", getName());
        return NULL;
    }
    
    device_nub->setSlaveDeviceFlags(I2C_CLIENT_HOST_NOTIFY);

    IOLog("Recieving SMBus version: %d\n", rmi_smb_get_version());
    
    if (rmi_driver_probe(this)) {
        IOLog("Could not probe");
        OSSafeReleaseNULL(device_nub);
        return NULL;
    }
    
    return this;
}

bool RMIBus::start(IOService *provider) {
    if (!super::start(provider))
        return false;
    
//    provider->joinPMtree(this);
//    registerPowerDriver(this, , unsigned long numberOfStates);
    
    registerService();
    return true;
}

void RMIBus::handleHostNotify() {
    IOLog("Notification recieved");
}

void RMIBus::stop(IOService *provider) {
    PMstop();
    IOLockFree(page_mutex);
    IOLockFree(mapping_table_mutex);
    OSSafeReleaseNULL(device_nub);
    super::stop(provider);
}

void RMIBus::initialize() {
    
    
}

int RMIBus::rmi_register_function(struct rmi_function) {
    // Idk what to do with this actually tbh...
    // Probs can delete
    
    return 0;
}
