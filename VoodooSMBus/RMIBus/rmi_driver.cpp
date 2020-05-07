//
//  rmi_driver.c
//  VoodooSMBus
//
//  Created by Gwy on 5/6/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#include "rmi_driver.hpp"

#define HAS_NONSTANDARD_PDT_MASK 0x40
#define RMI4_MAX_PAGE 0xff
#define RMI4_PAGE_SIZE 0x100
#define RMI4_PAGE_MASK 0xFF00

#define RMI_DEVICE_RESET_CMD    0x01
#define DEFAULT_RESET_DELAY_MS    100

static int rmi_driver_probe(RMIBus *dev)
{
    rmi_driver_data *data;
    struct rmi_device_platform_data *pdata;
    int retval;
    
    IOLog("Starting probe");
    
//    pdata = rmi_get_platform_data(rmi_dev);
    
    void* ptr =
        (IOMalloc(sizeof(struct rmi_driver_data)));
    
    memset(ptr, 0, sizeof(struct rmi_driver_data));
    
    data = reinterpret_cast<rmi_driver_data *>(ptr);
    
    if (!data)
        return -ENOMEM;
    
    // TODO:
//    INIT_LIST_HEAD(&data->function_list);
    data->rmi_dev = dev;
    dev->data = data;
    
    /*
     * Right before a warm boot, the sensor might be in some unusual state,
     * such as F54 diagnostics, or F34 bootloader mode after a firmware
     * or configuration update.  In order to clear the sensor to a known
     * state and/or apply any updates, we issue a initial reset to clear any
     * previous settings and force it into normal operation.
     *
     * We have to do this before actually building the PDT because
     * the reflash updates (if any) might cause various registers to move
     * around.
     *
     * For a number of reasons, this initial reset may fail to return
     * within the specified time, but we'll still be able to bring up the
     * driver normally after that failure.  This occurs most commonly in
     * a cold boot situation (where then firmware takes longer to come up
     * than from a warm boot) and the reset_delay_ms in the platform data
     * has been set too short to accommodate that.  Since the sensor will
     * eventually come up and be usable, we don't want to just fail here
     * and leave the customer's device unusable.  So we warn them, and
     * continue processing.
     */
    
    retval = rmi_scan_pdt(dev, NULL, rmi_initial_reset);
    if (retval < 0)
        IOLog("RMI initial reset failed! Continuing in spite of this.\n");
    
    retval = dev->read(PDT_PROPERTIES_LOCATION, &data->pdt_props);
    if (retval < 0) {
        /*
         * we'll print out a warning and continue since
         * failure to get the PDT properties is not a cause to fail
         */
        IOLog("Could not read PDT properties from %#06x (code %d). Assuming 0x00.\n",
                 PDT_PROPERTIES_LOCATION, retval);
    }
    
    IOSimpleLockInit(data->irq_mutex);
    IOSimpleLockInit(data->enabled_mutex);
    
    retval = rmi_probe_interrupts(data);
    if (retval)
        goto err;
    
    // allocate device
    
    retval = rmi_init_functions(data);
    if (retval)
        goto err;
    
    retval = rmi_irq_init(dev);
    if (retval < 0)
        goto err_destroy_functions;
    
    retval = rmi_enable_sensor(dev);
    if (retval)
        goto err_disable_irq;
    
    return 0;
    
err_disable_irq:
    rmi_disable_irq(dev, false);
err_destroy_functions:
    rmi_free_function_list(rmi_dev);
err:
    return retval;
}

#define RMI_SCAN_CONTINUE    0
#define RMI_SCAN_DONE        1


static int rmi_read_pdt_entry(RMIBus *rmi_dev,
                              struct pdt_entry *entry, u16 pdt_address)
{
    u8 buf[RMI_PDT_ENTRY_SIZE];
    int error;
    
    error = rmi_dev->readBlock(pdt_address, buf, RMI_PDT_ENTRY_SIZE);
    if (error) {
        IOLogError("Read PDT entry at %#06x failed, code: %d.\n", pdt_address, error);
        return error;
    }
    
    entry->page_start = pdt_address & RMI4_PAGE_MASK;
    entry->query_base_addr = buf[0];
    entry->command_base_addr = buf[1];
    entry->control_base_addr = buf[2];
    entry->data_base_addr = buf[3];
    entry->interrupt_source_count = buf[4] & RMI_PDT_INT_SOURCE_COUNT_MASK;
    entry->function_version = (buf[4] & RMI_PDT_FUNCTION_VERSION_MASK) >> 5;
    entry->function_number = buf[5];
    
    return 0;
}

