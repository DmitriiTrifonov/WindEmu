#pragma once
#include <stdint.h>
#include "cfcard.h"

class ARM710;

class Etna {
    uint8_t prom[0x80] = {};
    uint16_t promReadAddress = 0, promReadValue = 0;
    bool promReadActive = false;
    int promAddressBitsReceived = 0;

    uint8_t pendingInterrupts = 0, interruptMask = 0;
    uint8_t wake1 = 0, wake2 = 0;
    uint8_t socketControl = 0xFF;
    uint8_t uartInterruptMask = 0;
    uint8_t sktB0 = 0, sktB1 = 0;
    CFCard card;
    bool cardIrqLine = false;

	ARM710 *owner;

public:
	Etna(ARM710 *owner);
	void serialize(class StateIO &io, uint32_t version);

	bool insertCard(const char *imagePath) { return card.open(imagePath); }
	bool ejectCard() { return card.eject(); }
	bool irqActive();

	// the PC Card windows, 0x40000000 onwards
	uint32_t readCardSpace(uint32_t offset, int bits);
	void writeCardSpace(uint32_t offset, uint32_t value, int bits);

    uint32_t readReg8(uint32_t reg);
    uint32_t readReg32(uint32_t reg);
    void writeReg8(uint32_t reg, uint8_t value);
    void writeReg32(uint32_t reg, uint32_t value);

    // PROM
    void setPromBit0High(); // port B, bit 0
    void setPromBit0Low(); // port B, bit 0
    void setPromBit1High(); // port B, bit 1
};
