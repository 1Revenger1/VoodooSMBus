//
//  rmi.h
//  VoodooSMBus
//
//  Created by Gwy on 5/6/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#ifndef rmi_h
#define rmi_h

#include "RMIBus.hpp"

class RMIBus;
struct rmi_function;

/**
 * struct rmi_function_descriptor - RMI function base addresses
 *
 * @query_base_addr: The RMI Query base address
 * @command_base_addr: The RMI Command base address
 * @control_base_addr: The RMI Control base address
 * @data_base_addr: The RMI Data base address
 * @interrupt_source_count: The number of irqs this RMI function needs
 * @function_number: The RMI function number
 *
 * This struct is used when iterating the Page Description Table. The addresses
 * are 16-bit values to include the current page address.
 *
 */
struct rmi_function_descriptor {
    u16 query_base_addr;
    u16 command_base_addr;
    u16 control_base_addr;
    u16 data_base_addr;
    u8 interrupt_source_count;
    u8 function_number;
    u8 function_version;
};

struct rmi4_attn_data {
    unsigned long irq_status;
    size_t size;
    void *data;
};

struct __kfifo {
    unsigned int    in;
    unsigned int    out;
    unsigned int    mask;
    unsigned int    esize;
    void        *data;
};


class RMIFunction : public IOService {
    OSDeclareDefaultStructors(RMIFunction)
    
public:
    virtual RMIFunction* probe(IOService *provider, SInt32 *score) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    IOReturn virtual handleInterrupt();
    int virtual functionIrq();
    
    inline void setFunctionDesc(rmi_function_descriptor *desc) {
        this->fn_descriptor = desc;
    }
    
    inline void setBit(int bit) {
        irq_mask |= 1 << bit;
    }
    
    inline unsigned long getIRQ() {
        return irq_mask;
    }
    
private:
    unsigned long irq_mask;
    
protected:
    rmi_function_descriptor *fn_descriptor;
};

struct rmi_driver_data {
    RMIBus *rmi_dev;
    
    rmi_function *f01_container;
    rmi_function *f34_container;
    bool bootloader_mode;
    
    int num_of_irq_regs;
    int irq_count;
    unsigned long *irq_memory;
    unsigned long irq_memory_size;
    unsigned long *irq_status;
    unsigned long *fn_irq_bits;
    unsigned long *current_irq_mask;
    unsigned long *new_irq_mask;
    IOLock *irq_mutex;
    
    struct irq_domain *irqdomain;
    
    u8 pdt_props;
    
    u8 num_rx_electrodes;
    u8 num_tx_electrodes;
    
    bool enabled;
    IOLock *enabled_mutex;
    
    rmi4_attn_data attn_data;
    
    struct {
        union {
            struct __kfifo          kfifo;
            struct rmi4_attn_data   *type;
            const  rmi4_attn_data   *const_type;
            char                    (*rectype)[0];
            struct rmi4_attn_data   *ptr;
            struct rmi4_attn_data const *ptr_const;
        };
        rmi4_attn_data buf[16];
    } attn_fifo;
//    DECLARE_KFIFO(attn_fifo, struct rmi4_attn_data, 16);
};

#endif /* rmi_h */
