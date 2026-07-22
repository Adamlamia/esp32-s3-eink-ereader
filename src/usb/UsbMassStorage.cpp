// ===========================================================================
//  UsbMassStorage.cpp
// ===========================================================================
//  Implementation is compiled only when ENABLE_USB_MSC=1. It initialises the
//  microSD card through the ESP-IDF SD-over-SPI driver (block level, no FAT
//  mount) and bridges its sectors to the Arduino TinyUSB MSC class.
// ===========================================================================
#include "UsbMassStorage.h"
#include "config.h"

#if ENABLE_USB_MSC

#include "USB.h"
#include "USBMSC.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

// TinyUSB MSC callbacks are C-style, so the card handle lives at file scope.
static USBMSC      s_msc;
static sdmmc_card_t s_card;
static uint32_t    s_sectorSize = 512;

// Host wants to read `bufsize` bytes starting at sector `lba` (+byte offset).
static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    uint32_t startSector = lba + offset / s_sectorSize;
    uint32_t count       = bufsize / s_sectorSize;
    if (sdmmc_read_sectors(&s_card, buffer, startSector, count) != ESP_OK) return -1;
    return (int32_t)bufsize;
}

// Host writes `bufsize` bytes starting at sector `lba` (+byte offset).
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    uint32_t startSector = lba + offset / s_sectorSize;
    uint32_t count       = bufsize / s_sectorSize;
    if (sdmmc_write_sectors(&s_card, buffer, startSector, count) != ESP_OK) return -1;
    return (int32_t)bufsize;
}

// Host (un)mount / eject notification.
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition; (void)start; (void)load_eject;
    return true;
}

bool UsbMassStorage::begin() {
    // 1) Bring up the SPI bus and the SD card as a raw block device.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus = {};
    bus.mosi_io_num = SD_MOSI;
    bus.miso_io_num = SD_MISO;
    bus.sclk_io_num = SD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4092;
    if (spi_bus_initialize((spi_host_device_t)host.slot, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        Serial.println("[USB-MSC] SPI bus init failed");
        return false;
    }

    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = (gpio_num_t)SD_CS;
    dev.host_id = (spi_host_device_t)host.slot;

    sdspi_host_init();
    sdspi_dev_handle_t devHandle;
    if (sdspi_host_init_device(&dev, &devHandle) != ESP_OK) {
        Serial.println("[USB-MSC] sdspi device init failed");
        return false;
    }
    host.slot = devHandle;

    if (sdmmc_card_init(&host, &s_card) != ESP_OK) {
        Serial.println("[USB-MSC] card init failed (no SD inserted?)");
        return false;
    }

    s_sectorSize = s_card.csd.sector_size ? s_card.csd.sector_size : 512;
    _sectorSize  = s_sectorSize;
    _sectorCount = s_card.csd.capacity;    // total sectors

    Serial.printf("[USB-MSC] card ready: %u sectors x %u B (%.1f MB)\n",
                  (unsigned)_sectorCount, (unsigned)_sectorSize,
                  (double)_sectorCount * _sectorSize / (1024.0 * 1024.0));

    // 2) Expose it over USB Mass Storage.
    s_msc.vendorID("LILYGO");
    s_msc.productID("EReader SD");
    s_msc.productRevision("1.0");
    s_msc.onRead(onRead);
    s_msc.onWrite(onWrite);
    s_msc.onStartStop(onStartStop);
    s_msc.mediaPresent(true);
    s_msc.begin(_sectorCount, _sectorSize);

    USB.begin();
    _active = true;
    Serial.println("[USB-MSC] drive mode active — SD card mounted on host");
    return true;
}

#else  // ENABLE_USB_MSC == 0

// Stub so the class links even when the feature is compiled out.
bool UsbMassStorage::begin() { return false; }

#endif // ENABLE_USB_MSC
