#include "fspusb_drive.hpp"
#include "fspusb_usb_manager.hpp"

namespace ams::mitm::fspusb::impl {

    Result Drive::Mount() {
        if (!this->mounted) {
            R_UNLESS(this->IsSCSIOk(), ResultDriveInitializationFailure());

            /* Try to find a mountable index */
            if (FindAndMountAtIndex(&this->mounted_idx)) {
                FormatDriveMountName(this->mount_name, this->mounted_idx);
                FSP_USB_LOG("%s (interface ID %d): drive mount name -> \"%s\".", __func__, this->GetInterfaceId(), this->mount_name);
                
                auto ffrc = f_mount(&this->fat_fs, this->mount_name, 1);
                FSP_USB_LOG("%s (interface ID %d): f_mount returned %u.", __func__, this->GetInterfaceId(), ffrc);

                R_TRY(fspusb::result::CreateFromFATFSError(ffrc));
                
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

    void Drive::DisposeInterfaces() {
        usbHsEpClose(&this->usb_in_endpoint);
        usbHsEpClose(&this->usb_out_endpoint);
        usbHsIfClose(&this->usb_interface);
    }

}