#pragma once
#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <string>
#include "fatfolder.h"

// A CompactFlash card in PC Card ATA mode, backed by a raw disk image on the host,
// or by a folder that is turned into one
class CFCard {
	FILE *image = nullptr;
	std::unique_ptr<FatFolder> folder;
	std::string folderImagePath;
	uint32_t totalSectors = 0;
	uint16_t cylinders = 0, heads = 0, sectorsPerTrack = 0;

	// card configuration registers, in attribute memory
	uint8_t configOption = 0, configStatus = 0, pinReplacement = 0, socketCopy = 0;

	// ATA task file
	uint8_t error = 0, feature = 0, sectorCount = 0, sectorNumber = 0;
	uint8_t cylinderLow = 0, cylinderHigh = 0, driveHead = 0, status = 0, deviceControl = 0;

	// data being moved by the current command
	enum Transfer : uint8_t { TransferNone, TransferRead, TransferWrite };
	Transfer transfer = TransferNone;
	uint8_t buffer[512] = {};
	uint16_t bufferPos = 0;
	uint32_t transferSector = 0;
	uint16_t sectorsLeft = 0;
	bool interruptPending = false;

	uint32_t currentSector() const;
	void setCurrentSector(uint32_t lba);
	void executeCommand(uint8_t command);
	void abortCommand();
	void finishCommand();
	bool loadSector();
	void storeSector();
	void fillIdentify();
	void raiseInterrupt();

	uint8_t readTaskFile(uint32_t reg);
	void writeTaskFile(uint32_t reg, uint8_t value);
	uint8_t readDataByte();
	void writeDataByte(uint8_t value);
	int taskFileRegister(int space, uint32_t addr) const;

public:
	~CFCard();
	// takes a disk image, or a folder to present as a FAT16 volume
	bool open(const char *path);
	// takes the card out; changes to a folder's volume are copied back to the folder
	bool eject();
	static void geometryFor(uint32_t sectors, uint16_t &cylinders, uint16_t &heads, uint16_t &sectorsPerTrack);
	void reset();
	bool isInserted() const { return image != nullptr; }
	// the card's interrupt request line
	bool interruptRequested() const { return interruptPending && !(deviceControl & 2); }
	void serialize(class StateIO &io);

	// space: 0 = attribute memory, 1 = common memory, 2 = 8-bit I/O, 3 = 16-bit I/O
	uint8_t read8(int space, uint32_t addr);
	uint16_t read16(int space, uint32_t addr);
	void write8(int space, uint32_t addr, uint8_t value);
	void write16(int space, uint32_t addr, uint16_t value);
};
