//
//  rmi_driver.h
//  VoodooSMBus
//
//  Created by Gwy on 5/6/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#ifndef rmi_driver_h
#define rmi_driver_h

#include <IOKit/IOBufferMemoryDescriptor.h>
#include "RMIBus.hpp"

#define PDT_PROPERTIES_LOCATION 0x00EF
#define BSR_LOCATION 0x00FE

#define RMI_PDT_PROPS_HAS_BSR 0x02

#define NAME_BUFFER_SIZE 256

#define RMI_PDT_ENTRY_SIZE 6
#define RMI_PDT_FUNCTION_VERSION_MASK   0x60
#define RMI_PDT_INT_SOURCE_COUNT_MASK   0x07

#define PDT_START_SCAN_LOCATION 0x00e9
#define PDT_END_SCAN_LOCATION    0x0005
#define RMI4_END_OF_PDT(id) ((id) == 0x00 || (id) == 0xff)

struct pdt_entry {
    u16 page_start;
    u8 query_base_addr;
    u8 command_base_addr;
    u8 control_base_addr;
    u8 data_base_addr;
    u8 interrupt_source_count;
    u8 function_version;
    u8 function_number;
};

int rmi_initial_reset(RMIBus *dev, void *ctx, pdt_entry *pdt);
int rmi_scan_pdt(RMIBus *dev, void *ctx,
                 int (*callback)(RMIBus* dev,
                                 void *ctx, pdt_entry *entry));
int rmi_probe_interrupts(rmi_driver_data *data);
int rmi_init_functions(struct rmi_driver_data *data);

#endif /* rmi_driver_h */
