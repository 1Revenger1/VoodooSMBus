//
//  rmi_smbus.c
//  VoodooSMBus
//
//  Created by Gwy on 5/4/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#include "rmi_smbus.hpp"

static int rmi_smb_get_version(VoodooSMBusDeviceNub* dev)
{
    int retval;
    
    /* Check if for SMBus new version device by reading version byte. */
    retval = dev->readByteData(SMB_PROTOCOL_VERSION_ADDRESS);
    if (retval < 0) {
        IOLog("Failed to get SMBus version number!\n");
        return retval;
    }
    
    return retval + 1;
}

int RMIBus::blockWrite(u8 command, const u8 *buf, size_t len)
{
    int retval;
    
    retval = device_nub->writeBlockData(command, len, buf);
    if (retval < 0) {
        IOLog("Failed to write block to SMBus");
    }
    
    return retval;
}

/*
 * The function to get command code for smbus operations and keeps
 * records to the driver mapping table
 */
static int rmi_smb_get_command_code(RMIBus *dev,
                                    u16 rmiaddr, int bytecount, bool isread, u8 *commandcode)
{
    struct mapping_table_entry new_map;
    u8 i;
    int retval = 0;
    
    IOSimpleLockLock(dev->mapping_table_mutex);
    
    for (i = 0; i < RMI_SMB2_MAP_SIZE; i++) {
        struct mapping_table_entry *entry = &dev->mapping_table[i];
        
        if (le16_to_cpu(entry->rmiaddr) == rmiaddr) {
            if (isread) {
                if (entry->readcount == bytecount)
                    goto exit;
            } else {
                if (entry->flags & RMI_SMB2_MAP_FLAGS_WE) {
                    goto exit;
                }
            }
        }
    }
    
    i = dev->table_index;
    dev->table_index = (i + 1) % RMI_SMB2_MAP_SIZE;
    
    /* constructs mapping table data entry. 4 bytes each entry */
    memset(&new_map, 0, sizeof(new_map));
    new_map.rmiaddr = cpu_to_le16(rmiaddr);
    new_map.readcount = bytecount;
    new_map.flags = !isread ? RMI_SMB2_MAP_FLAGS_WE : 0;
    
    retval = dev->blockWrite(i + 0x80,
                            reinterpret_cast<const u8*>(&new_map), sizeof(new_map));
    if (retval < 0) {
        /*
         * if not written to device mapping table
         * clear the driver mapping table records
         */
        memset(&new_map, 0, sizeof(new_map));
    }
    
    /* save to the driver level mapping table */
    dev->mapping_table[i] = new_map;
    
exit:
    IOSimpleLockFree(dev->mapping_table_mutex);
    
    if (retval < 0)
        return retval;
    
    *commandcode = i;
    return 0;
}

int RMIBus::readBlock(u16 rmiaddr, u8 *databuff, size_t len) {
    int retval;
    u8 commandcode;
    int cur_len = (int)len;
    
    IOSimpleLockLock(page_mutex);
    memset(databuff, 0, len);
    
    while (cur_len > 0) {
        /* break into 32 bytes chunks to write get command code */
        int block_len =  min(cur_len, SMB_MAX_COUNT);
        
        retval = rmi_smb_get_command_code(this, rmiaddr, block_len,
                                          true, &commandcode);
        if (retval < 0)
            goto exit;
        
        retval = readBlock(commandcode,
                                databuff, block_len);
        if (retval < 0)
            goto exit;
        
        /* prepare to read next block of bytes */
        cur_len -= SMB_MAX_COUNT;
        databuff += SMB_MAX_COUNT;
        rmiaddr += SMB_MAX_COUNT;
    }
    
    retval = 0;
    
exit:
    IOSimpleLockFree(page_mutex);
    return retval;
}