static int rmi_scan_pdt_page(RMIBus *dev,
                             int page,
                             int *empty_pages,
                             void *ctx,
                             int (*callback)(RMIBus *rmi_dev,
                                             void *ctx,
                                             const struct pdt_entry *entry))
{
    rmi_driver_data *data = dev->data;
    struct pdt_entry pdt_entry;
    u16 page_start = RMI4_PAGE_SIZE * page;
    u16 pdt_start = page_start + PDT_START_SCAN_LOCATION;
    u16 pdt_end = page_start + PDT_END_SCAN_LOCATION;
    u16 addr;
    int error;
    int retval;
    
    for (addr = pdt_start; addr >= pdt_end; addr -= RMI_PDT_ENTRY_SIZE) {
        error = rmi_read_pdt_entry(dev, &pdt_entry, addr);
        if (error)
            return error;
        
        if (RMI4_END_OF_PDT(pdt_entry.function_number))
            break;
        
        retval = callback(dev, ctx, &pdt_entry);
        if (retval != RMI_SCAN_CONTINUE)
            return retval;
    }
    
    /*
     * Count number of empty PDT pages. If a gap of two pages
     * or more is found, stop scanning.
     */
    if (addr == pdt_start)
        ++*empty_pages;
    else
        *empty_pages = 0;
    
    return (data->bootloader_mode || *empty_pages >= 2) ?
    RMI_SCAN_DONE : RMI_SCAN_CONTINUE;
}

int rmi_scan_pdt(RMIBus *dev, void *ctx,
                 int (*callback)(RMIBus* dev,
                                 void *ctx, const struct pdt_entry *entry))
{
    int page;
    int empty_pages = 0;
    int retval = RMI_SCAN_DONE;
    
    for (page = 0; page <= RMI4_MAX_PAGE; page++) {
        retval = rmi_scan_pdt_page(dev, page, &empty_pages,
                                   ctx, callback);
        if (retval != RMI_SCAN_CONTINUE)
            break;
    }
    
    return retval < 0 ? retval : 0;
}

int rmi_initial_reset (RMIBus *dev, void *ctx, pdt_entry *pdt)
{
    int error;
    
    if (pdt->function_number == 0x01) {
        u16 cmd_addr = pdt->page_start + pdt->command_base_addr;
        u8 cmd_buf = RMI_DEVICE_RESET_CMD;
        
        IOLog("Sending Reset\n");
        error = dev->blockWrite(cmd_addr, &cmd_buf, 1);
        if (error) {
            IOLogError("Initial reset failed. Code = %d\n", error);
            return error;
        }
        
        mdelay(pdata->reset_delay_ms ?: DEFAULT_RESET_DELAY_MS);
        
        return RMI_SCAN_DONE;
    }
    
    /* F01 should always be on page 0. If we don't find it there, fail. */
    return pdt->page_start == 0 ? RMI_SCAN_CONTINUE : -ENODEV;
}

static int rmi_check_bootloader_mode(RMIBus *rmi_dev,
                                     const struct pdt_entry *pdt)
{
    struct rmi_driver_data *data = rmi_dev->data;
    int ret;
    u8 status;
    
    if (pdt->function_number == 0x34 && pdt->function_version > 1) {
        ret = rmi_dev->read(pdt->data_base_addr, &status);
        if (ret) {
            IOLogError("Failed to read F34 status: %d\n", ret);
            return ret;
        }
        
        if (status & BIT(7))
            data->bootloader_mode = true;
    } else if (pdt->function_number == 0x01) {
        ret = rmi_dev->read(pdt->data_base_addr, &status);
        if (ret) {
            IOLogError("Failed to read F01 status: %d.\n", ret);
            return ret;
        }
        
        if (status & BIT(6))
            data->bootloader_mode = true;
    }
    
    return 0;
}

static int rmi_count_irqs(RMIBus *rmi_dev,
                          void *ctx, const struct pdt_entry *pdt)
{
    int *irq_count = reinterpret_cast<int *>(ctx);
    int ret;
    
    *irq_count += pdt->interrupt_source_count;
    
    ret = rmi_check_bootloader_mode(rmi_dev, pdt);
    if (ret < 0)
        return ret;
    
    return RMI_SCAN_CONTINUE;
}

