#include "fspusb_service.hpp"

namespace ams::mitm::fspusb {

    void Service::ListMountedDrives(const sf::OutArray<s32> &out_interface_ids, sf::Out<s32> out_count) {
        FSP_USB_LOG("%s: forcing a mounted drive list update.", __func__);
        impl::DoUpdateDrives();

        auto acquired_drive_count = impl::GetAcquiredDriveCount();
        auto array_drive_count = out_interface_ids.GetSize();
        auto drive_count = std::min(acquired_drive_count, array_drive_count);
        FSP_USB_LOG("%s: acquired drive count -> %lu | array drive count -> %lu.", __func__, acquired_drive_count, array_drive_count);

        for(u32 i = 0; i < drive_count; i++) {
            out_interface_ids[i] = impl::GetDriveInterfaceId(i);
            FSP_USB_LOG("%s: interface ID #%u -> %d.", __func__, i, out_interface_ids[i]);
        }

        out_count.SetValue(static_cast<s32>(drive_count));
    }

    Result Service::GetDriveFileSystemType(s32 drive_interface_id, sf::Out<u8> out_fs_type) {
        FSP_USB_LOG("%s (interface ID %d): forcing a mounted drive list update.", __func__, drive_interface_id);
        impl::DoUpdateDrives();
        R_UNLESS(impl::IsDriveInterfaceIdValid(drive_interface_id), ResultInvalidDriveInterfaceId());

        u8 fs_type = UINT8_MAX;
        impl::DoWithDriveFATFS(drive_interface_id, [&](FATFS *fs) {
            fs_type = fs->fs_type;
        });
        /* Additional check if the fs type was actually got. */
        R_UNLESS(fs_type != UINT8_MAX, ResultInvalidDriveInterfaceId());

        out_fs_type.SetValue(fs_type);
        FSP_USB_LOG("%s (interface ID %d): set filesystem type to 0x%02X.", __func__, drive_interface_id, fs_type);

        return ResultSuccess();
    }

    Result Service::GetDriveLabel(s32 drive_interface_id, sf::OutBuffer &out_label_str) {
        FSP_USB_LOG("%s (interface ID %d): forcing a mounted drive list update.", __func__, drive_interface_id);
        impl::DoUpdateDrives();
        R_UNLESS(impl::IsDriveInterfaceIdValid(drive_interface_id), ResultInvalidDriveInterfaceId());

        auto drive_mounted_idx = impl::GetDriveMountedIndex(drive_interface_id);
        FSP_USB_LOG("%s (interface ID %d): drive index -> %u.", __func__, drive_interface_id, drive_mounted_idx);

        char mountname[0x10] = {0};
        impl::FormatDriveMountName(mountname, drive_mounted_idx);
        FSP_USB_LOG("%s (interface ID %d): drive mount name -> \"%s\".", __func__, drive_interface_id, mountname);

        auto ffrc = f_getlabel(mountname, reinterpret_cast<char*>(out_label_str.GetPointer()), nullptr);
        FSP_USB_LOG("%s (interface ID %d): f_getlabel returned %u.", __func__, drive_interface_id, ffrc);

        return result::CreateFromFATFSError(ffrc);
    }

    Result Service::SetDriveLabel(s32 drive_interface_id, sf::InBuffer &label_str) {
        FSP_USB_LOG("%s (interface ID %d): forcing a mounted drive list update.", __func__, drive_interface_id);
        impl::DoUpdateDrives();
        R_UNLESS(impl::IsDriveInterfaceIdValid(drive_interface_id), ResultInvalidDriveInterfaceId());

        auto drive_mounted_idx = impl::GetDriveMountedIndex(drive_interface_id);
        FSP_USB_LOG("%s (interface ID %d): drive index -> %u.", __func__, drive_interface_id, drive_mounted_idx);

        char mountname[0x10] = {0};
        impl::FormatDriveMountName(mountname, drive_mounted_idx);
        FSP_USB_LOG("%s (interface ID %d): drive mount name -> \"%s\".", __func__, drive_interface_id, mountname);

        /* Check that no more than 11 characters are copied */
        /* Also allow an empty label_str (label removal) */
        char newname[0x100] = {0};
        const char *input_label = reinterpret_cast<const char*>(label_str.GetPointer());

        if (strlen(input_label) > 0) {
            char label[0x10] = {0};
            snprintf(label, 11, input_label);
            sprintf(newname, "%s%s", mountname, label);
        } else {
            sprintf(newname, mountname);
        }

        FSP_USB_LOG("%s (interface ID %d): drive label -> \"%s\".", __func__, drive_interface_id, newname);

        auto ffrc = f_setlabel(newname);
        FSP_USB_LOG("%s (interface ID %d): f_setlabel returned %u.", __func__, drive_interface_id, ffrc);

        return result::CreateFromFATFSError(ffrc);
    }

    Result Service::OpenDriveFileSystem(s32 drive_interface_id, sf::Out<std::shared_ptr<IFileSystemInterface>> out_fs) {
        FSP_USB_LOG("%s (interface ID %d): forcing a mounted drive list update.", __func__, drive_interface_id);
        impl::DoUpdateDrives();
        R_UNLESS(impl::IsDriveInterfaceIdValid(drive_interface_id), ResultInvalidDriveInterfaceId());

        std::shared_ptr<fs::fsa::IFileSystem> drv_fs = std::make_shared<DriveFileSystem>(drive_interface_id);
        out_fs.SetValue(std::make_shared<IFileSystemInterface>(std::move(drv_fs), false));
        FSP_USB_LOG("%s (interface ID %d): IFileSystem object created.", __func__, drive_interface_id);

        return ResultSuccess();
    }

}