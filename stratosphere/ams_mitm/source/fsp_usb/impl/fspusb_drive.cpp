#include "fspusb_drive.hpp"
#include "fspusb_usb_manager.hpp"

namespace ams::mitm::fspusb::impl {

    Drive::Drive(UsbHsClientIfSession interface, UsbHsClientEpSession in_ep, UsbHsClientEpSession out_ep, u8 lun) : fs_lock(false), usb_interface(interface), usb_in_endpoint(in_ep), usb_out_endpoint(out_ep), mounted_idx(InvalidMountedIndex), scsi_context(nullptr), mounted(false) {
        this->scsi_context = new SCSIDriveContext(&this->usb_interface, &this->usb_in_endpoint, &this->usb_out_endpoint, lun);
    }

    Result Drive::Mount() {

        if (!this->mounted) {
            R_UNLESS(this->scsi_context->Ok(), ResultDriveInitializationFailure());

            /* Try to find a mountable index */
            if (FindAndMountAtIndex(&this->mounted_idx)) {
                FormatDriveMountName(this->mount_name, this->mounted_idx);
                FSP_USB_LOG("%s (interface ID %d): drive mount name -> \"%s\".", __func__, this->GetInterfaceId(), this->mount_name);
                
                auto ffrc = f_mount(&this->fat_fs, this->mount_name, 1);
                FSP_USB_LOG("%s (interface ID %d): f_mount returned %u.", __func__, this->GetInterfaceId(), ffrc);

                R_TRY(fspusb::result::CreateFromFRESULT(ffrc));
                
                this->mounted = true;
            }
        }

        return ResultSuccess();
    }

    void Drive::Unmount() {
        if (this->mounted) {
            UnmountAtIndex(this->mounted_idx);
            f_mount(nullptr, this->mount_name, 1);
            std::memset(&this->fat_fs, 0, sizeof(this->fat_fs));
            std::memset(this->mount_name, 0, 0x10);
            this->mounted = false;
        }
    }

    void Drive::Dispose(bool close_usbhs) {
        if (this->scsi_context != nullptr) {
            delete this->scsi_context;
            this->scsi_context = nullptr;
        }
        
        if (close_usbhs) {
            usbHsEpClose(&this->usb_in_endpoint);
            usbHsEpClose(&this->usb_out_endpoint);
            usbHsIfClose(&this->usb_interface);
        }
    }

}