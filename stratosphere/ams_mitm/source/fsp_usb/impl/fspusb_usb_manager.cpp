#include "fspusb_usb_manager.hpp"
#include <vector>
#include <array>
#include <memory>

namespace ams::mitm::fspusb::impl {

    ams::os::Mutex g_usb_manager_lock(true);
    ams::os::ThreadType g_usb_update_thread;
    alignas(os::ThreadStackAlignment) u8 g_usb_update_thread_stack[16_KB];

    std::vector<std::unique_ptr<Drive>> g_usb_manager_drives;
    Event g_usb_manager_interface_available_event;
    Event g_usb_manager_thread_exit_event;
    UsbHsInterfaceFilter g_usb_manager_device_filter;
    bool g_usb_manager_initialized = false;
    
    std::array<bool, DriveMax> g_usb_manager_mounted_index_array;

    void UpdateDrives() {
        std::scoped_lock lk(g_usb_manager_lock);

        UsbHsInterface iface_block[DriveMax];
        size_t iface_block_size = DriveMax * sizeof(UsbHsInterface);
        std::memset(iface_block, 0, iface_block_size);
        s32 iface_count = 0;

        /* TODO: refactor with R_TRY */
        
        Result rc;
        
        FSP_USB_LOG("%s: acquired drive count -> %lu.", __func__, g_usb_manager_drives.size());
        
        if (!g_usb_manager_drives.empty()) {
            FSP_USB_LOG("%s: checking interfaces from previously acquired drives.", __func__);
            
            std::vector<std::unique_ptr<Drive>> valid_drives;
            rc = usbHsQueryAcquiredInterfaces(iface_block, iface_block_size, &iface_count);
            if (R_SUCCEEDED(rc)) {
                for(auto &drive: g_usb_manager_drives) {
                    /* For each drive in our list, check whether it is still available (by looping through actual acquired interfaces) */
                    bool ok = false;
                    
                    for(s32 i = 0; i < iface_count; i++) {
                        if (iface_block[i].inf.ID == drive->GetInterfaceId()) {
                            ok = true;
                            break;
                        }
                    }
                    
                    if (ok) {
                        FSP_USB_LOG("%s: drive with interface ID %d still available.", __func__, drive->GetInterfaceId());
                        valid_drives.push_back(std::move(drive));
                    } else {
                        FSP_USB_LOG("%s: drive with interface ID %d not available anymore.", __func__, drive->GetInterfaceId());
                        drive->Unmount();
                        drive->DisposeInterfaces();
                    }
                }
            } else {
                FSP_USB_LOG("%s: usbHsQueryAcquiredInterfaces returned 0x%08X.", __func__, rc.GetValue());
            }
            
            g_usb_manager_drives.clear();
            for(auto &drive: valid_drives) {
                g_usb_manager_drives.push_back(std::move(drive));
            }
            valid_drives.clear();
        }
        
        std::memset(iface_block, 0, iface_block_size);
        
        /* Check new ones and (try to) acquire them */
        rc = usbHsQueryAvailableInterfaces(&g_usb_manager_device_filter, iface_block, iface_block_size, &iface_count);
        if (R_FAILED(rc)) {
            FSP_USB_LOG("%s: usbHsQueryAvailableInterfaces returned 0x%08X.", __func__, rc.GetValue());
            return;
        }
        
        for(s32 i = 0; i < iface_count; i++) {
            UsbHsClientIfSession iface;
            UsbHsClientEpSession inep;
            UsbHsClientEpSession outep;
            
            bool in_ep_opened = false;
            bool out_ep_opened = false;
            bool bulk_reset = false;
            
            rc = usbHsAcquireUsbIf(&iface, &iface_block[i]);
            if (R_FAILED(rc)) {
                FSP_USB_LOG("%s: usbHsAcquireUsbIf returned 0x%08X for enumerated interface #%d.", __func__, rc.GetValue(), i);
                continue;
            }
            
            for(u32 j = 0; j < 15; j++) {
                auto epd = &iface.inf.inf.input_endpoint_descs[j];
                if (epd->bLength > 0) {
                    in_ep_opened = R_SUCCEEDED(usbHsIfOpenUsbEp(&iface, &outep, 1, epd->wMaxPacketSize, epd));
                    break;
                }
            }
            
            for(u32 j = 0; j < 15; j++) {
                auto epd = &iface.inf.inf.output_endpoint_descs[j];
                if (epd->bLength > 0) {
                    out_ep_opened = R_SUCCEEDED(usbHsIfOpenUsbEp(&iface, &inep, 1, epd->wMaxPacketSize, epd));
                    break;
                }
            }

            bool drive_ok = false;
            
            /* Check if we opened our I/O endpoints */
            if (in_ep_opened && out_ep_opened) {
                /* Retrieve device configuration */
                /* Might fail, in which case just zero is returned */
                u8 conf = GetUSBConfiguration(&iface);
                FSP_USB_LOG("%s: enumerated interface #%d (ID %d) config -> 0x%02X | desirable config -> 0x%02X.", __func__, i, iface.ID, conf, iface.inf.config_desc.bConfigurationValue);
                
                /* Change the current configuration if it doesn't match our desired one */
                if (conf != iface.inf.config_desc.bConfigurationValue) {
                    FSP_USB_LOG("%s: changing config for enumerated interface #%d (ID %d).", __func__, i, iface.ID);
                    rc = SetUSBConfiguration(&iface, iface.inf.config_desc.bConfigurationValue);
                    bulk_reset = true;
                }
                
                if (R_SUCCEEDED(rc)) {
                    /* Check if there's an alternate interface available and set it */
                    /* Some devices use the default pipes as interrupt pipes - we actually want to use bulk pipes */
                    FSP_USB_LOG("%s: enumerated interface #%d (ID %d) alternate setting -> 0x%02X.", __func__, i, iface.ID, iface.inf.inf.interface_desc.bAlternateSetting);
                    
                    if (iface.inf.inf.interface_desc.bAlternateSetting != 0) {
                        FSP_USB_LOG("%s: setting alternate setting for enumerated interface #%d (ID %d).", __func__, i, iface.ID);
                        rc = SetUSBAlternativeInterface(&iface, iface.inf.inf.interface_desc.bAlternateSetting);
                        bulk_reset = true;
                    }
                    
                    if (R_SUCCEEDED(rc)) {
                        if (bulk_reset) {
                            /* Perform a bulk storage reset, if needed */
                            FSP_USB_LOG("%s: performing bulk-only mass storage reset on enumerated interface #%d (ID %d).", __func__, i, iface.ID);
                            rc = ResetBulkStorage(&iface, &inep, &outep);
                        }
                        
                        if (R_SUCCEEDED(rc)) {
                            /* Retrieve max LUN count from this drive */
                            u8 max_lun = GetMaxLUN(&iface);
                            FSP_USB_LOG("%s: enumerated interface #%d (ID %d) max LUN count -> %u.", __func__, i, iface.ID, max_lun);
                            
                            /* Clear possible STALL status from bulk pipes */
                            /* Not all devices support the max LUN request */
                            ClearEndpointHalt(&iface, &inep);
                            ClearEndpointHalt(&iface, &outep);
                            
                            /* Try to mount each LUN until one of them succeeds */
                            for(u8 j = 0; j < max_lun; j++) {
                                /* Since FATFS reads from drives in the vector and we need to mount it, push it to the vector first. */
                                /* Then, if it didn't mount correctly, pop from the vector and close the interface. */
                                auto drv = std::make_unique<Drive>(iface, inep, outep, j);
                                g_usb_manager_drives.push_back(std::move(drv));
                                
                                auto &drive_ref = g_usb_manager_drives.back();
                                rc = drive_ref->Mount();
                                if (R_SUCCEEDED(rc)) {
                                    drive_ok = true;
                                    break;
                                }

                                /* Failed to mount, remove from the vector. */
                                g_usb_manager_drives.pop_back();
                            }
                            
                            FSP_USB_LOG("%s: %s drive on enumerated interface #%d (ID %d).", __func__, (drive_ok ? "successfully mounted" : "failed to mount"), i, iface.ID);
                        }
                    }
                }
            }
            
            if (!drive_ok) {
                /* Something failed, so let's close the interfaces we opened. */
                FSP_USB_LOG("%s: Last result -> 0x%08X.", __func__, rc.GetValue());
                if (in_ep_opened) {
                    usbHsEpClose(&outep);
                }
                if (out_ep_opened) {
                    usbHsEpClose(&inep);
                }
                usbHsIfClose(&iface);
            }
        }
    }

