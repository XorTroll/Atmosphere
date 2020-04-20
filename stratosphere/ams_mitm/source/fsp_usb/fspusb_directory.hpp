
#pragma once
#include "impl/fspusb_usb_manager.hpp"

namespace ams::mitm::fspusb {

    class DriveDirectory : public fs::fsa::IDirectory {
        private:
            s32 usb_iface_id;
            DIR directory;

            bool IsDriveInterfaceIdValid() {
                return impl::IsDriveInterfaceIdValid(this->usb_iface_id);
            }

        public:
            DriveDirectory(s32 iface_id, DIR dir) : usb_iface_id(iface_id), directory(dir) {}

            ~DriveDirectory() {
                f_closedir(&this->directory);
            }

            virtual Result ReadImpl(s64 *out_count, fs::DirectoryEntry *out_entries, s64 max_entries) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());

                auto ffrc = FR_OK;
                fs::DirectoryEntry entry = {};
                FILINFO info = {};
                s64 count = 0;
                while(true) {
                    if (count >= max_entries) {
                        break;
                    }
                    ffrc = f_readdir(&this->directory, &info);
                    if ((ffrc != FR_OK) || (info.fname[0] == '\0')) {
                        break;
                    }
                    std::memset(&entry, 0, sizeof(fs::DirectoryEntry));
                    strcpy(entry.name, info.fname);

                    /* Fill in the DirectoryEntry struct, then copy back to the buffer */
                    if (info.fattrib & AM_DIR) {
                        entry.type = FsDirEntryType_Dir;
                    }
                    else {
                        entry.type = FsDirEntryType_File;
                        entry.file_size = info.fsize;
                    }
                    std::memcpy(&out_entries[count], &entry, sizeof(entry));
                    count++;
                }
                *out_count = count;

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result GetEntryCountImpl(s64 *out) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());

                s64 count = 0;
                auto ffrc = FR_OK;
                FILINFO info = {};
                while(true) {
                    ffrc = f_readdir(&this->directory, &info);
                    if ((ffrc != FR_OK) || (info.fname[0] == '\0')) {
                        break;
                    }
                    count++;
                }
                *out = count;

                return result::CreateFromFATFSError(ffrc);
            }

            virtual sf::cmif::DomainObjectId GetDomainObjectId() const override {
                return sf::cmif::InvalidDomainObjectId;
            }

    };

}