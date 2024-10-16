/*
 * VoodooSMBusControllerDriver.cpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 * some functions are ported from the linux kernel driver at:
 * https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-core-smbus.c
 * by Frodo Looijaard <frodol@dds.nl>
 * by Mark Studebaker <mdsxyz123@yahoo.com> and
 * Jean Delvare <jdelvare@suse.de>
 */

#include "VoodooSMBusControllerDriver.hpp"

OSDefineMetaClassAndStructors(VoodooSMBusControllerDriver, IOService)

#define super IOService

static const OSSymbol *gSMBusCompanionSymbol = nullptr;

bool VoodooSMBusControllerDriver::init(OSDictionary *dict) {
    bool result = super::init(dict);
    
    gSMBusCompanionSymbol = OSSymbol::withCString("VoodooSMBusCompanionDevice");
    adapter = reinterpret_cast<i801_adapter*>(IOMalloc(sizeof(i801_adapter)));
    awake = true;
    
    return result;
}

void VoodooSMBusControllerDriver::free(void) {
    IOFree(adapter, sizeof(i801_adapter));
    super::free();
}

IOService *VoodooSMBusControllerDriver::probe(IOService *provider, SInt32 *score) {
    IOService *result = super::probe(provider, score);
    return result;
}

bool VoodooSMBusControllerDriver::start(IOService *provider) {
    bool result = super::start(provider);
    
    pci_device = OSDynamicCast(IOPCIDevice, provider);
    
    if (!(pci_device = OSDynamicCast(IOPCIDevice, provider))) {
        IOLogError("Failed to cast provider");
        return false;
    }

    pci_device->setIOEnable(true);
   
    adapter->pci_device = pci_device;
    adapter->name = getMatchedName(provider);
    
    pci_device->retain();
    if (!pci_device->open(this)) {
        IOLogError("%s::%s Could not open provider", getName(), pci_device->getName());
        return false;
    }
    
    uint32_t host_config = pci_device->configRead8(SMBHSTCFG);
    if ((host_config & SMBHSTCFG_HST_EN) == 0) {
        IOLogError("SMBus disabled");
        return false;
    }
    
    adapter->smba = pci_device->configRead16(ICH_SMB_BASE) & 0xFFFE;
    
    if (host_config & SMBHSTCFG_SMB_SMI_EN) {
        IOLogError("No PCI IRQ. Poll mode is not implemented. Unloading.");
        return false;
    }
    
    adapter->original_hstcfg = host_config;
    adapter->original_slvcmd = pci_device->ioRead8(SMBSLVCMD(adapter));
    adapter->features |= FEATURE_I2C_BLOCK_READ;
    adapter->features |= FEATURE_IRQ;
    adapter->features |= FEATURE_SMBUS_PEC;
    adapter->features |= FEATURE_BLOCK_BUFFER;
    adapter->features |= FEATURE_HOST_NOTIFY;
    adapter->retries = 3;
    adapter->timeout = 200; // MS
    
    work_loop = reinterpret_cast<IOWorkLoop*>(getWorkLoop());
    if (!work_loop) {
        IOLogError("%s Could not get work loop", getName());
        goto exit;
    }
    
    if (provider->registerInterrupt(0, nullptr, handleInterrupt, this) != kIOReturnSuccess) {
        IOLogError("%s Could not register interrupt", getName());
        goto exit;
    }
    
    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLogError("%s Could not open command gate", getName());
        goto exit;
    }
    adapter->command_gate = command_gate;
    work_loop->retain();
    
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, VoodooSMBusPowerStates, kVoodooSMBusPowerStates);
    pci_device->enablePCIPowerManagement(kPCIPMCSPowerStateD0);
    
    deviceList = createEmptyList();
    publishMultipleNubs();
    // Enable interrupts so that device drivers can probe/attach
    provider->enableInterrupt(0);
    enableHostNotify();
    
    publishResource(gSMBusCompanionSymbol, this);
    registerService();

    return result;
    
exit:
    releaseResources();
    return false;
}

