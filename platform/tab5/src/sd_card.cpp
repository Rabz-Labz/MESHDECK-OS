// sd_card.cpp - see sd_card.h.
//
// The Tab5 wires its microSD socket to the ESP32-P4's SDMMC slot 0 (CLK 43, CMD 44, D0-D3 39-42)
// and powers the card's IO from the P4's on-chip LDO channel 4, as in M5Stack's board support.
//
// Hardware note (found on MESHDECK 1.x): slot 0's pins are reached only through the dedicated
// IO_MUX. Naming the pins explicitly makes this ESP-IDF version route them through the GPIO
// matrix instead, which slot 0 doesn't support, and the card times out (error 0x107). So every pin
// is left "unset" (0, which ESP-IDF treats as not set), exactly as Arduino's SD_MMC does.
#include "sd_card.h"
#include <Arduino.h>
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

namespace tab5 {

static uint64_t total = 0, freeBytes = 0;

bool mountSd() {
    // Step 1: power the card's IO lines from on-chip LDO channel 4.
    sd_pwr_ctrl_ldo_config_t ldo = {};
    ldo.ldo_chan_id = 4;
    sd_pwr_ctrl_handle_t pwr = nullptr;
    if (sd_pwr_ctrl_new_on_chip_ldo(&ldo, &pwr) != ESP_OK) return false;
    // Step 2: try 4-bit mode first (faster), then 1-bit (works with more cards).
    for (int width : {4, 1}) {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_0;
        host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
        host.flags = width == 4 ? SDMMC_HOST_FLAG_4BIT : SDMMC_HOST_FLAG_1BIT;
        host.pwr_ctrl_handle = pwr;
        sdmmc_slot_config_t slot = {};   // all pins unset on purpose (see the note above)
        slot.cd = SDMMC_SLOT_NO_CD;
        slot.wp = SDMMC_SLOT_NO_WP;
        slot.width = width;
        esp_vfs_fat_sdmmc_mount_config_t mount = {};
        mount.format_if_mount_failed = false;   // never wipe a card we can't read
        mount.max_files = 8;
        mount.allocation_unit_size = 16 * 1024;
        sdmmc_card_t* card = nullptr;
        if (esp_vfs_fat_sdmmc_mount(kSdRoot, &host, &slot, &mount, &card) == ESP_OK) {
            esp_vfs_fat_info(kSdRoot, &total, &freeBytes);
            Serial.printf("SD mounted: %s, %d-bit, %u MB\n", card->cid.name, width, (unsigned)(total >> 20));
            return true;
        }
    }
    // Step 3: no card (or unreadable): release the LDO so it isn't left powered.
    sd_pwr_ctrl_del_on_chip_ldo(pwr);
    return false;
}

uint64_t sdTotalBytes() { return total; }
uint64_t sdFreeBytes() { return freeBytes; }

} // namespace tab5
