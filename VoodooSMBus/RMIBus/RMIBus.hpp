//
//  RMIBus.h
//  VoodooSMBus
//
//  Created by Sheika Slate on 4/30/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#ifndef RMIBus_h
#define RMIBus_h
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include "VoodooSMBusSlaveDeviceDriver.hpp"
#include "VoodooSMBusDeviceNub.hpp"
#include "types.h"
#include "rmi.h"
#include "rmi_driver.hpp"
#include "rmi_smbus.hpp"

class RMIBus;
class RMIFunction;

#include "F01.hpp"


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

static inline UInt64 OSBitwiseAtomic64(unsigned long and_mask, unsigned long or_mask, unsigned long xor_mask, unsigned long * value)
{
    unsigned long    oldValue;
    unsigned long    newValue;
    
    do {
        oldValue = *value;
        newValue = ((oldValue & and_mask) | or_mask) ^ xor_mask;
    } while (! OSCompareAndSwap64(oldValue, newValue, value));
    
    return oldValue;
}

static inline unsigned long OSBitAndAtomic64(unsigned long mask, unsigned long * value)
{
    return OSBitwiseAtomic64(mask, 0, 0, value);
}

static inline unsigned long OSBitOrAtomic64(unsigned long mask, unsigned long * value)
{
    return OSBitwiseAtomic64(-1, mask, 0, value);
}

#define container_of(ptr, type, member) ({  \
    type *__mptr = (type *)(ptr);           \
    ((type *)(__mptr - offsetof(type, member))); })

/*
 * The interrupt source count in the function descriptor can represent up to
 * 6 interrupt sources in the normal manner.
 */
#define RMI_FN_MAX_IRQS    6

/**
 * struct rmi_function - represents the implementation of an RMI4
 * function for a particular device (basically, a driver for that RMI4 function)
 *
 * @fd: The function descriptor of the RMI function
 * @rmi_dev: Pointer to the RMI device associated with this function container
 * @dev: The device associated with this particular function.
 *
 * @num_of_irqs: The number of irqs needed by this function
 * @irq_pos: The position in the irq bitfield this function holds
 * @irq_mask: For convenience, can be used to mask IRQ bits off during ATTN
 * interrupt handling.
 * @irqs: assigned virq numbers (up to num_of_irqs)
 *
 * @node: entry in device's list of functions
 */
struct rmi_function {
    int size;
    struct rmi_function_descriptor fd;
    RMIBus *dev;
    
    unsigned int num_of_irqs;
    int irq[RMI_FN_MAX_IRQS];
    unsigned int irq_pos;
    unsigned long irq_mask[];
};

/*
 * Set the state of a register
 *    DEFAULT - use the default value set by the firmware config
 *    OFF - explicitly disable the register
 *    ON - explicitly enable the register
 */
enum rmi_reg_state {
    RMI_REG_STATE_DEFAULT = 0,
    RMI_REG_STATE_OFF = 1,
    RMI_REG_STATE_ON = 2
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
    int write(u8 command, const u8 *buf);
    // rmi_block_write
    int blockWrite(u8 command, const u8 *buf, size_t len);
    
    IOLock *page_mutex;
    IOLock *mapping_table_mutex;
    struct mapping_table_entry mapping_table[RMI_SMB2_MAP_SIZE];
    u8 table_index;
    
    int rmi_register_function(rmi_function* fn);
    int rmi_smb_get_version();
private:
    void handleHostNotifyThreaded();
    void initialize();
};
    
#endif /* RMIBus_h */
