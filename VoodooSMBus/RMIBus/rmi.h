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
#include "list.h"

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

struct rmi_driver_data {
    list_head function_list;
    
    RMIBus *rmi_dev;
    
    rmi_function *f01_container;
    rmi_function *f34_container;
    bool bootloader_mode;
    
    int num_of_irq_regs;
    int irq_count;
    unsigned long *irq_memory;
    unsigned long *irq_status;
    unsigned long *fn_irq_bits;
    unsigned long *current_irq_mask;
    unsigned long *new_irq_mask;
    IOSimpleLock *irq_mutex;
    
    struct irq_domain *irqdomain;
    
    u8 pdt_props;
    
    u8 num_rx_electrodes;
    u8 num_tx_electrodes;
    
    bool enabled;
    IOSimpleLock *enabled_mutex;
    
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