    void ManagerUpdateThread(void *arg) {
        Result rc;
        s32 idx;
        
        while(true) {
            /* Wait until one of our events is triggered */
            idx = 0;
            rc = waitMulti(&idx, -1, waiterForEvent(usbHsGetInterfaceStateChangeEvent()), waiterForEvent(&g_usb_manager_interface_available_event), waiterForEvent(&g_usb_manager_thread_exit_event));
            if (R_SUCCEEDED(rc)) {
                FSP_USB_LOG("%s: triggered event index -> %d (%s).", __func__, idx, (idx == 0 ? "interface state change" : (idx == 1 ? "filtered interface available" : "exit")));
                
                /* Clear InterfaceStateChangeEvent if it was triggered (not an autoclear event) */
                if (idx == 0) {
                    eventClear(usbHsGetInterfaceStateChangeEvent());
                }
                
                /* Break out of the loop if the thread exit user event was fired */
                if (idx == 2) {
                    break;
                }
                
                /* Update drives */
                UpdateDrives();
            } else {
                FSP_USB_LOG("%s: waitMulti returned 0x%08X.", __func__, rc.GetValue());
            }
        }
    }

    u32 FindNextMountableIndex() {
        for(u32 i = 0; i < DriveMax; i++) {
            if (!g_usb_manager_mounted_index_array[i]) {
                return i;
            }
        }
        /* All mounted */
        return InvalidMountedIndex;
    }