void VoodooSMBusControllerDriver::releaseResources() {
    deleteTree(deviceList);
    
    if (command_gate) {
        work_loop->removeEventSource(command_gate);
        command_gate->release();
        command_gate = NULL;
    }
    
    OSSafeReleaseNULL(work_loop);
    pci_device->close(this);
    OSSafeReleaseNULL(pci_device);
}


void VoodooSMBusControllerDriver::stop(IOService *provider) {
    disableHostNotify();
    pci_device->ioWrite8(SMBHSTCFG, adapter->original_hstcfg);
    provider->disableInterrupt(0);
    provider->unregisterInterrupt(0);
    releaseResources();
    PMstop();
    super::stop(provider);
}

IOReturn VoodooSMBusControllerDriver::setPowerState(unsigned long whichState, IOService* whatDevice) {
    if (whatDevice != this)
        return kIOPMAckImplied;
    
    if (whichState == kIOPMPowerOff) {
        disableHostNotify();
        pci_device->ioWrite8(SMBHSTCFG, adapter->original_hstcfg);
        awake = false;
    } else {
        if (!awake) {
            pci_device->enablePCIPowerManagement(kPCIPMCSPowerStateD0);
            enableHostNotify();
            awake = true;
        }
        
    }
    return kIOPMAckImplied;
}

void VoodooSMBusControllerDriver::disableCommandGate() {
    command_gate->disable();
}

void VoodooSMBusControllerDriver::publishMultipleNubs() {
    addresses = OSDynamicCast(OSArray, getProperty("Addresses"));
    if (!addresses)
        return;
    
    OSIterator *iter = OSCollectionIterator::withCollection(addresses);
    if (!iter)
        return;
    
    while (OSNumber *addr = OSDynamicCast(OSNumber, iter->getNextObject()))
    {
        uint8_t addrPrim = addr->unsigned8BitValue();
        VoodooSMBusDeviceNub *dev;
        IOReturn ret = createNubGated(addrPrim, nullptr, nullptr, &dev);
        if (ret != kIOReturnSuccess)
            goto exit;
        
        dev->registerService();
    }
    
exit:
    OSSafeReleaseNULL(iter);
}

VoodooSMBusDeviceNub *VoodooSMBusControllerDriver::createNub(UInt8 address, IOService *ps2parent, OSDictionary *props) {
    VoodooSMBusDeviceNub *dev;
    IOReturn ret = command_gate->runAction(OSMemberFunctionCast(Action, this, &VoodooSMBusControllerDriver::createNubGated),
                                           reinterpret_cast<void *>(address),
                                           ps2parent,
                                           props,
                                           &dev);
    
    return ret == kIOReturnSuccess ? dev : nullptr;
}


IOReturn VoodooSMBusControllerDriver::createNubGated(UInt8 address, IOService *ps2parent, OSDictionary *props, VoodooSMBusDeviceNub **nub) {
    if (!nub) {
        return kIOReturnBadArgument;
    }

    if (getDevice(deviceList, address) != nullptr) {
        IOLogError("Device at 0x%x already exists!", address);
        return kIOReturnPortExists;
    }

    auto *device_nub = OSTypeAlloc(VoodooSMBusDeviceNub);
    
    if (device_nub == nullptr ||
        !device_nub->init() ||
        !device_nub->attach(this, address) ||
        !device_nub->start(this)) {
        IOLogError("%s::%s Could not initialise nub", getName(), adapter->name);
        OSSafeReleaseNULL(device_nub);
        *nub = nullptr;
        return kIOReturnError;
    }
    
    device_nub->setProperty("PS/2 Parent", ps2parent);
    device_nub->setProperty("PS/2 Properties", props);
    
    getProvider()->disableInterrupt(0);
    if (!insertDevice(deviceList, device_nub, address)) {
        IOLogError("Failed to insert nub %#04x into list", address);
        OSSafeReleaseNULL(device_nub);
        *nub = nullptr;
        return kIOReturnNoMemory;
    } else {
        IOLogInfo("Publishing nub for slave device at address %#04x", address);
    }
    
    getProvider()->enableInterrupt(0);
    *nub = device_nub;
    return kIOReturnSuccess;
}


