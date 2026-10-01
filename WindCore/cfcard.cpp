#include "cfcard.h"
#include "state.h"
#include <string.h>
#include <filesystem>
#include <time.h>

// Card Information Structure, read from the even bytes of attribute memory
static const uint8_t Cis[] = {
	0x01, 0x03, 0xD9, 0x01, 0xFF,             // CISTPL_DEVICE: function specific, 250ns, 2KB
	0x1C, 0x04, 0x03, 0xD9, 0x01, 0xFF,       // CISTPL_DEVICE_OC: the same at 3.3V
	0x18, 0x02, 0xDF, 0x01,                   // CISTPL_JEDEC_C
	0x20, 0x04, 0x57, 0x45, 0x01, 0x00,       // CISTPL_MANFID
	0x15, 0x13, 0x04, 0x01,                   // CISTPL_VERS_1: PCMCIA 2.1
	'W', 'i', 'n', 'd', 'E', 'm', 'u', 0,
	'C', 'F', ' ', 'C', 'a', 'r', 'd', 0,
	0xFF,
	0x21, 0x02, 0x04, 0x01,                   // CISTPL_FUNCID: fixed disk, initialise at POST
	0x22, 0x02, 0x01, 0x01,                   // CISTPL_FUNCE: PC Card ATA interface
	0x22, 0x03, 0x02, 0x0C, 0x0F,             // CISTPL_FUNCE: ATA features
	0x1A, 0x05, 0x01, 0x01, 0x00, 0x02, 0x0F, // CISTPL_CONFIG: registers at 0x200, last entry 1
	// CISTPL_CFTABLE_ENTRY 0: memory mapped, 5V, 2KB of common memory
	0x1B, 0x08, 0xC0, 0xC0, 0xA1, 0x01, 0x55, 0x08, 0x00, 0x20,
	// CISTPL_CFTABLE_ENTRY 1: contiguous I/O, 16 registers, any IRQ
	0x1B, 0x0A, 0xC1, 0x41, 0x99, 0x01, 0x55, 0x64, 0xF0, 0xFF, 0xFF, 0x20,
	0x14, 0x00,                               // CISTPL_NO_LINK
	0xFF                                      // CISTPL_END
};

enum {
	RegData = 0, RegError = 1, RegSectorCount = 2, RegSectorNumber = 3,
	RegCylinderLow = 4, RegCylinderHigh = 5, RegDriveHead = 6, RegStatus = 7,
	RegDataEven = 8, RegDataOdd = 9, RegErrorDup = 0xD, RegAltStatus = 0xE, RegDriveAddress = 0xF
};

enum {
	StatusErr = 0x01, StatusDrq = 0x08, StatusDsc = 0x10, StatusDrdy = 0x40, StatusBsy = 0x80
};

enum { ErrorAbort = 0x04, ErrorIdNotFound = 0x10 };

CFCard::~CFCard() {
	if (image)
		fclose(image);
	if (!folderImagePath.empty())
		remove(folderImagePath.c_str());
}

void CFCard::geometryFor(uint32_t sectors, uint16_t &cylinders, uint16_t &heads, uint16_t &sectorsPerTrack) {
	// like a real card's, for drivers that don't use LBA
	sectorsPerTrack = 32;
	heads = 2;
	while (sectors / (heads * sectorsPerTrack) > 1024 && heads < 16)
		heads *= 2;
	if (sectors / (heads * sectorsPerTrack) > 1024)
		sectorsPerTrack = 63;
	uint32_t cyl = sectors / (heads * sectorsPerTrack);
	cylinders = cyl > 0xFFFF ? 0xFFFF : cyl;
}

bool CFCard::open(const char *path) {
	eject();

	std::error_code ec;
	if (std::filesystem::is_directory(path, ec)) {
		folder.reset(new FatFolder);
		folderImagePath = (std::filesystem::temp_directory_path(ec) /
			("windemu-cf-" + std::to_string((uintptr_t)this ^ (uintptr_t)time(nullptr)) + ".img")).string();
		if (!folder->build(path, folderImagePath)) {
			remove(folderImagePath.c_str());
			folderImagePath.clear();
			folder.reset();
			return false;
		}
		path = folderImagePath.c_str();
	}

	FILE *f = fopen(path, "r+b");
	long size = 0;
	if (f) {
		fseek(f, 0, SEEK_END);
		size = ftell(f);
	}
	if (size < 512 * 64) {
		if (f)
			fclose(f);
		if (!folderImagePath.empty()) {
			remove(folderImagePath.c_str());
			folderImagePath.clear();
			folder.reset();
		}
		return false;
	}
	image = f;
	totalSectors = (uint32_t)(size / 512);
	geometryFor(totalSectors, cylinders, heads, sectorsPerTrack);

	reset();
	return true;
}

