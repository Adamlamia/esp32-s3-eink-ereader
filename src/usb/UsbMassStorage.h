#pragma once
// ===========================================================================
//  UsbMassStorage  —  expose the microSD card as a USB drive ("drive mode")
// ===========================================================================
//  Opt-in feature (compile with -DENABLE_USB_MSC=1). When active, the reader
//  presents the raw SD card to a host computer over USB Mass Storage, so books
//  can be dragged onto the card over the cable — no Wi-Fi upload needed.
//
//  Because the host takes exclusive block-level control of the card, the
//  reader must NOT also mount the same card while drive mode is active. The
//  intended flow (see main.cpp): hold BOOT while plugging into USB -> enter
//  drive mode; otherwise boot normally as a reader.
// ===========================================================================
#include <Arduino.h>

class UsbMassStorage {
public:
    // Initialise the SD card at block level and start USB MSC.
    // Returns false if the card could not be initialised.
    bool begin();

    bool active() const { return _active; }

    uint32_t sectorCount() const { return _sectorCount; }
    uint32_t sectorSize()  const { return _sectorSize; }

private:
    bool     _active      = false;
    uint32_t _sectorCount = 0;
    uint32_t _sectorSize  = 512;
};