    bool MountAtIndex(u32 idx) {
        if (idx >= DriveMax) {
            /* Invalid index */
            return false;
        }
        if (g_usb_manager_mounted_index_array[idx]) {
            /* Already mounted here */
            return false;
        }
        g_usb_manager_mounted_index_array[idx] = true;
        return true;
    }

    Result InitializeManager() {
        std::scoped_lock lk(g_usb_manager_lock);

        if (!g_usb_manager_initialized) {
            /* No drives mounted, set all the mounted indexes' status to false */
            for(u32 i = 0; i < DriveMax; i++) {
                g_usb_manager_mounted_index_array[i] = false;
            }

            std::memset(&g_usb_manager_device_filter, 0, sizeof(UsbHsInterfaceFilter));
            std::memset(&g_usb_manager_interface_available_event, 0, sizeof(Event));
            std::memset(&g_usb_manager_thread_exit_event, 0, sizeof(Event));

            R_TRY(usbHsInitialize());

            g_usb_manager_device_filter = {};
            g_usb_manager_device_filter.Flags = UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bInterfaceSubClass | UsbHsInterfaceFilterFlags_bInterfaceProtocol;
            g_usb_manager_device_filter.bInterfaceClass = USB_CLASS_MASS_STORAGE;
            g_usb_manager_device_filter.bInterfaceSubClass = MASS_STORAGE_SCSI_COMMANDS;
            g_usb_manager_device_filter.bInterfaceProtocol = MASS_STORAGE_BULK_ONLY;
            
            R_TRY(usbHsCreateInterfaceAvailableEvent(&g_usb_manager_interface_available_event, true, 0, &g_usb_manager_device_filter));

            R_TRY(eventCreate(&g_usb_manager_thread_exit_event, true));

            R_TRY(os::CreateThread(&g_usb_update_thread, &ManagerUpdateThread, nullptr, g_usb_update_thread_stack, sizeof(g_usb_update_thread_stack), -7));
            os::StartThread(&g_usb_update_thread);
            
            g_usb_manager_initialized = true;
        }

        return ResultSuccess();
    }