bool CFCard::eject() {
	if (!image)
		return true;
	bool ok = true;
	if (folder)
		ok = folder->syncBack(image);
	fclose(image);
	image = nullptr;
	if (!folderImagePath.empty()) {
		if (ok)
			remove(folderImagePath.c_str());
		else
			fprintf(stderr, "The CF card's volume is kept in %s\n", folderImagePath.c_str());
		folderImagePath.clear();
	}
	folder.reset();
	return ok;
}

void CFCard::reset() {
	configOption = configStatus = pinReplacement = socketCopy = 0;
	error = 1; // diagnostics passed
	feature = 0;
	sectorCount = sectorNumber = 1;
	cylinderLow = cylinderHigh = driveHead = 0;
	status = StatusDrdy | StatusDsc;
	deviceControl = 0;
	transfer = TransferNone;
	bufferPos = 0;
	sectorsLeft = 0;
	interruptPending = false;
}

void CFCard::serialize(StateIO &io) {
	io.pod(configOption);
	io.pod(configStatus);
	io.pod(pinReplacement);
	io.pod(socketCopy);
	io.pod(error);
	io.pod(feature);
	io.pod(sectorCount);
	io.pod(sectorNumber);
	io.pod(cylinderLow);
	io.pod(cylinderHigh);
	io.pod(driveHead);
	io.pod(status);
	io.pod(deviceControl);
	io.pod(transfer);
	io.pod(buffer);
	io.pod(bufferPos);
	io.pod(transferSector);
	io.pod(sectorsLeft);
	io.pod(interruptPending);
}


uint32_t CFCard::currentSector() const {
	if (driveHead & 0x40) // LBA
		return ((driveHead & 0xF) << 24) | (cylinderHigh << 16) | (cylinderLow << 8) | sectorNumber;
	uint32_t cylinder = (cylinderHigh << 8) | cylinderLow;
	return (cylinder * heads + (driveHead & 0xF)) * sectorsPerTrack + sectorNumber - 1;
}

void CFCard::setCurrentSector(uint32_t lba) {
	if (driveHead & 0x40) {
		sectorNumber = lba & 0xFF;
		cylinderLow = (lba >> 8) & 0xFF;
		cylinderHigh = (lba >> 16) & 0xFF;
		driveHead = (driveHead & 0xF0) | ((lba >> 24) & 0xF);
	} else {
		uint32_t cylinder = lba / (heads * sectorsPerTrack);
		uint32_t rest = lba % (heads * sectorsPerTrack);
		sectorNumber = rest % sectorsPerTrack + 1;
		cylinderLow = cylinder & 0xFF;
		cylinderHigh = (cylinder >> 8) & 0xFF;
		driveHead = (driveHead & 0xF0) | (rest / sectorsPerTrack);
	}
}

void CFCard::raiseInterrupt() {
	interruptPending = true;
}

void CFCard::abortCommand() {
	transfer = TransferNone;
	error = ErrorAbort;
	status = StatusDrdy | StatusDsc | StatusErr;
	raiseInterrupt();
}

void CFCard::finishCommand() {
	transfer = TransferNone;
	error = 0;
	status = StatusDrdy | StatusDsc;
	raiseInterrupt();
}

bool CFCard::loadSector() {
	if (transferSector >= totalSectors || fseek(image, (long)transferSector * 512, SEEK_SET) != 0 ||
		fread(buffer, 1, 512, image) != 512) {
		error = ErrorIdNotFound;
		status = StatusDrdy | StatusDsc | StatusErr;
		transfer = TransferNone;
		raiseInterrupt();
		return false;
	}
	return true;
}

void CFCard::storeSector() {
	if (transferSector >= totalSectors || fseek(image, (long)transferSector * 512, SEEK_SET) != 0 ||
		fwrite(buffer, 1, 512, image) != 512) {
		error = ErrorIdNotFound;
		status = StatusDrdy | StatusDsc | StatusErr;
		transfer = TransferNone;
		raiseInterrupt();
		return;
	}
	fflush(image);
}

static void putString(uint16_t *words, int wordCount, const char *str) {
	// ATA strings hold two characters per word, first in the high byte
	char padded[64];
	memset(padded, ' ', sizeof(padded));
	memcpy(padded, str, strlen(str));
	for (int i = 0; i < wordCount; i++)
		words[i] = ((uint8_t)padded[i * 2] << 8) | (uint8_t)padded[i * 2 + 1];
}

