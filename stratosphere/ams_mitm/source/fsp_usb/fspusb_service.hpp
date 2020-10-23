
#pragma once
#include "impl/fspusb_usb_manager.hpp"
#include "fspusb_filesystem.hpp"
#include <stratosphere/fssrv/fssrv_interface_adapters.hpp>

namespace ams::mitm::fspusb {

    namespace impl {

        #define AMS_FSPUSB_INTERFACE_INFO(C, H)                                                                                                              \
            AMS_SF_METHOD_INFO(C, H, 0, Result, ListMountedDrives,       (const sf::OutArray<s32> &out_interface_ids, sf::Out<s32> out_count))               \
            AMS_SF_METHOD_INFO(C, H, 1, Result, GetDriveFileSystemType,  (s32 drive_interface_id, sf::Out<u8> out_fs_type))                                  \
            AMS_SF_METHOD_INFO(C, H, 2, Result, GetDriveLabel,           (s32 drive_interface_id, sf::OutBuffer &out_label_str))                             \
            AMS_SF_METHOD_INFO(C, H, 3, Result, SetDriveLabel,           (s32 drive_interface_id, sf::InBuffer &label_str))                                  \
            AMS_SF_METHOD_INFO(C, H, 4, Result, OpenDriveFileSystem,     (s32 drive_interface_id, sf::Out<std::shared_ptr<fssrv::sf::IFileSystem>> out_fs))

        AMS_SF_DEFINE_INTERFACE(IFspUsbInterface, AMS_FSPUSB_INTERFACE_INFO)

    }

    class FspUsbService final {
        public:
            Result ListMountedDrives(const sf::OutArray<s32> &out_interface_ids, sf::Out<s32> out_count);
            Result GetDriveFileSystemType(s32 drive_interface_id, sf::Out<u8> out_fs_type);
            Result GetDriveLabel(s32 drive_interface_id, sf::OutBuffer &out_label_str);
            Result SetDriveLabel(s32 drive_interface_id, sf::InBuffer &label_str);
            Result OpenDriveFileSystem(s32 drive_interface_id, sf::Out<std::shared_ptr<fssrv::sf::IFileSystem>> out_fs);
    };
    static_assert(impl::IsIFspUsbInterface<FspUsbService>);

}