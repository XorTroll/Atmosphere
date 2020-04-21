
#pragma once
#include <thread>
#include <memory>
#include "../fatfs/ff.h"
#include "../fatfs/diskio.h"
#include "fspusb_utils.hpp"
#include "fspusb_scsi.hpp"

namespace ams::mitm::fspusb::impl {

    /* Maximum amount of drives, basically FATFS's volume number */
    constexpr u32 DriveMax = FF_VOLUMES;

    /* Used for drive mounting as an invalid index */
    constexpr u32 InvalidMountedIndex = UINT32_MAX;

    class Drive {
            NON_COPYABLE(Drive);
            NON_MOVEABLE(Drive);
        private:
            UsbHsClientIfSession usb_interface;
            UsbHsClientEpSession usb_in_endpoint;
            UsbHsClientEpSession usb_out_endpoint;
            
            FATFS fat_fs;
            ams::os::Mutex fat_fs_lock;
            u32 mounted_idx;
            char mount_name[0x10];
            SCSIContext scsi_context;
            bool mounted;

        public:
            Drive(UsbHsClientIfSession interface, UsbHsClientEpSession in_ep, UsbHsClientEpSession out_ep, u8 lun) : usb_interface(interface), usb_in_endpoint(in_ep), usb_out_endpoint(out_ep), fat_fs_lock(true), mounted_idx(InvalidMountedIndex), scsi_context(&this->usb_interface, &this->usb_in_endpoint, &this->usb_out_endpoint, lun), mounted(false) {}

            Result Mount();
            void Unmount();
            void DisposeInterfaces();

            inline s32 GetInterfaceId() {
                return this->usb_interface.ID;
            }

            inline u32 GetMountedIndex() {
                return this->mounted_idx;
            }

            inline u32 GetBlockSize() {
                return this->scsi_context.GetBlockSize();
            }

            inline bool IsSCSIOk() {
                return this->scsi_context.Ok();
            }

            inline u32 ReadSectors(u8 *buffer, u64 sector_offset, u32 num_sectors) {
                return this->scsi_context.ReadSectors(buffer, sector_offset, num_sectors);
            }

            inline u32 WriteSectors(const u8 *buffer, u64 sector_offset, u32 num_sectors) {
                return this->scsi_context.WriteSectors(buffer, sector_offset, num_sectors);
            }

            inline void DoWithFATFS(std::function<void(FATFS*)> fn) {
                std::scoped_lock lk(this->fat_fs_lock);
                fn(&this->fat_fs);
            }

            const char *GetMountName() {
                return this->mount_name;
            }
    };

}