void CFCard::fillIdentify() {
	uint16_t id[256] = {};
	id[0] = 0x848A; // CompactFlash, removable
	id[1] = cylinders;
	id[3] = heads;
	id[4] = 512 * sectorsPerTrack;
	id[5] = 512;
	id[6] = sectorsPerTrack;
	id[7] = totalSectors >> 16;
	id[8] = totalSectors & 0xFFFF;
	putString(&id[10], 10, "WINDEMU0001");
	id[20] = 2; // dual ported buffer
	id[21] = 1; // buffer size in sectors
	putString(&id[23], 4, "1.0");
	putString(&id[27], 20, "WindEmu CF Card");
	id[47] = 1; // multiple sector transfers of up to 1 sector
	id[49] = 0x0200; // LBA
	id[51] = 0x0200; // PIO mode 2 timing
	id[53] = 1; // words 54-58 are valid
	id[54] = cylinders;
	id[55] = heads;
	id[56] = sectorsPerTrack;
	uint32_t chsCapacity = (uint32_t)cylinders * heads * sectorsPerTrack;
	id[57] = chsCapacity & 0xFFFF;
	id[58] = chsCapacity >> 16;
	id[59] = 0x0101;
	id[60] = totalSectors & 0xFFFF;
	id[61] = totalSectors >> 16;
	for (int i = 0; i < 256; i++) {
		buffer[i * 2] = id[i] & 0xFF;
		buffer[i * 2 + 1] = id[i] >> 8;
	}
}

void CFCard::executeCommand(uint8_t command) {
	interruptPending = false;
	switch (command) {
	case 0xEC: // IDENTIFY DEVICE
		fillIdentify();
		transfer = TransferRead;
		sectorsLeft = 1;
		bufferPos = 0;
		error = 0;
		status = StatusDrdy | StatusDsc | StatusDrq;
		raiseInterrupt();
		break;
	case 0x20: case 0x21: // READ SECTORS
	case 0xC4: // READ MULTIPLE
		transferSector = currentSector();
		sectorsLeft = sectorCount ? sectorCount : 256;
		if (!loadSector())
			break;
		transfer = TransferRead;
		bufferPos = 0;
		error = 0;
		status = StatusDrdy | StatusDsc | StatusDrq;
		raiseInterrupt();
		break;
	case 0x30: case 0x31: // WRITE SECTORS
	case 0xC5: // WRITE MULTIPLE
	case 0x38: // CFA WRITE WITHOUT ERASE
		transferSector = currentSector();
		sectorsLeft = sectorCount ? sectorCount : 256;
		transfer = TransferWrite;
		bufferPos = 0;
		error = 0;
		status = StatusDrdy | StatusDsc | StatusDrq; // no interrupt before the first sector
		break;
	case 0x40: case 0x41: // READ VERIFY SECTORS
		if (currentSector() + (sectorCount ? sectorCount : 256) > totalSectors)
			abortCommand();
		else
			finishCommand();
		break;
	case 0x91: // INITIALIZE DEVICE PARAMETERS
		if (sectorCount == 0) {
			abortCommand();
		} else {
			sectorsPerTrack = sectorCount;
			heads = (driveHead & 0xF) + 1;
			finishCommand();
		}
		break;
	case 0x90: // EXECUTE DEVICE DIAGNOSTIC
		finishCommand();
		error = 1;
		break;
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: // RECALIBRATE
	case 0x70: // SEEK
	case 0xC6: // SET MULTIPLE MODE
	case 0xEF: // SET FEATURES
	case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5: case 0xE6: // power management
	case 0x94: case 0x95: case 0x96: case 0x97: case 0x98: case 0x99: // old power management
	case 0xC0: // CFA ERASE SECTORS
	case 0x03: // CFA REQUEST EXTENDED ERROR
		finishCommand();
		if (command == 0xE5 || command == 0x98)
			sectorCount = 0xFF; // active or idle
		break;
	default:
		abortCommand();
		break;
	}
}

uint8_t CFCard::readDataByte() {
	if (transfer != TransferRead)
		return 0xFF;
	uint8_t value = buffer[bufferPos++];
	if (bufferPos == 512) {
		bufferPos = 0;
		if (--sectorsLeft == 0) {
			transfer = TransferNone;
			status = StatusDrdy | StatusDsc;
		} else {
			transferSector++;
			setCurrentSector(transferSector);
			if (loadSector())
				raiseInterrupt();
		}
	}
	return value;
}

void CFCard::writeDataByte(uint8_t value) {
	if (transfer != TransferWrite)
		return;
	buffer[bufferPos++] = value;
	if (bufferPos == 512) {
		bufferPos = 0;
		storeSector();
		if (transfer != TransferWrite)
			return; // failed
		transferSector++;
		setCurrentSector(transferSector);
		if (--sectorsLeft == 0) {
			sectorCount = 0;
			finishCommand();
		} else {
			raiseInterrupt();
		}
	}
}

