
#pragma once
#include "fspusb_file.hpp" 
#include "fspusb_directory.hpp"

namespace ams::mitm::fspusb {

    class DriveFileSystem : public fs::fsa::IFileSystem {

        private:
            s32 usb_iface_id;
            char mount_name[0x10];

            inline void DoWithDrive(std::function<void(std::unique_ptr<impl::Drive>&)> fn) {
                impl::DoWithDrive(this->usb_iface_id, fn);
            }

            inline void DoWithDriveFATFS(std::function<void(FATFS*)> fn) {
                this->DoWithDrive([&](std::unique_ptr<impl::Drive> &drive_ptr) {
                    drive_ptr->DoWithFATFS(fn);
                });
            }

            inline bool IsDriveInterfaceIdValid() {
                return impl::IsDriveInterfaceIdValid(this->usb_iface_id);
            }

            inline void NormalizePath(char *out_path, const char *input_path) {
                std::memset(out_path, 0, strlen(out_path));
                sprintf(out_path, "%s%s", this->mount_name, input_path);
                const size_t out_path_len = strlen(out_path);
                if (out_path[out_path_len - 1] == '/') {
                    out_path[out_path_len - 1] = '\0';
                }
            }

            FRESULT DeleteDirectoryRecursivelyBase(const char *path, bool delete_parent_dir) {
                DIR dir = {};
                FILINFO info = {};
                char ffpath[FS_MAX_PATH] = {0};
                auto ffrc = f_opendir(&dir, path);
                
                if (ffrc == FR_OK) {
                    while(true) {
                        ffrc = f_readdir(&dir, &info);
                        
                        if (ffrc != FR_OK || info.fname[0] == '\0') {
                            break;
                        }
                        
                        sprintf(ffpath, "%s/%s", path, info.fname);
                        
                        if (info.fattrib & AM_DIR) {
                            ffrc = DeleteDirectoryRecursivelyBase(ffpath, true);
                        } else {
                            ffrc = f_unlink(ffpath);
                        }
                        
                        if (ffrc != FR_OK) {
                            break;
                        }
                    }
                    
                    f_closedir(&dir);
                    
                    if ((ffrc == FR_OK) && delete_parent_dir) {
                        ffrc = f_rmdir(path);
                    }
                }
                
                return ffrc;
            }

            FRESULT GetSpaceImpl(s64 *out, bool get_total_space) {
                u32 block_size = 0;
                auto ffrc = FR_OK;
                FATFS *fs = nullptr;
                DWORD clstrs = 0;
                
                this->DoWithDrive([&](std::unique_ptr<impl::Drive> &drive_ptr) {
                    block_size = drive_ptr->GetBlockSize();
                });
                
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_getfree(this->mount_name, &clstrs, &fs);
                });
                
                if (ffrc == FR_OK && fs) {
                    if (get_total_space) {
                        /* Calculate total space. */
                        *out = ((s64)((fs->n_fatent - 2) * fs->csize) * (s64)block_size);
                    } else {
                        /* Calculate free space. */
                        *out = ((s64)(clstrs * fs->csize) * (s64)block_size);
                    }
                }
                