    void FinalizeManager() {
        if (g_usb_manager_initialized) {
            for(auto &drive: g_usb_manager_drives) {
                drive->Unmount();
                drive->DisposeInterfaces();
            }
            
            g_usb_manager_drives.clear();
            
            /* Fire thread exit user event, wait until the thread exits and close the event */
            eventFire(&g_usb_manager_thread_exit_event);
            os::WaitThread(&g_usb_update_thread);
            eventClose(&g_usb_manager_thread_exit_event);
            
            usbHsDestroyInterfaceAvailableEvent(&g_usb_manager_interface_available_event, 0);
            usbHsExit();
            
            g_usb_manager_initialized = false;
        }
    }

    void DoUpdateDrives() {
        UpdateDrives();
    }

    size_t GetAcquiredDriveCount() {
        std::scoped_lock lk(g_usb_manager_lock);
        return g_usb_manager_drives.size();
    }

    bool FindAndMountAtIndex(u32 *out_mounted_idx) {
        u32 idx = FindNextMountableIndex();
        /* If FindNextMountableIndex returns InvalidMountedIndex (unable to find index), it will fail here. */
        bool ok = MountAtIndex(idx);
        if (ok) {
            *out_mounted_idx = idx;
        }
        return ok;
    }

    void UnmountAtIndex(u32 mounted_idx) {
        if (mounted_idx < DriveMax) {
            g_usb_manager_mounted_index_array[mounted_idx] = false;
        }
    }

    bool IsDriveInterfaceIdValid(s32 drive_interface_id) {
        std::scoped_lock lk(g_usb_manager_lock);
        for(auto &drive: g_usb_manager_drives) {
            if (drive_interface_id == drive->GetInterfaceId()) {
                return true;
            }
        }
        return false;
    }

    u32 GetDriveMountedIndex(s32 drive_interface_id) {
        std::scoped_lock lk(g_usb_manager_lock);
        for(auto &drive: g_usb_manager_drives) {
            if (drive_interface_id == drive->GetInterfaceId()) {
                return drive->GetMountedIndex();
            }
        }
        return InvalidMountedIndex;
    }

    s32 GetDriveInterfaceId(u32 drive_idx) {
        std::scoped_lock lk(g_usb_manager_lock);
        if (drive_idx < g_usb_manager_drives.size()) {
            auto &drive = g_usb_manager_drives.at(drive_idx);
            return drive->GetInterfaceId();
        }
        return 0;
    }

    void DoWithDrive(s32 drive_interface_id, std::function<void(std::unique_ptr<Drive>&)> fn) {
        std::scoped_lock lk(g_usb_manager_lock);
        for(auto &drive: g_usb_manager_drives) {
            if (drive_interface_id == drive->GetInterfaceId()) {
                fn(drive);
                break;
            }
        }
    }

    void DoWithDriveMountedIndex(u32 drive_mounted_idx, std::function<void(std::unique_ptr<Drive>&)> fn) {
        std::scoped_lock lk(g_usb_manager_lock);
        for(auto &drive: g_usb_manager_drives) {
            if (drive_mounted_idx == drive->GetMountedIndex()) {
                fn(drive);
                break;
            }
        }
    }

    void DoWithDriveFATFS(s32 drive_interface_id, std::function<void(FATFS*)> fn) {
        DoWithDrive(drive_interface_id, [&](std::unique_ptr<Drive> &drive_ptr) {
            drive_ptr->DoWithFATFS(fn);
        });
    }

}