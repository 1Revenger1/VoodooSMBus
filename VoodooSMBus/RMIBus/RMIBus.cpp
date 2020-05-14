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

void RMIBus::handleHostNotifyThreaded()
{
    OSIterator* iter = getClientIterator();
    OSObject* obj = NULL;
    RMIFunction* func;
    
    unsigned long curIRQ = 0;
    
    while((obj = iter->getNextObject()) != NULL)
    {
        func = reinterpret_cast<RMIFunction*>(obj);
        if (!func) continue;
        
        if (func->functionIrq() & curIRQ)
            func->handleInterrupt();
        
        func = NULL;
    }
    iter->reset();
    
    IOLogError("Unknown command");
}

void RMIBus::handleHostNotify() {
    IOLog("Notification recieved");
    
    thread_t new_thread;
    kern_return_t ret = kernel_thread_start(
            OSMemberFunctionCast(thread_continue_t, this, &RMIBus::handleHostNotifyThreaded),
            this, &new_thread);
    
    if (ret != KERN_SUCCESS) {
        IOLogDebug(" Thread error while attemping to handle host notify in device nub.\n");
    } else {
        thread_deallocate(new_thread);
    }
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

void RMIBus::free() {
    OSIterator *iter = getClientIterator();
    OSObject *obj;
    IOService *cast;
    
    while ((obj = iter->getNextObject()) != NULL) {
        cast = reinterpret_cast<IOService*>(obj);
        cast->detach(this);
        cast = NULL;
    }
}

int RMIBus::rmi_register_function(struct rmi_function fn) {
    RMIFunction * function;
    
    switch(fn.fd.function_number) {
        case 0x01:
            function = reinterpret_cast<RMIFunction*>(OSTypeAlloc(F01));
            break;
        case 0x54:
            IOLog("F54 not implemented - Debug function\n");
            return 0;
        default:
            IOLogError("Unknown function: %02X\n", fn.fd.function_number);
            return -ENODEV;
    }
    
    if (!function || !function->init()) {
        IOLogError("Could not initialize function: %02X\n", fn.fd.function_number);
        return -ENODEV;
    }
    
    function->setFunctionDesc(&fn.fd);
    
    SInt32 score = 100;
    
    if (!function->probe(this, &score)) {
        IOLogError("Probe failed for function: %02X\n", fn.fd.function_number);
        return -ENODEV;
    }
    
    // rmi_create_function_irq
    for(int i = 0; i < fn.num_of_irqs; i++) {
        function->setBit(fn.irq_pos + i);
    }
    
    if (!function->attach(this)) {
        IOLogError("Function %02X could not attach\n", fn.fd.function_number);
        return -ENODEV;
    }
    
    return 0;
}
