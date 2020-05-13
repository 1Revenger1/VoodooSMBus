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
#include "rmi.h"
#include "rmi_driver.hpp"
#include "rmi_smbus.hpp"

class RMIBus;

#define SMB_PROTOCOL_VERSION_ADDRESS 0xfd
#define SMB_PROTOCOL_VERSION_ADDRESS    0xfd
#define SMB_MAX_COUNT            32
#define RMI_SMB2_MAP_SIZE        8 /* 8 entry of 4 bytes each */
#define RMI_SMB2_MAP_FLAGS_WE        0x01

// types.h

#define __force

#define ENOMEM  12
#define ENODEV  19
#define EINVAL  22

typedef UInt8  u8;
typedef UInt16 u16;
typedef UInt32 u32;
typedef UInt64 u64;
typedef u8 __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;
typedef  SInt16 __be16;
typedef  SInt32 __be32;
typedef  SInt64 __be64;
typedef  SInt16 __le16;
typedef  SInt32 __le32;
typedef  SInt64 __le64;
typedef SInt8  s8;
typedef SInt16 s16;
typedef SInt32 s32;
typedef SInt64 s64;
typedef s8  __s8;
typedef s16 __s16;
typedef s32 __s32;
typedef s64 __s64;

#define __cpu_to_le64(x) ((__force __le64)(__u64)(x))
#define __le64_to_cpu(x) ((__force __u64)(__le64)(x))
#define __cpu_to_le32(x) ((__force __le32)(__u32)(x))
#define __le32_to_cpu(x) ((__force __u32)(__le32)(x))
#define __cpu_to_le16(x) ((__force __le16)(__u16)(x))
#define __le16_to_cpu(x) ((__force __u16)(__le16)(x))
#define __cpu_to_be64(x) ((__force __be64)__swab64((x)))
#define __be64_to_cpu(x) __swab64((__force __u64)(__be64)(x))
#define __cpu_to_be32(x) ((__force __be32)__swab32((x)))
#define __be32_to_cpu(x) __swab32((__force __u32)(__be32)(x))
#define __cpu_to_be16(x) ((__force __be16)__swab16((x)))
#define __be16_to_cpu(x) __swab16((__force __u16)(__be16)(x))

#define cpu_to_le64 __cpu_to_le64
#define le64_to_cpu __le64_to_cpu
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu
#define cpu_to_le16 __cpu_to_le16
#define le16_to_cpu __le16_to_cpu
#define cpu_to_be64 OSSwapHostToBigInt64
#define be64_to_cpu OSSwapBigToHostInt64
#define cpu_to_be32 OSSwapHostToBigInt32
#define be32_to_cpu OSSwapBigToHostInt32
#define cpu_to_be16 OSSwapHostToBigInt16
#define be16_to_cpu OSSwapBigToHostInt16

#define BITS_PER_BYTE           8
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

// end types.h

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

class RMIFunction{
    
public:
    int virtual probe();
    int virtual config();
    int virtual reset();
    void virtual remove();
    IOReturn virtual attention(void* ctx);
    int virtual suspend();
    int virtual resume();
};

class RMIBus : public VoodooSMBusSlaveDeviceDriver {
    OSDeclareDefaultStructors(RMIBus);
    
public:
    RMIBus * probe(IOService *provider, SInt32 *score) override;
    void handleHostNotify () override;
    bool init(OSDictionary *dictionary) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    
    VoodooSMBusDeviceNub *device_nub;
    rmi_driver_data *data;
    
    // rmi_read
    int read(u16 addr, u8 *buf);
    // rmi_read_block
    int readBlock (u16 rmiaddr, u8 *databuff, size_t len);
    // rmi_block_write
    int blockWrite(u8 command, const u8 *buf, size_t len);
    
    IOLock *page_mutex;
    IOLock *mapping_table_mutex;
    struct mapping_table_entry mapping_table[RMI_SMB2_MAP_SIZE];
    u8 table_index;
    
    int rmi_register_function(struct rmi_function);
    int rmi_smb_get_version();
private:
    void initialize();
};
    
#endif /* RMIBus_h */