                return ffrc;
            }

        public:
            DriveFileSystem(s32 drive_interface_id) : usb_iface_id(drive_interface_id) {
                auto drive_mounted_idx = impl::GetDriveMountedIndex(drive_interface_id);
                impl::FormatDriveMountName(this->mount_name, drive_mounted_idx);
            }

            virtual Result CreateFileImpl(const char *path, s64 size, int flags) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    FIL fp = {};
                    ffrc = f_open(&fp, ffpath, FA_CREATE_NEW | FA_WRITE);
                    if (ffrc == FR_OK) {
                        f_lseek(&fp, (u64)size);
                        f_close(&fp);
                    }
                });

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result DeleteFileImpl(const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_unlink(ffpath);
                });

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result CreateDirectoryImpl(const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_mkdir(ffpath);
                });

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result DeleteDirectoryImpl(const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_rmdir(ffpath);
                });

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result DeleteDirectoryRecursivelyImpl(const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());

                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                /* Remove directory contents and the directory itself */
                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = DeleteDirectoryRecursivelyBase(ffpath, true);
                });

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result RenameFileImpl(const char *old_path, const char *new_path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                char ffoldpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffoldpath, old_path);
                char ffnewpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffnewpath, new_path);

                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_rename(ffoldpath, ffnewpath);
                });

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result RenameDirectoryImpl(const char *old_path, const char *new_path) override final {
                /* This is the same as RenameFileImpl */
                return this->RenameFileImpl(old_path, new_path);
            }

            virtual Result GetEntryTypeImpl(fs::DirectoryEntryType *out, const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                FILINFO finfo = {};
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_stat(ffpath, &finfo);
                });

                R_TRY(result::CreateFromFATFSError(ffrc));

                *out = ((finfo.fattrib & AM_DIR) ? fs::DirectoryEntryType_Directory : fs::DirectoryEntryType_File);

                return ResultSuccess();
            }

            virtual Result OpenFileImpl(std::unique_ptr<fs::fsa::IFile> *out_file, const char *path, fs::OpenMode mode) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                BYTE openmode = FA_OPEN_EXISTING;

                if (mode & fs::OpenMode_Read) {
                    openmode |= FA_READ;
                }
                if (mode & fs::OpenMode_Write) {
                    openmode |= FA_WRITE;
                }
                if (mode & fs::OpenMode_AllowAppend) {
                    openmode |= FA_OPEN_APPEND;
                }

                FIL fil = {};
                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_open(&fil, ffpath, openmode);
                });

                if (ffrc == FR_OK) {
                    *out_file = std::make_unique<DriveFile>(this->usb_iface_id, fil);
                }

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result OpenDirectoryImpl(std::unique_ptr<fs::fsa::IDirectory> *out_dir, const char *path, fs::OpenDirectoryMode mode) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                DIR dir = {};
                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_opendir(&dir, ffpath);
                });

                if (ffrc == FR_OK) {
                    *out_dir = std::make_unique<DriveDirectory>(this->usb_iface_id, dir);
                }

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result CommitImpl() override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                return ResultSuccess();
            }

            virtual Result GetFreeSpaceSizeImpl(s64 *out, const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());

                R_TRY(result::CreateFromFATFSError(GetSpaceImpl(out, false)));

                return ResultSuccess();
            }

            virtual Result GetTotalSpaceSizeImpl(s64 *out, const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());

                R_TRY(result::CreateFromFATFSError(GetSpaceImpl(out, true)));

                return ResultSuccess();
            }

            virtual Result CleanDirectoryRecursivelyImpl(const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());

                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                /* Remove just the directory contents */
                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = DeleteDirectoryRecursivelyBase(ffpath, false);
                });

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result GetFileTimeStampRawImpl(fs::FileTimeStampRaw *out, const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                
                FILINFO finfo = {};
                
                char ffpath[FS_MAX_PATH] = {0};
                this->NormalizePath(ffpath, path);

                auto ffrc = FR_OK;
                this->DoWithDriveFATFS([&](FATFS *fatfs) {
                    ffrc = f_stat(ffpath, &finfo);
                });

                if (ffrc == FR_OK) {
                    std::memset(out, 0, sizeof(fs::FileTimeStampRaw));
                    
                    struct tm timeinfo;
                    std::memset(&timeinfo, 0, sizeof(struct tm));
                    
                    timeinfo.tm_year = (int)(((finfo.fdate >> 9) & 0x7F) + 1980);
                    timeinfo.tm_mon = (int)(((finfo.fdate >> 5) & 0x0F) - 1);
                    timeinfo.tm_mday = (int)(finfo.fdate & 0x1F);
                    timeinfo.tm_hour = (int)((finfo.ftime >> 11) & 0x1F);
                    timeinfo.tm_min = (int)((finfo.ftime >> 5) & 0x3F);
                    timeinfo.tm_sec = (int)((finfo.ftime & 0x1F) * 2);
                    
                    time_t rawtime = mktime(&timeinfo);
                    
                    /* FAT filesystems don't keep track of creation nor last access dates */
                    out->modified = (u64)rawtime;
                    out->is_valid = 0x1;
                }

                return result::CreateFromFATFSError(ffrc);
            }

            virtual Result QueryEntryImpl(char *dst, size_t dst_size, const char *src, size_t src_size, fs::fsa::QueryId query, const char *path) override final {
                R_UNLESS(this->IsDriveInterfaceIdValid(), ResultDriveUnavailable());
                /* TODO */
                return fs::ResultNotImplemented();
            }
    };

}