void VoodooSMBusControllerDriver::removeNub(UInt8 address) {
    command_gate->runAction(OSMemberFunctionCast(Action, this, &VoodooSMBusControllerDriver::removeNubGated),
                            reinterpret_cast<void *>(address));
}


IOReturn VoodooSMBusControllerDriver::removeNubGated(UInt8 address) {
    getProvider()->disableInterrupt(0);
    IOService *temp = deleteDevice(deviceList, address);
    if (temp) temp->terminate();
    OSSafeReleaseNULL(temp);
    getProvider()->enableInterrupt(0);
    return kIOReturnSuccess;
}

IOWorkLoop* VoodooSMBusControllerDriver::getWorkLoop() {
    // Do we have a work loop already?, if so return it NOW.
    if ((vm_address_t) work_loop >> 1)
        return work_loop;
    
    if (OSCompareAndSwap(0, 1, reinterpret_cast<IOWorkLoop*>(&work_loop))) {
        // Construct the workloop and set the cntrlSync variable
        // to whatever the result is and return
        work_loop = IOWorkLoop::workLoop();
    } else {
        while (reinterpret_cast<IOWorkLoop*>(work_loop) == reinterpret_cast<IOWorkLoop*>(1)) {
            // Spin around the cntrlSync variable until the
            // initialization finishes.
            thread_block(0);
        }
    }
    
    return work_loop;
}

// static
void VoodooSMBusControllerDriver::handleInterrupt(OSObject* owner, void *refCon, IOService *nub, int source) {
    VoodooSMBusControllerDriver *me = (VoodooSMBusControllerDriver *)refCon;
    i801_adapter *adapter = me->adapter;
    u8 status;

    if (adapter->features & FEATURE_HOST_NOTIFY) {
        status = adapter->inb_p(SMBSLVSTS(adapter));
        if (status & SMBSLVSTS_HST_NTFY_STS) {
            UInt8 addr;
            
            addr = adapter->inb_p(SMBNTFDADD(adapter)) >> 1;
            
            /*
             * With the tested platforms, reading SMBNTFDDAT (22 + (p)->smba)
             * always returns 0. Our current implementation doesn't provide
             * data, so we just ignore it.
             */
            
            VoodooSMBusDeviceNub *nub = getDevice(me->deviceList, addr);
            if (nub) {
                nub->handleHostNotify();
            }
            
            /* clear Host Notify bit and return */
            adapter->outb_p(SMBSLVSTS_HST_NTFY_STS, SMBSLVSTS(adapter));
            return;
        }
    }
    
    status = adapter->inb_p(SMBHSTSTS(adapter));

    if (status & SMBHSTSTS_BYTE_DONE) {
        i801_isr_byte_done(adapter);
    }
    
    /*
     * Clear irq sources and report transaction result.
     * ->status must be cleared before the next transaction is started.
     */
    status &= SMBHSTSTS_INTR | STATUS_ERROR_FLAGS;
    if (status) {
        adapter->outb_p(status, SMBHSTSTS(adapter));
        adapter->status = status;
        me->command_gate->commandWakeup(&adapter->status);
    }
}


void VoodooSMBusControllerDriver::enableHostNotify() {
    
    if(!(adapter->original_slvcmd & SMBSLVCMD_HST_NTFY_INTREN)) {
        pci_device->ioWrite8(SMBSLVCMD(adapter), SMBSLVCMD_HST_NTFY_INTREN | adapter->original_slvcmd);
    }

    /* clear Host Notify bit to allow a new notification */
    pci_device->ioWrite8(SMBSLVSTS(adapter), SMBSLVSTS_HST_NTFY_STS);
}

void VoodooSMBusControllerDriver::disableHostNotify() {
    pci_device->ioWrite8(SMBSLVCMD(adapter), adapter->original_slvcmd);
}

IOReturn VoodooSMBusControllerDriver::readByteData(VoodooSMBusSlaveDevice *client, u8 command) {
    union i2c_smbus_data data;
    IOReturn status;
    
    status = transfer(client, I2C_SMBUS_READ, command, I2C_SMBUS_BYTE_DATA, &data);
    if (status != kIOReturnSuccess)
        return status;
    
    return data.byte;
}