uint8_t CFCard::readTaskFile(uint32_t reg) {
	switch (reg) {
	case RegData:
	case RegDataEven:
	case RegDataOdd:
		return readDataByte();
	case RegError:
	case RegErrorDup:
		return error;
	case RegSectorCount: return sectorCount;
	case RegSectorNumber: return sectorNumber;
	case RegCylinderLow: return cylinderLow;
	case RegCylinderHigh: return cylinderHigh;
	case RegDriveHead: return driveHead | 0xA0;
	case RegStatus:
		interruptPending = false;
		return status;
	case RegAltStatus: return status;
	case RegDriveAddress: return 0xFF;
	}
	return 0xFF;
}

void CFCard::writeTaskFile(uint32_t reg, uint8_t value) {
	switch (reg) {
	case RegData:
	case RegDataEven:
	case RegDataOdd:
		writeDataByte(value);
		break;
	case RegError:
	case RegErrorDup:
		feature = value;
		break;
	case RegSectorCount: sectorCount = value; break;
	case RegSectorNumber: sectorNumber = value; break;
	case RegCylinderLow: cylinderLow = value; break;
	case RegCylinderHigh: cylinderHigh = value; break;
	case RegDriveHead: driveHead = value; break;
	case RegStatus:
		if (!(driveHead & 0x10)) // only respond as the master
			executeCommand(value);
		break;
	case RegAltStatus:
		if ((value & 4) && !(deviceControl & 4)) {
			// software reset
			uint8_t keepConfig = configOption;
			reset();
			configOption = keepConfig;
		}
		deviceControl = value;
		break;
	}
}

// Which task file register an address hits, or -1 for none
int CFCard::taskFileRegister(int space, uint32_t addr) const {
	if (space == 1) {
		// memory mapped: registers at 0-0xF, with 0x400-0x7FF also reaching the data register
		addr &= 0x7FF;
		return (addr >= 0x400) ? RegData : (int)(addr & 0xF);
	}
	switch (configOption & 0x3F) {
	case 2: // primary I/O
	case 3: // secondary I/O
		addr &= 0x3FF;
		if ((addr & 0x3F8) == 0x170 || (addr & 0x3F8) == 0x1F0) return addr & 7;
		if ((addr & 0x3FE) == 0x376 || (addr & 0x3FE) == 0x3F6) return 0xE | (addr & 1);
		return -1;
	default: // contiguous I/O
		return addr & 0xF;
	}
}

uint8_t CFCard::read8(int space, uint32_t addr) {
	if (!image)
		return 0xFF;
	if (space == 0) {
		if (addr & 1)
			return 0xFF; // attribute memory is only on even bytes
		uint32_t index = addr >> 1;
		if (index < sizeof(Cis))
			return Cis[index];
		switch (addr) {
		case 0x200: return configOption;
		case 0x202: return configStatus;
		case 0x204: return pinReplacement;
		case 0x206: return socketCopy;
		}
		return 0xFF;
	}
	int reg = taskFileRegister(space, addr);
	return reg < 0 ? 0xFF : readTaskFile(reg);
}

void CFCard::write8(int space, uint32_t addr, uint8_t value) {
	if (!image)
		return;
	if (space == 0) {
		switch (addr) {
		case 0x200:
			if (value & 0x80) {
				reset(); // SRESET
				configOption = value & 0x7F;
			} else {
				configOption = value;
			}
			break;
		case 0x202: configStatus = value; break;
		case 0x204: pinReplacement = value; break;
		case 0x206: socketCopy = value; break;
		}
		return;
	}
	int reg = taskFileRegister(space, addr);
	if (reg >= 0)
		writeTaskFile(reg, value);
}

// A 16-bit cycle on the data register moves two bytes of data; elsewhere it
// reaches the register at the address and the one after it.
uint16_t CFCard::read16(int space, uint32_t addr) {
	if (space != 0 && taskFileRegister(space, addr) == RegData) {
		uint16_t lo = read8(space, addr);
		return lo | (read8(space, addr) << 8);
	}
	return read8(space, addr) | (read8(space, addr + 1) << 8);
}

void CFCard::write16(int space, uint32_t addr, uint16_t value) {
	if (space != 0 && taskFileRegister(space, addr) == RegData) {
		write8(space, addr, value & 0xFF);
		write8(space, addr, value >> 8);
	} else {
		write8(space, addr, value & 0xFF);
		write8(space, addr + 1, value >> 8);
	}
}