int rmi_probe_interrupts(rmi_driver_data *data)
{
    RMIBus *rmi_dev = data->rmi_dev;
    int irq_count = 0;
    size_t size;
    int retval;
    
    /*
     * We need to count the IRQs and allocate their storage before scanning
     * the PDT and creating the function entries, because adding a new
     * function can trigger events that result in the IRQ related storage
     * being accessed.
     */
    IOLog("%s: Counting IRQs.\n", __func__);
    data->bootloader_mode = false;
    
    retval = rmi_scan_pdt(rmi_dev, &irq_count, rmi_count_irqs);
    if (retval < 0) {
        IOLogError("IRQ counting failed with code %d.\n", retval);
        return retval;
    }
    
    if (data->bootloader_mode)
        IOLogDebug("Device in bootloader mode.\n");
    
    /* Allocate and register a linear revmap irq_domain */
    // TODO: IRQs
    //    data->irqdomain = irq_domain_create_linear(fwnode, irq_count,
//                                               &irq_domain_simple_ops,
//                                               data);
    if (!data->irqdomain) {
        IOLogError("Failed to create IRQ domain\n");
        return -ENOMEM;
    }
    
    data->irq_count = irq_count;
    data->num_of_irq_regs = (data->irq_count + 7) / 8;
    
    size = BITS_TO_LONGS(data->irq_count) * sizeof(unsigned long);
    
    data->irq_memory = reinterpret_cast<unsigned long *>(IOMalloc(size * 4));
    memset(data->irq_memory, 0, size * 4);
    if (!data->irq_memory) {
        IOLogError("Failed to allocate memory for irq masks.\n");
        return -ENOMEM;
    }
    
    data->irq_status        = data->irq_memory + size * 0;
    data->fn_irq_bits       = data->irq_memory + size * 1;
    data->current_irq_mask  = data->irq_memory + size * 2;
    data->new_irq_mask      = data->irq_memory + size * 3;
    
    return retval;
}

static void rmi_driver_copy_pdt_to_fd(const struct pdt_entry *pdt,
                                      struct rmi_function_descriptor *fd)
{
    fd->query_base_addr = pdt->query_base_addr + pdt->page_start;
    fd->command_base_addr = pdt->command_base_addr + pdt->page_start;
    fd->control_base_addr = pdt->control_base_addr + pdt->page_start;
    fd->data_base_addr = pdt->data_base_addr + pdt->page_start;
    fd->function_number = pdt->function_number;
    fd->interrupt_source_count = pdt->interrupt_source_count;
    fd->function_version = pdt->function_version;
}

// bits.h

#define BITS_PER_LONG       (__CHAR_BIT__ * __SIZEOF_LONG__)
#define BIT_MASK(nr)        ((1) << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)        ((nr) / BITS_PER_LONG)

// asm-generic/bitops/atomic.h
/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void set_bit(int nr, volatile unsigned long *addr)
{
    unsigned long mask = static_cast<unsigned long>(1) << (nr % BITS_PER_LONG);
    unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
    OSBitOrAtomic64(mask, p);
}

// end atomic.h

static int rmi_create_function(RMIBus *rmi_dev,
                               void *ctx, const struct pdt_entry *pdt)
{
    struct rmi_driver_data *data = rmi_dev->data;
    int *current_irq_count = reinterpret_cast<int *>(ctx);
    struct rmi_function *fn;
    int i;
    int error;
    
    IOLog("Initializing F%02X.\n", pdt->function_number);

    fn = reinterpret_cast<rmi_function*>(IOMalloc(sizeof(rmi_function) +
                 BITS_TO_LONGS(data->irq_count) * sizeof(unsigned long)));
    
    memset (fn, 0, sizeof(rmi_function) +
            BITS_TO_LONGS(data->irq_count) * sizeof(unsigned long));
    
    if (!fn) {
        IOLogError("Failed to allocate memory for F%02X\n", pdt->function_number);
        return -ENOMEM;
    }
    
    INIT_LIST_HEAD(&fn->node);
    rmi_driver_copy_pdt_to_fd(pdt, &fn->fd);
    
    fn->dev = rmi_dev;
    
    fn->num_of_irqs = pdt->interrupt_source_count;
    fn->irq_pos = *current_irq_count;
    *current_irq_count += fn->num_of_irqs;
    
    for (i = 0; i < fn->num_of_irqs; i++)
        set_bit(fn->irq_pos + i, fn->irq_mask);
    
    error = rmi_register_function(fn);
    if (error)
        return error;
    
    if (pdt->function_number == 0x01)
        data->f01_container = fn;
    else if (pdt->function_number == 0x34)
        data->f34_container = fn;
    
    list_add_tail(&fn->node, &data->function_list);
    
    return RMI_SCAN_CONTINUE;
}


int rmi_init_functions(rmi_driver_data *data)
{
    RMIBus *rmi_dev = data->rmi_dev;
    int irq_count = 0;
    int retval;
    
    IOLogDebug("%s: Creating functions.\n", __func__);
    retval = rmi_scan_pdt(rmi_dev, &irq_count, rmi_create_function);
    if (retval < 0) {
        IOLogError("Function creation failed with code %d.\n", retval);
        goto err_destroy_functions;
    }
    
    if (!data->f01_container) {
        IOLogError("Missing F01 container!\n");
        retval = -EINVAL;
        goto err_destroy_functions;
    }
    
    retval = rmi_dev->readBlock(
                            data->f01_container->fd.control_base_addr + 1,
                            data->current_irq_mask, data->num_of_irq_regs);
    if (retval < 0) {
        IOLogError("%s: Failed to read current IRQ mask.\n", __func__);
        goto err_destroy_functions;
    }
    
    return 0;
    
err_destroy_functions:
    rmi_free_function_list(rmi_dev);
    return retval;
}