IOReturn VoodooSMBusControllerDriver::readBlockData(VoodooSMBusSlaveDevice *client, u8 command, u8 *values) {
    union i2c_smbus_data data;
    IOReturn status;
    
    status = transfer(client, I2C_SMBUS_READ, command, I2C_SMBUS_BLOCK_DATA, &data);
    if (status != kIOReturnSuccess)
        return status;
    
    memcpy(values, &data.block[1], data.block[0]);
    return data.block[0];
}

IOReturn VoodooSMBusControllerDriver::writeByteData(VoodooSMBusSlaveDevice *client, u8 command, u8 value) {
    union i2c_smbus_data data;
    data.byte = value;
    
    return transfer(client, I2C_SMBUS_WRITE, command, I2C_SMBUS_BYTE_DATA, &data);
}


IOReturn VoodooSMBusControllerDriver::writeByte(VoodooSMBusSlaveDevice *client, u8 value) {
    return transfer(client, I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}


IOReturn VoodooSMBusControllerDriver::writeBlockData(VoodooSMBusSlaveDevice *client, u8 command,
                                                     u8 length, const u8 *values) {
    union i2c_smbus_data data;
    
    if (length > I2C_SMBUS_BLOCK_MAX)
        length = I2C_SMBUS_BLOCK_MAX;
    data.block[0] = length;
    memcpy(&data.block[1], values, length);
    return transfer(client, I2C_SMBUS_WRITE, command, I2C_SMBUS_BLOCK_DATA, &data);
}

IOReturn VoodooSMBusControllerDriver::transfer(VoodooSMBusSlaveDevice *client, char  read_write, u8 command, int protocol, union i2c_smbus_data *data) {
    VoodooSMBusControllerMessage message = {
        .slave_device = client,
        .read_write = read_write,
        .command = command,
        .protocol = protocol,
    };
    
    return command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooSMBusControllerDriver::transferGated), &message, data);
}

// __i2c_smbus_xfer
IOReturn VoodooSMBusControllerDriver::transferGated(VoodooSMBusControllerMessage *message, union i2c_smbus_data *data) {
    int _try;
    s32 res;

    VoodooSMBusSlaveDevice* slave_device = message->slave_device;
    slave_device->flags &= I2C_M_TEN | I2C_CLIENT_PEC | I2C_CLIENT_SCCB;
    
    /* Retry automatically on arbitration loss */
    for (res = 0, _try = 0; _try <= adapter->retries; _try++) {
        res = i801_access(adapter, slave_device->addr, slave_device->flags, message->read_write, message->command, message->protocol, data);
        if (res != -EAGAIN)
            break;
    }
    
    return res;
}

IOReturn VoodooSMBusControllerDriver::callPlatformFunction(const OSSymbol *functionName, bool waitForFunction, void *param1, void *param2, void *param3, void *param4) {
    if (functionName == gSMBusCompanionSymbol) {
        IOService *parent = OSDynamicCast(IOService, (OSMetaClassBase *) param1);
        OSDictionary *dict = OSDynamicCast(OSDictionary, (OSMetaClassBase *) param2);
        UInt8 addr = static_cast<UInt8>(reinterpret_cast<size_t>(param3));
        if (!dict || !parent) {
            return kIOReturnBadArgument;
        }
        
        IOLogDebug("SMBus Device Discovered");
        auto *dev = createNub(addr, parent, dict);
        if (dev == nullptr) {
            return kIOReturnError;
        }
        
        dev->registerService(kIOServiceSynchronous);
        IOLogInfo("Finished probing w/ client %p", dev->getClient());
        // Matching failed, remove nub to prevent future matching
        if (dev->getClient() == nullptr) {
            removeNub(addr);
            OSSafeReleaseNULL(dev);
            return kIOReturnNoDevice;
        }
        
        OSSafeReleaseNULL(dev);
        return kIOReturnSuccess;
    }
    
    return super::callPlatformFunction(functionName, waitForFunction, param1, param2, param3, param4);
}
