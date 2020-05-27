//
//  RMIBus.h
//  VoodooSMBus
//
//  Created by Sheika Slate on 4/30/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#ifndef RMIBus_h
#define RMIBus_h

class RMIBus;
class RMIFunction;

#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "VoodooSMBusSlaveDeviceDriver.hpp"
#include "VoodooSMBusDeviceNub.hpp"
#include "rmi.h"
#include "types.h"
#include "rmi_driver.hpp"
#include "rmi_smbus.hpp"

#include <F01.hpp>
#include <F11.hpp>
#include <F30.hpp>
#include <F34.hpp>

#define SMB_PROTOCOL_VERSION_ADDRESS 0xfd
#define SMB_PROTOCOL_VERSION_ADDRESS    0xfd
#define SMB_MAX_COUNT            32
#define RMI_SMB2_MAP_SIZE        8 /* 8 entry of 4 bytes each */
#define RMI_SMB2_MAP_FLAGS_WE        0x01

struct mapping_table_entry {
    __le16 rmiaddr;
    u8 readcount;
    u8 flags;
};

#define container_of(ptr, type, member) ({  \
    type *__mptr = (type *)(ptr);           \
    ((type *)(__mptr - offsetof(type, member))); })

enum {
    kHandleRMIInterrupt = iokit_vendor_specific_msg(1100)
};

class RMIBus : public VoodooSMBusSlaveDeviceDriver {
    OSDeclareDefaultStructors(RMIBus);
    
public:
    RMIBus * probe(IOService *provider, SInt32 *score) override;
    void handleHostNotify () override;
    bool init(OSDictionary *dictionary) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    
    VoodooSMBusDeviceNub *device_nub;
    rmi_driver_data *data;
    
    // rmi_read
    int read(u16 addr, u8 *buf);
    // rmi_read_block
    int readBlock (u16 rmiaddr, u8 *databuff, size_t len);
    // rmi_write
    int write(u16 rmiaddr, u8 *buf);
    // rmi_block_write
    int blockWrite(u16 rmiaddr, u8 *buf, size_t len);
    
    IOLock *page_mutex;
    IOLock *mapping_table_mutex;
    OSSet *functions;
    struct mapping_table_entry mapping_table[RMI_SMB2_MAP_SIZE];
    u8 table_index;
    
    int rmi_register_function(rmi_function* fn);
    int rmi_smb_get_version();
private:
    void handleHostNotifyThreaded();
};
    
#endif /* RMIBus_h */
