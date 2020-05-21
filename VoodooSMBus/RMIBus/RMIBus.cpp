//
//  RMIBus.c
//  VoodooSMBus
//
//  Created by Sheika Slate on 4/30/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#include "RMIBus.hpp"

OSDefineMetaClassAndStructors(RMIBus, VoodooSMBusSlaveDeviceDriver)
OSDefineMetaClassAndStructors(RMIFunction, IOService)
#define super IOService

bool RMIBus::init(OSDictionary *dictionary) {
    data = reinterpret_cast<rmi_driver_data *>(IOMalloc(sizeof(rmi_driver_data)));
    memset(data, 0, sizeof(rmi_driver_data));
    
    if (!data) return false;
    
    page_mutex = IOLockAlloc();
    mapping_table_mutex = IOLockAlloc();
    data->irq_mutex = IOLockAlloc();
    data->enabled_mutex = IOLockAlloc();
    return super::init(dictionary);
}

RMIBus * RMIBus::probe(IOService *provider, SInt32 *score) {
    IOLog("Probe");
    if (!super::probe(provider, score))
        return NULL;
    
    device_nub = OSDynamicCast(VoodooSMBusDeviceNub, provider);
    
    IOLog("Recieving SMBus version: %d\n", rmi_smb_get_version());
    if (!device_nub) {
        IOLog("%s Could not et VoodooSMBus device nub instance\n", getName());
        return NULL;
    }
    
    device_nub->setSlaveDeviceFlags(I2C_CLIENT_HOST_NOTIFY);

    
    if (rmi_driver_probe(this)) {
        IOLog("Could not probe");
        return NULL;
    }
    
    return this;
}

bool RMIBus::start(IOService *provider) {
    if (!super::start(provider))
        return false;
    int retval;
    
    retval = rmi_init_functions(data);
    if (retval)
        goto err;

    retval = rmi_enable_sensor(this);
    if (retval)
        goto err;
    
//    provider->joinPMtree(this);
//    registerPowerDriver(this, , unsigned long numberOfStates);
    
    registerService();
    return true;
err:
    IOLog("Could not start");
    return false;
}

void RMIBus::handleHostNotifyThreaded()
{
    OSIterator* iter = getClientIterator();
    OSObject* obj = NULL;
    RMIFunction* func;
    
    unsigned long curIRQ = 0;
    
    while((func = OSDynamicCast(RMIFunction, iter->getNextObject())))
    {
//        if (func->getIRQ() & curIRQ)
//            func->handleInterrupt();
        
        func = NULL;
    }
    iter->reset();
    
    OSSafeReleaseNULL(iter);
    
    IOLogError("Unknown command");
}

void RMIBus::handleHostNotify() {
    IOLog("Notification recieved");
    
//    thread_t new_thread;
//    kern_return_t ret = kernel_thread_start(
//            OSMemberFunctionCast(thread_continue_t, this, &RMIBus::handleHostNotifyThreaded),
//            this, &new_thread);
//    
//    if (ret != KERN_SUCCESS) {
//        IOLogDebug(" Thread error while attemping to handle host notify in device nub.\n");
//    } else {
//        thread_deallocate(new_thread);
//    }
}

void RMIBus::stop(IOService *provider) {
    PMstop();
    
    super::stop(provider);
}

void RMIBus::initialize() {
        
    
}

void RMIBus::free() {
    rmi_free_function_list(this);
    
    IOLockFree(data->enabled_mutex);
    IOLockFree(data->irq_mutex);
    IOLockFree(page_mutex);
    IOLockFree(mapping_table_mutex);
    OSSafeReleaseNULL(device_nub);
    super::free();
}

int RMIBus::rmi_register_function(rmi_function *fn) {
    RMIFunction * function;
    
    switch(fn->fd.function_number) {
        case 0x01:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F01));
            break;
        case 0x11:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F11));
            break;
        case 0x30:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F30));
            break;
        case 0x34:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F34));
            break;
        case 0x54:
            IOLog("F54 not implemented - Debug function\n");
            return 0;
        default:
            IOLogError("Unknown function: %02X - Continuing to load\n", fn->fd.function_number);
            return 0;
    }
    
    if (!function || !function->init()) {
        IOLogError("Could not initialize function: %02X\n", fn->fd.function_number);
        OSSafeReleaseNULL(function);
        return -ENODEV;
    }
    
    // Duplicate to store in function
    rmi_function_descriptor* desc =
        reinterpret_cast<rmi_function_descriptor*>(IOMalloc(sizeof(rmi_function_descriptor)));
    
    desc->command_base_addr = fn->fd.command_base_addr;
    desc->control_base_addr = fn->fd.control_base_addr;
    desc->data_base_addr = fn->fd.data_base_addr;
    desc->function_number = fn->fd.function_number;
    desc->function_version = fn->fd.function_version;
    desc->interrupt_source_count = fn->fd.interrupt_source_count;
    desc->query_base_addr = fn->fd.query_base_addr;
    
    function->setFunctionDesc(desc);
    function->setMask(fn->irq_mask[0]);
    function->setIrqPos(fn->irq_pos);
    
    SInt32 score = 2046;
    
    if (!function->probe(this, &score)) {
        IOLogError("Probe failed for function: %02X\n", fn->fd.function_number);
        OSSafeReleaseNULL(function);
        return -ENODEV;
    }
    
    if (!function->attach(this)) {
        IOLogError("Function %02X could not attach\n", fn->fd.function_number);
        OSSafeReleaseNULL(function);
        return -ENODEV;
    }
    
    // For some reason trying to release it and detatch in ::stop
    // just doesn't work. An iterator over the dictionary and getClientIterator()
    // just returns null when we try to iterate. Releasing here seems to work fine
    // as jank as it seems.
    OSSafeReleaseNULL(function);
    return 0;
}
