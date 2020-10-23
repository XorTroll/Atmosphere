/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#include "../impl/fspusb_usb_manager.hpp"

/* For convenience */
using namespace ams::mitm::fspusb;

/* Reference for needed FATFS impl functions: http://irtos.sourceforge.net/FAT32_ChaN/doc/en/appnote.html#port */

namespace {

	inline u8 GetDriveStatusImpl(u32 mounted_idx) {
		u8 status = STA_NOINIT;

		impl::DoWithDriveMountedIndex(mounted_idx, [&](std::unique_ptr<impl::Drive> &drive_ptr) {
			if (drive_ptr->IsSCSIOk()) {
				status = 0;
			}
		});

		return status;
	}

}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

extern "C" DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	return GetDriveStatusImpl(static_cast<u32>(pdrv));
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

extern "C" DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	return GetDriveStatusImpl(static_cast<u32>(pdrv));
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

extern "C" DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	auto res = RES_PARERR;
	impl::DoWithDriveMountedIndex(static_cast<u32>(pdrv), [&](std::unique_ptr<impl::Drive> &drive_ptr) {
        auto r_res = drive_ptr->ReadSectors(buff, sector, count);
		/* ReadSectors returns 0 on failure and the input sector count on success. */
		if(r_res == count) {
			res = RES_OK;
		}
	});
	return res;
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

extern "C" DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	auto res = RES_PARERR;
	impl::DoWithDriveMountedIndex(static_cast<u32>(pdrv), [&](std::unique_ptr<impl::Drive> &drive_ptr) {
		auto w_res = drive_ptr->WriteSectors(buff, sector, count);
		/* WriteSectors returns 0 on failure and the input sector count on success. */
		if(w_res == count) {
			res = RES_OK;
		}
	});
	return res;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

extern "C" DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
    switch(cmd) {
        case GET_SECTOR_SIZE: {
            impl::DoWithDriveMountedIndex(static_cast<u32>(pdrv), [&](std::unique_ptr<impl::Drive> &drive_ptr) {
				auto block_size = drive_ptr->GetBlockSize();
                *(WORD*)buff = static_cast<WORD>(block_size);
            });
            break;
		}
        default:
            break;
    }
    
    return RES_OK;
}

#if !FF_FS_READONLY && !FF_FS_NORTC /* Get system time */
DWORD get_fattime(void)
{
	/* Use FF_NORTC values by default */
    DWORD output = FAT_TIMESTAMP(FF_NORTC_YEAR, FF_NORTC_MON, FF_NORTC_MDAY, 0, 0, 0);
    
	ams::sm::DoWithSession([&]() {
		if (R_SUCCEEDED(timeInitialize())) {
			u64 timestamp = 0;
			if (R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp))) {
				time_t rawtime = static_cast<time_t>(timestamp);
				struct tm *timeinfo = localtime(&rawtime);
				output = FAT_TIMESTAMP(timeinfo->tm_year, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
			}
			timeExit();
		}
	});
    
    return output;
}
#endif
