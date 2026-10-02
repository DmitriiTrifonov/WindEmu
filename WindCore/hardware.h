#pragma once
#include "arm710.h"
#include <stdio.h>
#include <deque>
#include <functional>

struct Timer {
	ARM710 *cpu;

	enum {
		MODE_512KHZ = 1<<3,
		PERIODIC = 1<<6,
		ENABLED = 1<<7
	};
    int64_t nextTickAt;
	uint8_t config;
	uint32_t interval;
	int32_t value;
	int clockSpeed;

	int tickInterval() const {
		return (config & MODE_512KHZ) ? (clockSpeed / 512000) : (clockSpeed / 2000);
	}
	void load(uint32_t lval) {
		interval = lval;
		value = lval;
	}
	void setConfig(uint8_t cval) {
		nextTickAt -= tickInterval();
		config = cval;
		nextTickAt += tickInterval();
	}
    bool tick(int64_t cycles) {
		if (cycles >= nextTickAt) {
			nextTickAt += tickInterval();

			if (config & ENABLED) {
				--value;
				if (value == 0) {
					if (config & PERIODIC)
						value = interval;
					return true;
				}
			}
		}
		return false;
	}
	void dump() {
		printf("enabled=%s periodic=%s interval=%d value=%d\n",
			(config & ENABLED) ? "true" : "false",
			(config & PERIODIC) ? "true" : "false",
			interval, value
		);
	}
};

enum UartRegs {
	UART0DATA = 0x600,
	UART0FCR = 0x604,
	UART0LCR = 0x608,
	UART0CON = 0x60C,
	UART0FLG = 0x610,
	UART0INT = 0x614,
	UART0INTM = 0x618,
	UART0INTR = 0x61C,
	UART0TEST1 = 0x620,
	UART0TEST2 = 0x624,
	UART0TEST3 = 0x628,
	UART1DATA = 0x700,
	UART1FCR = 0x704,
	UART1LCR = 0x708,
	UART1CON = 0x70C,
	UART1FLG = 0x710,
	UART1INT = 0x714,
	UART1INTM = 0x718,
	UART1INTR = 0x71C,
	UART1TEST1 = 0x720,
	UART1TEST2 = 0x724,
	UART1TEST3 = 0x728,
};

struct UART {
	ARM710 *cpu;

	enum {
		IntRx = 1,
		IntTx = 2,
		IntModemStatus = 4,
		PortCtrlEnable = 1,
		PortCtrlSirEnable = 2,
		PortCtrlIrdaTx = 4,
		FrameCtrlBreak = 1,
		FrameCtrlParityEnable = 2,
		FrameCtrlEvenParity = 4,
		FrameCtrlExtraStopBit = 8,
		FrameCtrlUFifoEn = 0x10,
		FrameCtrlWrdLenMask = 0x60,
		FrameCtrlWlen5 = 0,
		FrameCtrlWlen6 = 0x20,
		FrameCtrlWlen7 = 0x40,
		FrameCtrlWlen8 = 0x60,
		RecvFrameError = 0x100,
		RecvParityError = 0x200,
		RecvOverrunError = 0x400,
		FlagClearToSend = 1,
		FlagDataSetReady = 2,
		FlagDataCarrierDetect = 4,
		FlagBusy = 8,
		FlagReceiveFifoEmpty = 0x10,
		FlagTransmitFifoFull = 0x20,
		FifoSize = 16
	};
	uint8_t portControl = 0;
	uint8_t frameControl = 0;
	uint8_t interrupts = 0, interruptMask = 0;
	uint32_t lineControl = 0;

	// What the Psion sends goes straight out through transmit (so the
	// transmitter is never busy); what arrives waits in rxQueue until the
	// receive FIFO has room for it.
	std::function<void(uint8_t)> transmit;
	std::deque<uint8_t> rxFifo, rxQueue;
	// whether something is connected, raising CTS, DSR and DCD
	bool connected = false;
	// the modem lines changed, and EPOC hasn't yet acknowledged it
	bool modemStatusChanged = false;
	void setConnected(bool c) {
		if (c != connected)
			modemStatusChanged = true;
		connected = c;
	}

	int fifoDepth() const { return (frameControl & FrameCtrlUFifoEn) ? FifoSize : 1; }

	// fills the receive FIFO from the queue; called often
	void poll() {
		while (!rxQueue.empty() && (int)rxFifo.size() < fifoDepth()) {
			rxFifo.push_back(rxQueue.front());
			rxQueue.pop_front();
		}
	}

	uint8_t rawInterrupts() const {
		uint8_t raw = IntTx; // the transmit FIFO always has room
		if (!rxFifo.empty())
			raw |= IntRx;
		if (modemStatusChanged)
			raw |= IntModemStatus;
		return raw;
	}
	bool interruptPending() const {
		return (portControl & PortCtrlEnable) && (rawInterrupts() & interruptMask);
	}

	uint8_t flags() const {
		uint8_t f = 0;
		if (rxFifo.empty())
			f |= FlagReceiveFifoEmpty;
		// EPOC reads CTS and DCD as active low, and DSR as active high
		if (connected)
			f |= FlagDataSetReady;
		else
			f |= FlagClearToSend | FlagDataCarrierDetect;
		return f;
	}

	uint32_t readData() {
		if (rxFifo.empty())
			return 0;
		uint8_t byte = rxFifo.front();
		rxFifo.pop_front();
		poll();
		return byte;
	}

	// UART0DATA = 0x600, byte write, long read
	// UART0FCR = 0x604, long
	// UART0LCR = 0x608, long
	// UART0CON = 0x60C, byte
	// UART0FLG = 0x610, byte
	// UART0INT = 0x614, long write, byte read
	// UART0INTM = 0x618, byte
	// UART0INTR = 0x61C, byte
	uint32_t readReg(uint32_t reg) {
		switch (reg) {
		case UART0DATA & 0xFF: return readData();
		case UART0FCR & 0xFF:  return frameControl;
		case UART0LCR & 0xFF:  return lineControl;
		case UART0CON & 0xFF:  return portControl;
		case UART0FLG & 0xFF:  return flags();
		// EPOC treats INTR as the masked status, and INT as the raw one
		case UART0INT & 0xFF:  return rawInterrupts();
		case UART0INTM & 0xFF: return interruptMask;
		case UART0INTR & 0xFF: return rawInterrupts() & interruptMask;
		}
		return 0;
	}
	void writeReg(uint32_t reg, uint32_t value) {
		switch (reg) {
		case UART0DATA & 0xFF:
			if ((portControl & PortCtrlEnable) && transmit)
				transmit(value & 0xFF);
			break;
		case UART0FCR & 0xFF:  frameControl = value; break;
		case UART0LCR & 0xFF:  lineControl = value; break;
		case UART0CON & 0xFF:
			// a port being switched on finds out about the lines' state
			if ((value & PortCtrlEnable) && !(portControl & PortCtrlEnable) && connected)
				modemStatusChanged = true;
			portControl = value;
			break;
		case UART0INT & 0xFF:  modemStatusChanged = false; break; // acknowledges a modem status change
		case UART0INTM & 0xFF: interruptMask = value; break;
		}
	}
	uint32_t readReg8(uint32_t reg) { return readReg(reg); }
	uint32_t readReg32(uint32_t reg) { return readReg(reg); }
	void writeReg8(uint32_t reg, uint8_t value) { writeReg(reg, value); }
	void writeReg32(uint32_t reg, uint32_t value) { writeReg(reg, value); }
};
