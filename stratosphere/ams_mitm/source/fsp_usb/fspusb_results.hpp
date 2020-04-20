
#pragma once
#include <stratosphere.hpp>
#include "fatfs/ff.h"

namespace ams::mitm::fspusb {

    R_DEFINE_NAMESPACE_RESULT_MODULE(2);

    /* Use custom results from 8000 to 8999, since FS uses almost to 7000. */
    /* FATFS errors are converted below, unhandled ones are returned as 8100 + the error. */

    R_DEFINE_ERROR_RESULT(InvalidDriveInterfaceId,     8001);
    R_DEFINE_ERROR_RESULT(DriveUnavailable,            8002);
    R_DEFINE_ERROR_RESULT(DriveInitializationFailure,  8003);


    R_DEFINE_ERROR_RANGE(FATFSError, 8100, 8199);

    #define FSPUSB_MAKE_FATFS_ERROR_RESULT(err) \
        MAKERESULT(R_CURRENT_NAMESPACE_RESULT_MODULE, 8100 + err)

    namespace result {

        NX_CONSTEXPR Result CreateFromFATFSError(FRESULT err) {
            switch(err) {
                case FR_OK:
                    return ResultSuccess();

                case FR_NO_FILE:
                case FR_NO_PATH:
                case FR_INVALID_NAME:
                    return fs::ResultPathNotFound();

                case FR_EXIST:
                    return fs::ResultPathAlreadyExists();

                case FR_WRITE_PROTECTED:
                    return fs::ResultUnsupportedOperation();

                case FR_INVALID_DRIVE:
                    return fs::ResultInvalidMountName();

                case FR_LOCKED:
                    return fs::ResultTargetLocked();

                case FR_TOO_MANY_OPEN_FILES:
                    return fs::ResultOpenCountLimit();

                case FR_INVALID_PARAMETER:
                    return fs::ResultInvalidArgument();

                /* TODO: support more FATFS errors */

                default:
                    return FSPUSB_MAKE_FATFS_ERROR_RESULT(err);
            }
        }

    }

}