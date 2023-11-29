/*

ESPectrum, a Sinclair ZX Spectrum emulator for Espressif ESP32 SoC

Copyright (c) 2023 Víctor Iborra [Eremus] and David Crespo [dcrespo3d]
https://github.com/EremusOne/ZX-ESPectrum-IDF

Based on ZX-ESPectrum-Wiimote
Copyright (c) 2020, 2022 David Crespo [dcrespo3d]
https://github.com/dcrespo3d/ZX-ESPectrum-Wiimote

Based on previous work by Ramón Martinez and Jorge Fuertes
https://github.com/rampa069/ZX-ESPectrum

Original project by Pete Todd
https://github.com/retrogubbins/paseVGA

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

To Contact the dev team you can write to zxespectrum@gmail.com or 
visit https://zxespectrum.speccy.org/contacto

*/

#include <stdio.h>
#include <string>

#include "ESPectrum.h"
#include "Snapshot.h"
#include "Config.h"
#include "FileUtils.h"
#include "OSDMain.h"
#include "Ports.h"
#include "MemESP.h"
#include "CPU.h"
#include "Video.h"
#include "messages.h"
#include "AySound.h"
#include "Tape.h"
#include "Z80_JLS/z80.h"
#include "pwm_audio.h"
#include "fabgl.h"
#include "wd1793.h"

#ifndef ESP32_SDL2_WRAPPER
#include "ZXKeyb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/timer.h"
#include "soc/timer_group_struct.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#endif

using namespace std;

// #pragma GCC optimize("O3")

//=======================================================================================
// KEYBOARD
//=======================================================================================
fabgl::PS2Controller ESPectrum::PS2Controller;
bool ESPectrum::ps2kbd2 = false; ///////

//=======================================================================================
// AUDIO
//=======================================================================================
uint8_t ESPectrum::audioBuffer[ESP_AUDIO_SAMPLES_PENTAGON] = { 0 };
uint8_t ESPectrum::overSamplebuf[ESP_AUDIO_OVERSAMPLES_PENTAGON] = { 0 };
signed char ESPectrum::aud_volume = ESP_DEFAULT_VOLUME;
uint32_t ESPectrum::audbufcnt = 0;
uint32_t ESPectrum::faudbufcnt = 0;
uint32_t ESPectrum::audbufcntAY = 0;
uint32_t ESPectrum::faudbufcntAY = 0;
int ESPectrum::lastaudioBit = 0;
int ESPectrum::faudioBit = 0;
int ESPectrum::samplesPerFrame;
int ESPectrum::overSamplesPerFrame;
bool ESPectrum::AY_emu = false;
int ESPectrum::Audio_freq;
int ESPectrum::TapeNameScroller = 0;
// bool ESPectrum::Audio_restart = false;

QueueHandle_t audioTaskQueue;
TaskHandle_t ESPectrum::audioTaskHandle;
uint8_t *param;

//=======================================================================================
// BETADISK
//=======================================================================================

bool ESPectrum::trdos = false;
WD1793 ESPectrum::Betadisk;

//=======================================================================================
// ARDUINO FUNCTIONS
//=======================================================================================

#ifndef ESP32_SDL2_WRAPPER
#define NOP() asm volatile ("nop")
#else
#define NOP() {for(int i=0;i<1000;i++){}}
#endif

// IRAM_ATTR int64_t micros()
// {
//     return esp_timer_get_time();    
// }

IRAM_ATTR unsigned long millis()
{
    return (unsigned long) (esp_timer_get_time() / 1000ULL);
}

// inline void delay(uint32_t ms)
// {
//     vTaskDelay(ms / portTICK_PERIOD_MS);
// }

IRAM_ATTR void delayMicroseconds(int64_t us)
{
    int64_t m = esp_timer_get_time();
    if(us){
        int64_t e = (m + us);
        if(m > e){ //overflow
            while(esp_timer_get_time() > e){
                NOP();
            }
        }
        while(esp_timer_get_time() < e){
            NOP();
        }
    }
}

//=======================================================================================
// TIMING
//=======================================================================================

static double totalseconds = 0;
static double totalsecondsnodelay = 0;

int64_t ESPectrum::target;

//=======================================================================================
// LOGGING / TESTING
//=======================================================================================

int ESPectrum::ESPoffset = 0;

void showMemInfo(const char* caption = "ZX-ESPectrum-IDF") {

#ifndef ESP32_SDL2_WRAPPER

multi_heap_info_t info;

heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
printf("=========================================================================\n");
printf(" %s - Mem info:\n",caption);
printf("-------------------------------------------------------------------------\n");
printf("Total currently free in all non-continues blocks: %d\n", info.total_free_bytes);
printf("Minimum free ever: %d\n", info.minimum_free_bytes);
printf("Largest continues block to allocate big array: %d\n", info.largest_free_block);
printf("Heap caps get free size (MALLOC_CAP_8BIT): %d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
printf("Heap caps get free size (MALLOC_CAP_32BIT): %d\n", heap_caps_get_free_size(MALLOC_CAP_32BIT));
printf("Heap caps get free size (MALLOC_CAP_INTERNAL): %d\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
printf("=========================================================================\n\n");

// heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

// printf("=========================================================================\n");
// heap_caps_print_heap_info(MALLOC_CAP_8BIT);            

// printf("=========================================================================\n");
// heap_caps_print_heap_info(MALLOC_CAP_32BIT);                        

// printf("=========================================================================\n");
// heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);

// printf("=========================================================================\n");
// heap_caps_print_heap_info(MALLOC_CAP_DMA);            

// printf("=========================================================================\n");
// heap_caps_print_heap_info(MALLOC_CAP_EXEC);            

// printf("=========================================================================\n");
// heap_caps_print_heap_info(MALLOC_CAP_IRAM_8BIT);            

// printf("=========================================================================\n");
// heap_caps_dump_all();

// printf("=========================================================================\n");

// UBaseType_t wm;
// wm = uxTaskGetStackHighWaterMark(audioTaskHandle);
// printf("Audio Task Stack HWM: %u\n", wm);
// // wm = uxTaskGetStackHighWaterMark(loopTaskHandle);
// // printf("Loop Task Stack HWM: %u\n", wm);
// wm = uxTaskGetStackHighWaterMark(VIDEO::videoTaskHandle);
// printf("Video Task Stack HWM: %u\n", wm);

#endif

}

//=======================================================================================
// BOOT KEYBOARD
//=======================================================================================
void ESPectrum::bootKeyboard() {

    auto Kbd = PS2Controller.keyboard();
    fabgl::VirtualKeyItem NextKey;
    string kbdstr = "";
	bool r = false;

	if (ZXKeyb::Exists) {
		
		// Process physical keyboard
		ZXKeyb::process();
		// Detect and process physical kbd menu key combinations
		
		if (!bitRead(ZXKeyb::ZXcols[3], 0)) {     // 1
			if (!bitRead(ZXKeyb::ZXcols[2], 0)) { // Q
				kbdstr = "1Q";
			} else
			if (!bitRead(ZXKeyb::ZXcols[2], 1)) { // W
				kbdstr = "1W";
			}
		} else
		if (!bitRead(ZXKeyb::ZXcols[3], 1)) {     // 2
			if (!bitRead(ZXKeyb::ZXcols[2], 0)) { // Q
				kbdstr = "2Q";
			} else
			if (!bitRead(ZXKeyb::ZXcols[2], 1)) { // W
				kbdstr = "2W";
			}
		} else
		if (!bitRead(ZXKeyb::ZXcols[3], 2)) {     // 3
			if (!bitRead(ZXKeyb::ZXcols[2], 0)) { // Q
				kbdstr = "3Q";
			} else
			if (!bitRead(ZXKeyb::ZXcols[2], 1)) { // W
				kbdstr = "3W";
			}
		}
		
	}

    while (Kbd->virtualKeyAvailable()) {
        r = Kbd->getNextVirtualKey(&NextKey);
        if (r) {

            // Check keyboard status ///////
            if ((PS2Controller.keyboard()->isVKDown(fabgl::VK_q) && PS2Controller.keyboard()->isVKDown(fabgl::VK_1))) kbdstr = "1Q";
            if ((PS2Controller.keyboard()->isVKDown(fabgl::VK_w) && PS2Controller.keyboard()->isVKDown(fabgl::VK_1))) kbdstr = "1W";
            if ((PS2Controller.keyboard()->isVKDown(fabgl::VK_q) && PS2Controller.keyboard()->isVKDown(fabgl::VK_2))) kbdstr = "2Q";
            if ((PS2Controller.keyboard()->isVKDown(fabgl::VK_w) && PS2Controller.keyboard()->isVKDown(fabgl::VK_2))) kbdstr = "2W";
            if ((PS2Controller.keyboard()->isVKDown(fabgl::VK_q) && PS2Controller.keyboard()->isVKDown(fabgl::VK_3))) kbdstr = "3Q";
            if ((PS2Controller.keyboard()->isVKDown(fabgl::VK_w) && PS2Controller.keyboard()->isVKDown(fabgl::VK_3))) kbdstr = "3W";
            ///////
            
            /* ///////
            // Check keyboard status
            if (PS2Controller.keyboard()->isVKDown(fabgl::VK_1)) kbdstr += "1";
            if (PS2Controller.keyboard()->isVKDown(fabgl::VK_2)) kbdstr += "2";
            if (PS2Controller.keyboard()->isVKDown(fabgl::VK_3)) kbdstr += "3";
            if (PS2Controller.keyboard()->isVKDown(fabgl::VK_Q) || PS2Controller.keyboard()->isVKDown(fabgl::VK_q)) kbdstr += "Q";
            if (PS2Controller.keyboard()->isVKDown(fabgl::VK_W) || PS2Controller.keyboard()->isVKDown(fabgl::VK_w)) kbdstr += "W";
            */ ///////

        }

    }

	if (kbdstr != "") {
		if (kbdstr == "1Q") {
			Config::aspect_16_9=false;
			Config::videomode=0;
		} else
		if (kbdstr == "1W") {
			Config::aspect_16_9=true;
			Config::videomode=0;
		} else
		if (kbdstr == "2Q") {
			Config::aspect_16_9=false;
			Config::videomode=1;
		} else
		if (kbdstr == "2W") {
			Config::aspect_16_9=true;
			Config::videomode=1;
		} else
		if (kbdstr == "3Q") {
			Config::aspect_16_9=false;
			Config::videomode=2;
		} else
		if (kbdstr == "3W") {
			Config::aspect_16_9=true;
			Config::videomode=2;
		}
		
		Config::ram_file="none";
		Config::save();
		//printf("%s\n", b.c_str());
		//break; ///////
	}

}

//=======================================================================================
// SETUP
//=======================================================================================
// TaskHandle_t ESPectrum::loopTaskHandle;

void ESPectrum::setup() 
{

    #ifndef ESP32_SDL2_WRAPPER

    if (Config::slog_on) {

        printf("------------------------------------\n");
        printf("| ESPectrum: booting               |\n");        
        printf("------------------------------------\n");    

        showMemInfo();

    }

    #endif

    //=======================================================================================
    // PHYSICAL KEYBOARD (SINCLAIR 8 + 5 MEMBRANE KEYBOARD)
    //=======================================================================================

    ZXKeyb::setup();
   
    //=======================================================================================
    // LOAD CONFIG
    //=======================================================================================
    
    Config::load();

    //=======================================================================================
    // INIT PS/2 KEYBOARD
    //=======================================================================================

    //ESPectrum::ps2kbd2 = (Config::ps2_dev2 != 0); ///////
	ESPectrum::ps2kbd2 = !((ZXKeyb::Exists) || (Config::ps2_dev2 == 0)); // ZXKeyb check is disabling the 2nd port when physical keyboard exists - See also Config.cpp ///////
    
    if (ZXKeyb::Exists) {
        PS2Controller.begin(ps2kbd2 ? PS2Preset::KeyboardPort0 : PS2Preset::zxKeyb, KbdMode::CreateVirtualKeysQueue);
    } else {
        PS2Controller.begin(ps2kbd2 ? PS2Preset::KeyboardPort0_KeybJoystickPort1 : PS2Preset::KeyboardPort0, KbdMode::CreateVirtualKeysQueue);
    }

    //ps2kbd2 &= !ZXKeyb::Exists; ///////
	
	//PS2Controller.begin(ps2kbd2 ? (ZXKeyb::Exists ? PS2Preset::KeyboardPort0_KeyboardAltPort1 : PS2Preset::KeyboardPort0_KeybJoystickPort1) : PS2Preset::KeyboardPort0, KbdMode::CreateVirtualKeysQueue); // This could replace the previous code while testing second keyboard port when ZXKeyb is available ///////
	//PS2Controller.keyboard()->reset(false); // This is already setting the ScancodeSet 2 - Needed to allow video mode change on boot by pressing keys only (not alternate) ///////
	PS2Controller.keyboard()->resetOnly(); ///////
	if(ps2kbd2) PS2Controller.keybjoystick()->resetOnly(); ///////

    // Set Scroll Lock Led as current CursorAsJoy value
    PS2Controller.keyboard()->setLEDs(false, false, Config::CursorAsJoy);
    if(ps2kbd2) PS2Controller.keybjoystick()->setLEDs(false, false, Config::CursorAsJoy);

    #ifndef ESP32_SDL2_WRAPPER

    if (Config::slog_on) {
        showMemInfo("Keyboard started");
    }

    // Get chip information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    Config::esp32rev = chip_info.revision;

    if (Config::slog_on) {

        printf("\n");
        printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
                CONFIG_IDF_TARGET,
                chip_info.cores,
                (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
                (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
        printf("silicon revision %d, ", chip_info.revision);
        printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
                (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
        printf("IDF Version: %s\n",esp_get_idf_version());
        printf("\n");

        if (Config::slog_on) printf("Executing on core: %u\n", xPortGetCoreID());

        showMemInfo();

    }
    
    #endif

    //=======================================================================================
    // BOOTKEYS: Read keyboard for 200 ms. checking boot keys
    //=======================================================================================

    // printf("Waiting boot keys\n");
    bootKeyboard();
    // printf("End Waiting boot keys\n");

    //=======================================================================================
    // MEMORY SETUP
    //=======================================================================================

    MemESP::ram5 = staticMemPage0;
    MemESP::ram0 = staticMemPage1;
    MemESP::ram2 = staticMemPage2;

    MemESP::ram1 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT);
    MemESP::ram3 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT);
    MemESP::ram4 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT);
    MemESP::ram6 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT);
    MemESP::ram7 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT);

    // MemESP::ram1 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    // MemESP::ram3 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    // MemESP::ram4 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    // MemESP::ram6 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    // MemESP::ram7 = (unsigned char *) heap_caps_malloc(0x4000, MALLOC_CAP_8BIT);

    if (Config::slog_on) {
        if (MemESP::ram1 == NULL) printf("ERROR! Unable to allocate ram1\n");        
        if (MemESP::ram3 == NULL) printf("ERROR! Unable to allocate ram3\n");        
        if (MemESP::ram4 == NULL) printf("ERROR! Unable to allocate ram4\n");        
        if (MemESP::ram6 == NULL) printf("ERROR! Unable to allocate ram6\n");
        if (MemESP::ram7 == NULL) printf("ERROR! Unable to allocate ram7\n");
    }

    MemESP::ram[0] = MemESP::ram0; MemESP::ram[1] = MemESP::ram1;
    MemESP::ram[2] = MemESP::ram2; MemESP::ram[3] = MemESP::ram3;
    MemESP::ram[4] = MemESP::ram4; MemESP::ram[5] = MemESP::ram5;
    MemESP::ram[6] = MemESP::ram6; MemESP::ram[7] = MemESP::ram7;

    MemESP::romInUse = 0;
    MemESP::bankLatch = 0;
    MemESP::videoLatch = 0;
    MemESP::romLatch = 0;

    MemESP::ramCurrent[0] = (unsigned char *)MemESP::rom[MemESP::romInUse];
    MemESP::ramCurrent[1] = (unsigned char *)MemESP::ram[5];
    MemESP::ramCurrent[2] = (unsigned char *)MemESP::ram[2];
    MemESP::ramCurrent[3] = (unsigned char *)MemESP::ram[MemESP::bankLatch];

    MemESP::ramContended[0] = false;
    MemESP::ramContended[1] = Config::getArch() == "Pentagon" ? false : true;
    MemESP::ramContended[2] = false;
    MemESP::ramContended[3] = false;

    if (Config::getArch() == "48K") MemESP::pagingLock = 1; else MemESP::pagingLock = 0;

    if (Config::slog_on) showMemInfo("RAM Initialized");

    //=======================================================================================
    // VIDEO
    //=======================================================================================

    VIDEO::Init();
    VIDEO::Reset();
    
    if (Config::slog_on) showMemInfo("VGA started");

    //=======================================================================================
    // INIT FILESYSTEM
    //=======================================================================================
    
    FileUtils::initFileSystem();

    //=======================================================================================
    // AUDIO
    //=======================================================================================

    // Set samples per frame and AY_emu flag depending on arch
    if (Config::getArch() == "48K") {
        overSamplesPerFrame=ESP_AUDIO_OVERSAMPLES_48;
        samplesPerFrame=ESP_AUDIO_SAMPLES_48; 
        AY_emu = Config::AY48;
        Audio_freq = ESP_AUDIO_FREQ_48;
    } else if (Config::getArch() == "128K") {
        overSamplesPerFrame=ESP_AUDIO_OVERSAMPLES_128;
        samplesPerFrame=ESP_AUDIO_SAMPLES_128;
        AY_emu = true;        
        Audio_freq = ESP_AUDIO_FREQ_128;
    } else if (Config::getArch() == "Pentagon") {
        overSamplesPerFrame=ESP_AUDIO_OVERSAMPLES_PENTAGON;
        samplesPerFrame=ESP_AUDIO_SAMPLES_PENTAGON;
        AY_emu = true;        
        Audio_freq = ESP_AUDIO_FREQ_PENTAGON;
    }

    ESPoffset = 0;

    // Create Audio task
    audioTaskQueue = xQueueCreate(1, sizeof(uint8_t *));
    // Latest parameter = Core. In ESPIF, main task runs on core 0 by default. In Arduino, loop() runs on core 1.
    xTaskCreatePinnedToCore(&ESPectrum::audioTask, "audioTask", 1024, NULL, configMAX_PRIORITIES - 1, &audioTaskHandle, 1);

    // AY Sound
    AySound::init();
    AySound::set_sound_format(Audio_freq,1,8);
    AySound::set_stereo(AYEMU_MONO,NULL);
    AySound::reset();

    // Init tape
    Tape::Init();
    Tape::tapeFileName = "none";
    Tape::tapeStatus = TAPE_STOPPED;
    Tape::SaveStatus = SAVE_STOPPED;
    Tape::romLoading = false;

    // Init CPU
    Z80::create();

    // Set Ports starting values
    for (int i = 0; i < 128; i++) Ports::port[i] = 0xBF;
    if (Config::joystick) Ports::port[0x1f] = 0; // Kempston

    // Init disk controller
    Betadisk.Init();

    // Load romset
    Config::requestMachine(Config::getArch(), Config::getRomSet());

    // Reset cpu
    CPU::reset();

    // Load snapshot if present in Config::ram_file
    if (Config::ram_file != NO_RAM_FILE) {

        FileUtils::SNA_Path = Config::SNA_Path;
        FileUtils::fileTypes[DISK_SNAFILE].begin_row = Config::SNA_begin_row;
        FileUtils::fileTypes[DISK_SNAFILE].focus = Config::SNA_focus;

        FileUtils::TAP_Path = Config::TAP_Path;
        FileUtils::fileTypes[DISK_TAPFILE].begin_row = Config::TAP_begin_row;
        FileUtils::fileTypes[DISK_TAPFILE].focus = Config::TAP_focus;

        FileUtils::DSK_Path = Config::DSK_Path;
        FileUtils::fileTypes[DISK_DSKFILE].begin_row = Config::DSK_begin_row;
        FileUtils::fileTypes[DISK_DSKFILE].focus = Config::DSK_focus;

        LoadSnapshot(Config::ram_file,"");

        Config::last_ram_file = Config::ram_file;
        #ifndef SNAPSHOT_LOAD_LAST
        Config::ram_file = NO_RAM_FILE;
        Config::save("ram");
        #endif

    }

    if (Config::slog_on) showMemInfo("ZX-ESPectrum-IDF setup finished.");

    // Create loop function as task: it doesn't seem better than calling from main.cpp and increases RAM consumption (4096 bytes for stack).
    // xTaskCreatePinnedToCore(&ESPectrum::loop, "loopTask", 4096, NULL, 1, &loopTaskHandle, 0);

}

//=======================================================================================
// RESET
//=======================================================================================
void ESPectrum::reset()
{

    // Ports
    for (int i = 0; i < 128; i++) Ports::port[i] = 0xBF;
    if (Config::joystick) Ports::port[0x1f] = 0; // Kempston

    // Memory
    MemESP::bankLatch = 0;
    MemESP::videoLatch = 0;
    MemESP::romLatch = 0;

    string arch = Config::getArch();

    if (arch == "48K") MemESP::pagingLock = 1; else MemESP::pagingLock = 0;

    MemESP::romInUse = 0;

    MemESP::ramCurrent[0] = (unsigned char *)MemESP::rom[MemESP::romInUse];
    MemESP::ramCurrent[1] = (unsigned char *)MemESP::ram[5];
    MemESP::ramCurrent[2] = (unsigned char *)MemESP::ram[2];
    MemESP::ramCurrent[3] = (unsigned char *)MemESP::ram[MemESP::bankLatch];

    MemESP::ramContended[0] = false;
    MemESP::ramContended[1] = arch == "Pentagon" ? false : true;
    MemESP::ramContended[2] = false;
    MemESP::ramContended[3] = false;

    VIDEO::Reset();

    // Reinit disk controller
    Betadisk.ShutDown();
    Betadisk.Init();

    Tape::tapeFileName = "none";
    if (Tape::tape != NULL) {
        fclose(Tape::tape);
        Tape::tape = NULL;
    }
    Tape::tapeStatus = TAPE_STOPPED;
    Tape::SaveStatus = SAVE_STOPPED;
    Tape::romLoading = false;

    // Empty audio buffers
    for (int i=0;i<ESP_AUDIO_OVERSAMPLES_PENTAGON;i++) overSamplebuf[i]=0;
    for (int i=0;i<ESP_AUDIO_SAMPLES_PENTAGON;i++) {
        audioBuffer[i]=0;
        AySound::SamplebufAY[i]=0;
    }
    lastaudioBit=0;

    // Set samples per frame and AY_emu flag depending on arch
    if (arch == "48K") {
        overSamplesPerFrame=ESP_AUDIO_OVERSAMPLES_48;
        samplesPerFrame=ESP_AUDIO_SAMPLES_48; 
        AY_emu = Config::AY48;
        Audio_freq = ESP_AUDIO_FREQ_48;
    } else if (arch == "128K") {
        overSamplesPerFrame=ESP_AUDIO_OVERSAMPLES_128;
        samplesPerFrame=ESP_AUDIO_SAMPLES_128;
        AY_emu = true;        
        Audio_freq = ESP_AUDIO_FREQ_128;
    } else if (arch == "Pentagon") {
        overSamplesPerFrame=ESP_AUDIO_OVERSAMPLES_PENTAGON;
        samplesPerFrame=ESP_AUDIO_SAMPLES_PENTAGON;
        AY_emu = true;        
        Audio_freq = ESP_AUDIO_FREQ_PENTAGON;
    }

    ESPoffset = 0;

    pwm_audio_stop();
    
    delay(100); // Maybe this fix random sound lost ?
    
    pwm_audio_set_param(Audio_freq,LEDC_TIMER_8_BIT,1);
    
    pwm_audio_start();
    
    pwm_audio_set_volume(aud_volume);

    // Reset AY emulation
    AySound::init();
    AySound::set_sound_format(Audio_freq,1,8);
    AySound::set_stereo(AYEMU_MONO,NULL);
    AySound::reset();

    CPU::reset();

}

//=======================================================================================
// KEYBOARD / KEMPSTON
//=======================================================================================
IRAM_ATTR bool ESPectrum::readKbd(fabgl::VirtualKeyItem *Nextkey) {
    
    bool r = PS2Controller.keyboard()->getNextVirtualKey(Nextkey);
    // Global keys
    if (Nextkey->down) {
        if (Nextkey->vk == fabgl::VK_PRINTSCREEN) { // Capture framebuffer to BMP file in SD Card (thx @dcrespo3d!)
            // pwm_audio_stop();
            CaptureToBmp();
            // pwm_audio_start();
            r = false;
        } else
        if (Nextkey->vk == fabgl::VK_SCROLLLOCK) { // Change CursorAsJoy setting
            Config::CursorAsJoy = !Config::CursorAsJoy;
            PS2Controller.keyboard()->setLEDs(false,false,Config::CursorAsJoy);
            if(ps2kbd2)
                PS2Controller.keybjoystick()->setLEDs(false, false, Config::CursorAsJoy);
            Config::save("CursorAsJoy");
            r = false;
        }
    }

    return r;
}

//
// Read second ps/2 port and inject on first queue
//
IRAM_ATTR void ESPectrum::readKbdJoy() {

    if (ps2kbd2) {

        fabgl::VirtualKeyItem NextKey;
        auto KbdJoy = PS2Controller.keybjoystick();    

        while (KbdJoy->virtualKeyAvailable()) {
            PS2Controller.keybjoystick()->getNextVirtualKey(&NextKey);
            ESPectrum::PS2Controller.keyboard()->injectVirtualKey(NextKey.vk, NextKey.down, false);
        }

    }

}

IRAM_ATTR void ESPectrum::processKeyboard() {

    static uint8_t PS2cols[8] = { 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf };    
    static int zxDelay = 0;
    auto Kbd = PS2Controller.keyboard();
    fabgl::VirtualKeyItem NextKey;
    fabgl::VirtualKey KeytoESP;
    bool Kdown;
    bool r = false;
    bool jLeft = true;
    bool jRight = true;
    bool jUp = true;
    bool jDown = true;
    bool jFire = true;
    bool jShift = true;
    
    // START - Virtual keyboard pending key management ///////
    if (OSD::KeytoEMU != fabgl::VK_NONE) { // Send keydown to queue from onscreen virtual keyboard
        if (OSD::KeytoEMUtime == 0) {
            Kbd->emptyVirtualKeyQueue(); // Set all PS2 keys as not pressed
            Kbd->injectVirtualKey(fabgl::VK_RETURN, false, false); // Solve problem of return key still pressed from OSD
            Kbd->injectVirtualKey(OSD::KeytoEMU, true, false);
        }
        if (OSD::KeytoEMUtime < 10) { // Ensure the press of the onscreen key will be simulated enough time
            OSD::KeytoEMUtime ++;
        } else {
            Kbd->injectVirtualKey(OSD::KeytoEMU, false, false); // Send keyup to queue from virtual keyboard when the needed time is reached
            OSD::KeytoEMU = fabgl::VK_NONE;
            OSD::KeytoEMUtime = 0;
        }
        if (ZXKeyb::Exists) {
            zxDelay = 50; // Prevent undesired keystrokes when the key is still pressed
        }
    }
    // END - Virtual keyboard pending key management ///////

    readKbdJoy();
    
    while (Kbd->virtualKeyAvailable()) {

        r = readKbd(&NextKey);

        if (r) {

            KeytoESP = NextKey.vk;
            Kdown = NextKey.down;
          
            if ((Kdown) && ((KeytoESP >= fabgl::VK_F1 && KeytoESP <= fabgl::VK_F12) || KeytoESP == fabgl::VK_PAUSE)) {

                OSD::do_OSD(KeytoESP,NextKey.CTRL);

                Kbd->emptyVirtualKeyQueue();
                
                // Set all zx keys as not pressed
                for (uint8_t i = 0; i < 8; i++) ZXKeyb::ZXcols[i] = 0xbf;
                zxDelay = 15;
                
                totalseconds = 0;
                totalsecondsnodelay = 0;
                VIDEO::framecnt = 0;

                return;

            }

            if (Config::CursorAsJoy) {

                // Kempston Joystick emulation
                if (Config::joystick) {

                    Ports::port[0x1f] = 0;

                    jShift = !(Kbd->isVKDown(fabgl::VK_LSHIFT) || Kbd->isVKDown(fabgl::VK_RSHIFT));

                    if (Kbd->isVKDown(fabgl::VK_RIGHT) || Kbd->isVKDown(fabgl::VK_KP_RIGHT)) {
                        jRight = jShift;
                        bitWrite(Ports::port[0x1f], 0, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_LEFT) || Kbd->isVKDown(fabgl::VK_KP_LEFT)) {
                        jLeft = jShift;
                        bitWrite(Ports::port[0x1f], 1, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_DOWN) || Kbd->isVKDown(fabgl::VK_KP_DOWN) || Kbd->isVKDown(fabgl::VK_KP_CENTER)) {
                        jDown = jShift;
                        bitWrite(Ports::port[0x1f], 2, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_UP) || Kbd->isVKDown(fabgl::VK_KP_UP)) {
                        jUp = jShift;
                        bitWrite(Ports::port[0x1f], 3, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_RALT)) {
                        jFire = jShift;
                        bitWrite(Ports::port[0x1f], 4, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_SLASH) || Kbd->isVKDown(fabgl::VK_RGUI)) {
                        bitWrite(Ports::port[0x1f], 5, 1);
                    }

                }

                bitWrite(PS2cols[3], 4, (!Kbd->isVKDown(fabgl::VK_5)) & (!Kbd->isVKDown(fabgl::VK_PERCENT))
                    & ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_LEFT) & !Kbd->isVKDown(fabgl::VK_KP_LEFT)))
                    & (jLeft)
                        ); // Cursor joystick Left
                bitWrite(PS2cols[4], 0, (!Kbd->isVKDown(fabgl::VK_0)) & (!Kbd->isVKDown(fabgl::VK_RIGHTPAREN))
                                    &   (!Kbd->isVKDown(fabgl::VK_BACKSPACE))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_RALT)))
                                    & (jFire)
                                    ); // Cursor joystick Fire
                bitWrite(PS2cols[4], 2, (!Kbd->isVKDown(fabgl::VK_8)) & (!Kbd->isVKDown(fabgl::VK_ASTERISK))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_RIGHT) & !Kbd->isVKDown(fabgl::VK_KP_RIGHT)))
                                    & (jRight)
                                    ); // Cursor joystick Right
                bitWrite(PS2cols[4], 3, (!Kbd->isVKDown(fabgl::VK_7)) & (!Kbd->isVKDown(fabgl::VK_AMPERSAND))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_UP) & !Kbd->isVKDown(fabgl::VK_KP_UP)))
                                    & (jUp)
                                    ); // Cursor joystick Up
                bitWrite(PS2cols[4], 4, (!Kbd->isVKDown(fabgl::VK_6)) & (!Kbd->isVKDown(fabgl::VK_CARET))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_DOWN) & !Kbd->isVKDown(fabgl::VK_KP_DOWN)  & !Kbd->isVKDown(fabgl::VK_KP_CENTER)))
                                    & (jDown)
                                    ); // Cursor joystick Down

            } else {

                // Kempston Joystick emulation
                if (Config::joystick) {

                    // printf("VK: %d\n",(int)KeytoESP);

                    Ports::port[0x1f] = 0;

                    if (Kbd->isVKDown(fabgl::VK_KP_RIGHT)) {
                        bitWrite(Ports::port[0x1f], 0, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_KP_LEFT)) {
                        bitWrite(Ports::port[0x1f], 1, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_KP_DOWN) || Kbd->isVKDown(fabgl::VK_KP_CENTER)) {
                        bitWrite(Ports::port[0x1f], 2, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_KP_UP)) {
                        bitWrite(Ports::port[0x1f], 3, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_RALT)) {
                        bitWrite(Ports::port[0x1f], 4, 1);
                    }

                    if (Kbd->isVKDown(fabgl::VK_SLASH) || Kbd->isVKDown(fabgl::VK_RGUI)) {
                        bitWrite(Ports::port[0x1f], 5, 1);
                    }

                }

                // Cursor Keys
                if (Kbd->isVKDown(fabgl::VK_RIGHT)) {
                    jShift = false;
                    jRight = jShift;
                }

                if (Kbd->isVKDown(fabgl::VK_LEFT)) {
                    jShift = false;
                    jLeft = jShift;
                }

                if (Kbd->isVKDown(fabgl::VK_DOWN)) {
                    jShift = false;                
                    jDown = jShift;
                }

                if (Kbd->isVKDown(fabgl::VK_UP)) {
                    jShift = false;
                    jUp = jShift;
                }


                bitWrite(PS2cols[3], 4, (!Kbd->isVKDown(fabgl::VK_5)) & (!Kbd->isVKDown(fabgl::VK_PERCENT))
                    & ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_LEFT)))
                    & (jLeft)
                        ); // Cursor joystick Left

                bitWrite(PS2cols[4], 0, (!Kbd->isVKDown(fabgl::VK_0)) & (!Kbd->isVKDown(fabgl::VK_RIGHTPAREN))
                                    &   (!Kbd->isVKDown(fabgl::VK_BACKSPACE))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_RALT)))
                                    ); // Cursor joystick Fire

                bitWrite(PS2cols[4], 2, (!Kbd->isVKDown(fabgl::VK_8)) & (!Kbd->isVKDown(fabgl::VK_ASTERISK))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_RIGHT)))
                                    & (jRight)
                                    ); // Cursor joystick Right

                bitWrite(PS2cols[4], 3, (!Kbd->isVKDown(fabgl::VK_7)) & (!Kbd->isVKDown(fabgl::VK_AMPERSAND))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_UP)))
                                    & (jUp)
                                    ); // Cursor joystick Up

                bitWrite(PS2cols[4], 4, (!Kbd->isVKDown(fabgl::VK_6)) & (!Kbd->isVKDown(fabgl::VK_CARET))
                                    &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_DOWN) & !Kbd->isVKDown(fabgl::VK_KP_CENTER)) )
                                    & (jDown)
                                    ); // Cursor joystick Down

            }

            // Check keyboard status and map it to Spectrum Ports
            
            bitWrite(PS2cols[0], 0, (!Kbd->isVKDown(fabgl::VK_LSHIFT)) 
                                &   (!Kbd->isVKDown(fabgl::VK_RSHIFT))
                                &   (!Kbd->isVKDown(fabgl::VK_BACKSPACE)) // Backspace
                                & (jShift)
                            ); // CAPS SHIFT

            bitWrite(PS2cols[0], 1, (!Kbd->isVKDown(fabgl::VK_Z)) & (!Kbd->isVKDown(fabgl::VK_z)));
            bitWrite(PS2cols[0], 2, (!Kbd->isVKDown(fabgl::VK_X)) & (!Kbd->isVKDown(fabgl::VK_x)));
            bitWrite(PS2cols[0], 3, (!Kbd->isVKDown(fabgl::VK_C)) & (!Kbd->isVKDown(fabgl::VK_c)));
            bitWrite(PS2cols[0], 4, (!Kbd->isVKDown(fabgl::VK_V)) & (!Kbd->isVKDown(fabgl::VK_v)));

            bitWrite(PS2cols[1], 0, (!Kbd->isVKDown(fabgl::VK_A)) & (!Kbd->isVKDown(fabgl::VK_a)));    
            bitWrite(PS2cols[1], 1, (!Kbd->isVKDown(fabgl::VK_S)) & (!Kbd->isVKDown(fabgl::VK_s)));
            bitWrite(PS2cols[1], 2, (!Kbd->isVKDown(fabgl::VK_D)) & (!Kbd->isVKDown(fabgl::VK_d)));
            bitWrite(PS2cols[1], 3, (!Kbd->isVKDown(fabgl::VK_F)) & (!Kbd->isVKDown(fabgl::VK_f)));
            bitWrite(PS2cols[1], 4, (!Kbd->isVKDown(fabgl::VK_G)) & (!Kbd->isVKDown(fabgl::VK_g)));

            bitWrite(PS2cols[2], 0, (!Kbd->isVKDown(fabgl::VK_Q)) & (!Kbd->isVKDown(fabgl::VK_q)));
            bitWrite(PS2cols[2], 1, (!Kbd->isVKDown(fabgl::VK_W)) & (!Kbd->isVKDown(fabgl::VK_w)));
            bitWrite(PS2cols[2], 2, (!Kbd->isVKDown(fabgl::VK_E)) & (!Kbd->isVKDown(fabgl::VK_e)));
            bitWrite(PS2cols[2], 3, (!Kbd->isVKDown(fabgl::VK_R)) & (!Kbd->isVKDown(fabgl::VK_r)));
            bitWrite(PS2cols[2], 4, (!Kbd->isVKDown(fabgl::VK_T)) & (!Kbd->isVKDown(fabgl::VK_t)));

            bitWrite(PS2cols[3], 0, (!Kbd->isVKDown(fabgl::VK_1)) & (!Kbd->isVKDown(fabgl::VK_EXCLAIM)));
            bitWrite(PS2cols[3], 1, (!Kbd->isVKDown(fabgl::VK_2)) & (!Kbd->isVKDown(fabgl::VK_AT)));
            bitWrite(PS2cols[3], 2, (!Kbd->isVKDown(fabgl::VK_3)) & (!Kbd->isVKDown(fabgl::VK_HASH)));
            bitWrite(PS2cols[3], 3, (!Kbd->isVKDown(fabgl::VK_4)) & (!Kbd->isVKDown(fabgl::VK_DOLLAR)));

            // bitWrite(PS2cols[3], 4, (!Kbd->isVKDown(fabgl::VK_5)) & (!Kbd->isVKDown(fabgl::VK_PERCENT))
            //     & ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_LEFT)))
            //     & (jLeft)
            //         ); // Cursor joystick Left

            // bitWrite(PS2cols[4], 0, (!Kbd->isVKDown(fabgl::VK_0)) & (!Kbd->isVKDown(fabgl::VK_RIGHTPAREN))
            //                     &   (!Kbd->isVKDown(fabgl::VK_BACKSPACE))
            //                     &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_RALT)))
            //                     // & (jFire)
            //                     ); // Cursor joystick Fire

            bitWrite(PS2cols[4], 1, !Kbd->isVKDown(fabgl::VK_9) & (!Kbd->isVKDown(fabgl::VK_LEFTPAREN)));

            // bitWrite(PS2cols[4], 2, (!Kbd->isVKDown(fabgl::VK_8)) & (!Kbd->isVKDown(fabgl::VK_ASTERISK))
            //                     &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_RIGHT)))
            //                     & (jRight)
            //                     ); // Cursor joystick Right

            // bitWrite(PS2cols[4], 3, (!Kbd->isVKDown(fabgl::VK_7)) & (!Kbd->isVKDown(fabgl::VK_AMPERSAND))
            //                     &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_UP)))
            //                     & (jUp)
            //                     ); // Cursor joystick Up

            // bitWrite(PS2cols[4], 4, (!Kbd->isVKDown(fabgl::VK_6)) & (!Kbd->isVKDown(fabgl::VK_CARET))
            //                     &   ((Config::joystick) | (!Kbd->isVKDown(fabgl::VK_KP_DOWN) & !Kbd->isVKDown(fabgl::VK_KP_CENTER)) )
            //                     & (jDown)
            //                     ); // Cursor joystick Down

            bitWrite(PS2cols[5], 0, (!Kbd->isVKDown(fabgl::VK_P)) & (!Kbd->isVKDown(fabgl::VK_p)));
            bitWrite(PS2cols[5], 1, (!Kbd->isVKDown(fabgl::VK_O)) & (!Kbd->isVKDown(fabgl::VK_o)));
            bitWrite(PS2cols[5], 2, (!Kbd->isVKDown(fabgl::VK_I)) & (!Kbd->isVKDown(fabgl::VK_i)));
            bitWrite(PS2cols[5], 3, (!Kbd->isVKDown(fabgl::VK_U)) & (!Kbd->isVKDown(fabgl::VK_u)));
            bitWrite(PS2cols[5], 4, (!Kbd->isVKDown(fabgl::VK_Y)) & (!Kbd->isVKDown(fabgl::VK_y)));

            bitWrite(PS2cols[6], 0, !Kbd->isVKDown(fabgl::VK_RETURN));
            bitWrite(PS2cols[6], 1, (!Kbd->isVKDown(fabgl::VK_L)) & (!Kbd->isVKDown(fabgl::VK_l)));
            bitWrite(PS2cols[6], 2, (!Kbd->isVKDown(fabgl::VK_K)) & (!Kbd->isVKDown(fabgl::VK_k)));
            bitWrite(PS2cols[6], 3, (!Kbd->isVKDown(fabgl::VK_J)) & (!Kbd->isVKDown(fabgl::VK_j)));
            bitWrite(PS2cols[6], 4, (!Kbd->isVKDown(fabgl::VK_H)) & (!Kbd->isVKDown(fabgl::VK_h)));

            bitWrite(PS2cols[7], 0, !Kbd->isVKDown(fabgl::VK_SPACE));
            bitWrite(PS2cols[7], 1, (!Kbd->isVKDown(fabgl::VK_LCTRL)) // SYMBOL SHIFT
                                &   (!Kbd->isVKDown(fabgl::VK_RCTRL))
                                &   (!Kbd->isVKDown(fabgl::VK_COMMA)) // Comma
                                &   (!Kbd->isVKDown(fabgl::VK_PERIOD)) // Period
                                ); // SYMBOL SHIFT
            bitWrite(PS2cols[7], 2, (!Kbd->isVKDown(fabgl::VK_M)) & (!Kbd->isVKDown(fabgl::VK_m))
                                &   (!Kbd->isVKDown(fabgl::VK_PERIOD)) // Period
                                );
            bitWrite(PS2cols[7], 3, (!Kbd->isVKDown(fabgl::VK_N)) & (!Kbd->isVKDown(fabgl::VK_n))
                                &   (!Kbd->isVKDown(fabgl::VK_COMMA)) // Comma
                                );
            bitWrite(PS2cols[7], 4, (!Kbd->isVKDown(fabgl::VK_B)) & (!Kbd->isVKDown(fabgl::VK_b)));

        }

    }

    if (ZXKeyb::Exists) { // START - ZXKeyb Exists
        
        if (zxDelay > 0)
            zxDelay--;
        else
            // Process physical keyboard
            ZXKeyb::process();
        
        // Add extra key combination to show the OSD menu ///////
        if ((!bitRead(ZXKeyb::ZXcols[7],1)) && (!bitRead(ZXKeyb::ZXcols[6],0))) { // SS+Enter ///////
            zxDelay = 15;
            OSD::do_OSD(fabgl::VK_F1,0); // Now needs 0 as argument to parse CTRL key is not needed
            for (uint8_t i = 0; i < 8; i++) ZXKeyb::ZXcols[i] = 0xbf;
            return;
        }

        // Detect and process physical kbd menu key combinations
        // CS+SS+<1..0> -> F1..F10 Keys, CS+SS+Q -> F11, CS+SS+W -> F12, CS+SS+S -> Capture screen
        if ((!bitRead(ZXKeyb::ZXcols[0],0)) && (!bitRead(ZXKeyb::ZXcols[7],1))) {

            zxDelay = 15;

            if (!bitRead(ZXKeyb::ZXcols[3],0)) {
                OSD::do_OSD(fabgl::VK_F1,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[3],1)) {
                OSD::do_OSD(fabgl::VK_F2,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[3],2)) {
                OSD::do_OSD(fabgl::VK_F3,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[3],3)) {
                OSD::do_OSD(fabgl::VK_F4,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[3],4)) {
                OSD::do_OSD(fabgl::VK_F5,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[4],4)) {
                OSD::do_OSD(fabgl::VK_F6,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[4],3)) {
                OSD::do_OSD(fabgl::VK_F7,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[4],2)) {
                OSD::do_OSD(fabgl::VK_F8,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[4],1)) {
                OSD::do_OSD(fabgl::VK_F9,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[4],0)) {
                OSD::do_OSD(fabgl::VK_F10,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[2],0)) {
                OSD::do_OSD(fabgl::VK_F11,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[2],1)) {
                OSD::do_OSD(fabgl::VK_F12,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[5],0)) { // P -> Pause
                OSD::do_OSD(fabgl::VK_PAUSE,0);
            } else
            if (!bitRead(ZXKeyb::ZXcols[5],2)) { // I -> Info
                OSD::do_OSD(fabgl::VK_F1,true);
            } else
            if (!bitRead(ZXKeyb::ZXcols[1],1)) { // S -> Screen capture
                CaptureToBmp();
            } else
            if (!bitRead(ZXKeyb::ZXcols[0],1)) { // Z -> CenterH
                if (Config::CenterH > -16) Config::CenterH--;
                Config::save("CenterH");
                OSD::osdCenteredMsg("Horiz. center: " + to_string(Config::CenterH), LEVEL_INFO, 375);
            } else
            if (!bitRead(ZXKeyb::ZXcols[0],2)) { // X -> CenterH
                if (Config::CenterH < 16) Config::CenterH++;
                Config::save("CenterH");
                OSD::osdCenteredMsg("Horiz. center: " + to_string(Config::CenterH), LEVEL_INFO, 375);
            } else
            if (!bitRead(ZXKeyb::ZXcols[0],3)) { // C -> CenterV
                if (Config::CenterV > -16) Config::CenterV--;
                Config::save("CenterV");
                OSD::osdCenteredMsg("Vert. center: " + to_string(Config::CenterV), LEVEL_INFO, 375);
            } else
            if (!bitRead(ZXKeyb::ZXcols[0],4)) { // V -> CenterV
                if (Config::CenterV < 16) Config::CenterV++;
                Config::save("CenterV");
                OSD::osdCenteredMsg("Vert. center: " + to_string(Config::CenterV), LEVEL_INFO, 375);
            } else
                zxDelay = 0;

            if (zxDelay) {
                // Set all keys as not pressed
                for (uint8_t i = 0; i < 8; i++) ZXKeyb::ZXcols[i] = 0xbf;
                return;
            }
        
        }

        // Combine both keyboards
        for (uint8_t rowidx = 0; rowidx < 8; rowidx++) {
            Ports::port[rowidx] = PS2cols[rowidx] & ZXKeyb::ZXcols[rowidx];
        }

    } else {

        if (r) {
            for (uint8_t rowidx = 0; rowidx < 8; rowidx++) {
                Ports::port[rowidx] = PS2cols[rowidx];
            }
        }

    }

}

// static int bmax = 0;

//=======================================================================================
// AUDIO
//=======================================================================================
IRAM_ATTR void ESPectrum::audioTask(void *unused) {

    size_t written;

    // PWM Audio Init
    pwm_audio_config_t pac;
    pac.duty_resolution    = LEDC_TIMER_8_BIT;
    pac.gpio_num_left      = SPEAKER_PIN;
    pac.ledc_channel_left  = LEDC_CHANNEL_0;
    pac.gpio_num_right     = -1;
    pac.ledc_channel_right = LEDC_CHANNEL_1;
    pac.ledc_timer_sel     = LEDC_TIMER_0;
    pac.tg_num             = TIMER_GROUP_0;
    pac.timer_num          = TIMER_0;
    pac.ringbuf_len        = /* 1024 * 8;*/ /*2560;*/ 2880;

    pwm_audio_init(&pac);
    pwm_audio_set_param(Audio_freq,LEDC_TIMER_8_BIT,1);
    pwm_audio_start();
    pwm_audio_set_volume(aud_volume);

    for (;;) {

        xQueueReceive(audioTaskQueue, &param, portMAX_DELAY);

        pwm_audio_write(ESPectrum::audioBuffer, samplesPerFrame, &written, 5 / portTICK_PERIOD_MS);

        xQueueReceive(audioTaskQueue, &param, portMAX_DELAY);

        // Finish fill of oversampled audio buffers
        if (faudbufcnt) {
            if (faudbufcnt < overSamplesPerFrame)
                for (int i=faudbufcnt; i < overSamplesPerFrame;i++) overSamplebuf[i] = faudioBit;
        }
        
        // Downsample beeper (median) and mix AY channels to output buffer
        int beeper;
        
        if (Z80Ops::is128) {

            if (faudbufcntAY < ESP_AUDIO_SAMPLES_128)
                AySound::gen_sound(ESP_AUDIO_SAMPLES_128 - faudbufcntAY , faudbufcntAY);

            if (faudbufcnt) {
                int n = 0;
                for (int i=0;i<ESP_AUDIO_OVERSAMPLES_128; i += 6) {
                    // Downsample (median)
                    beeper  =  overSamplebuf[i];
                    beeper +=  overSamplebuf[i+1];
                    beeper +=  overSamplebuf[i+2];
                    beeper +=  overSamplebuf[i+3];
                    beeper +=  overSamplebuf[i+4];
                    beeper +=  overSamplebuf[i+5];

                    beeper =  (beeper / 6) + AySound::SamplebufAY[n];
                    // if (bmax < SamplebufAY[n]) bmax = SamplebufAY[n];
                    audioBuffer[n++] = beeper > 255 ? 255 : beeper; // Clamp
                }
            } else {
                for (int i = 0; i < ESP_AUDIO_SAMPLES_128; i++) {
                    beeper = faudioBit + AySound::SamplebufAY[i];
                    audioBuffer[i] = beeper > 255 ? 255 : beeper; // Clamp
                }
            }

        } else {

            if (AY_emu) {
                if (faudbufcntAY < samplesPerFrame)
                    AySound::gen_sound(samplesPerFrame - faudbufcntAY , faudbufcntAY);
            }

            if (faudbufcnt) {
                int n = 0;
                for (int i=0;i < overSamplesPerFrame; i += 7) {
                    // Downsample (median)
                    beeper  =  overSamplebuf[i];
                    beeper +=  overSamplebuf[i+1];
                    beeper +=  overSamplebuf[i+2];
                    beeper +=  overSamplebuf[i+3];
                    beeper +=  overSamplebuf[i+4];
                    beeper +=  overSamplebuf[i+5];
                    beeper +=  overSamplebuf[i+6];

                    beeper = AY_emu ? (beeper / 7) + AySound::SamplebufAY[n] : beeper / 7;
                    // if (bmax < SamplebufAY[n]) bmax = SamplebufAY[n];
                    audioBuffer[n++] = beeper > 255 ? 255 : beeper; // Clamp

                }
            } else {
                for (int i = 0; i < samplesPerFrame; i++) {
                    beeper = AY_emu ? faudioBit + AySound::SamplebufAY[i] : faudioBit;
                    audioBuffer[i] = beeper > 255 ? 255 : beeper; // Clamp
                }
            }

        }
    }
}

IRAM_ATTR void ESPectrum::BeeperGetSample(int Audiobit) {
    // Beeper audiobuffer generation (oversample)
    uint32_t audbufpos = Z80Ops::is128 ? CPU::tstates / 19 : CPU::tstates >> 4;
    for (;audbufcnt < audbufpos; audbufcnt++) overSamplebuf[audbufcnt] = lastaudioBit;
}

IRAM_ATTR void ESPectrum::AYGetSample() {
    // AY audiobuffer generation (oversample)
    uint32_t audbufpos = CPU::tstates / (Z80Ops::is128 ? 114 : 112);
    if (audbufpos > audbufcntAY) {
        AySound::gen_sound(audbufpos - audbufcntAY, audbufcntAY);
        audbufcntAY = audbufpos;
    }
}

//=======================================================================================
// MAIN LOOP
//=======================================================================================

int ESPectrum::sync_cnt = 0;
volatile bool ESPectrum::vsync = false;

IRAM_ATTR void ESPectrum::loop() {    

int64_t ts_start, elapsed;
int64_t idle;

// int ESPmedian = 0;

for(;;) {

    ts_start = esp_timer_get_time();

    // Send audioBuffer to pwmaudio
    xQueueSend(audioTaskQueue, &param, portMAX_DELAY);
    audbufcnt = 0;
    audbufcntAY = 0;    

    CPU::loop();

    // Process audio buffer
    faudbufcnt = audbufcnt;
    faudioBit = lastaudioBit;
    faudbufcntAY = audbufcntAY;
    xQueueSend(audioTaskQueue, &param, portMAX_DELAY);

    processKeyboard();

    // Update stats every 50 frames
    if (VIDEO::framecnt == 1 && VIDEO::OSD) OSD::drawStats();

    // Flashing flag change
    if (!(VIDEO::flash_ctr++ & 0x0f)) VIDEO::flashing ^= 0x80;

    // OSD calcs
    if (VIDEO::framecnt) {
        
        totalsecondsnodelay += esp_timer_get_time() - ts_start;
        
        if (totalseconds >= 1000000) {

            if (elapsed < 100000) {
        
                // printf("Tstates: %u, RegPC: %u\n",CPU::tstates,Z80::getRegPC());

                #ifdef LOG_DEBUG_TIMING
                printf("===========================================================================\n");
                printf("[CPU] elapsed: %u; idle: %d\n", elapsed, idle);
                printf("[Audio] Volume: %d\n", aud_volume);
                printf("[Framecnt] %u; [Seconds] %f; [FPS] %f; [FPS (no delay)] %f\n", CPU::framecnt, totalseconds / 1000000, CPU::framecnt / (totalseconds / 1000000), CPU::framecnt / (totalsecondsnodelay / 1000000));
                // printf("[ESPoffset] %d\n", ESPoffset);
                showMemInfo();
                #endif

                #ifdef TESTING_CODE

                // printf("[Framecnt] %u; [Seconds] %f; [FPS] %f; [FPS (no delay)] %f\n", CPU::framecnt, totalseconds / 1000000, CPU::framecnt / (totalseconds / 1000000), CPU::framecnt / (totalsecondsnodelay / 1000000));

                // showMemInfo();
                
                snprintf(linea1, sizeof(linea1), "CPU: %05d / IDL: %05d ", (int)(elapsed), (int)(idle));
                // snprintf(linea1, sizeof(linea1), "CPU: %05d / TGT: %05d ", (int)elapsed, (int)target);
                // snprintf(linea1, sizeof(linea1), "CPU: %05d / BMX: %05d ", (int)(elapsed), bmax);
                // snprintf(linea1, sizeof(linea1), "CPU: %05d / OFF: %05d ", (int)(elapsed), (int)(ESPmedian/50));

                snprintf(linea2, sizeof(linea2), "FPS:%6.2f / FND:%6.2f ", CPU::framecnt / (totalseconds / 1000000), CPU::framecnt / (totalsecondsnodelay / 1000000));

                #else

                if (Tape::tapeStatus==TAPE_LOADING) {

                    snprintf(OSD::stats_lin1, sizeof(OSD::stats_lin1), " %-12s %04d/%04d ", Tape::tapeFileName.substr(0 + TapeNameScroller, 12).c_str(), Tape::tapeCurBlock + 1, Tape::tapeNumBlocks);

                    float percent = (float)((Tape::tapebufByteCount + Tape::tapePlayOffset) * 100) / (float)Tape::tapeFileSize;
                    snprintf(OSD::stats_lin2, sizeof(OSD::stats_lin2), " %05.2f%% %07d%s%07d ", percent, Tape::tapebufByteCount + Tape::tapePlayOffset, "/" , Tape::tapeFileSize);

                    if ((++TapeNameScroller + 12) > Tape::tapeFileName.length()) TapeNameScroller = 0;

                } else {

                    snprintf(OSD::stats_lin1, sizeof(OSD::stats_lin1), "CPU: %05d / IDL: %05d ", (int)(elapsed), (int)(idle));
                    snprintf(OSD::stats_lin2, sizeof(OSD::stats_lin2), "FPS:%6.2f / FND:%6.2f ", VIDEO::framecnt / (totalseconds / 1000000), VIDEO::framecnt / (totalsecondsnodelay / 1000000));

                }

                #endif
            }

            totalseconds = 0;
            totalsecondsnodelay = 0;
            VIDEO::framecnt = 0;

            // ESPmedian = 0;

        }
        
        elapsed = esp_timer_get_time() - ts_start;
        idle = target - elapsed - ESPoffset;

        #ifdef VIDEO_FRAME_TIMING    

        if(Config::videomode) {

            if (sync_cnt++ == 0) {
                if (idle > 0) { 
                    delayMicroseconds(idle);
                }
            } else {

                // Audio sync (once every 250 frames ~ 2,5 seconds)
                if (sync_cnt++ == 250) {
                    ESPoffset = 128 - pwm_audio_rbstats();
                    sync_cnt = 0;
                }

                // Wait for vertical sync
                for (;;) {
                    if (vsync) break;
                }

            }

        } else {

            if (idle > 0) { 
                delayMicroseconds(idle);
            }

            // Audio sync
            if (sync_cnt++ & 0x0f) {
                ESPoffset = 128 - pwm_audio_rbstats();
                sync_cnt = 0;
            }

            // ESPmedian += ESPoffset;

        }
        
        #endif

        totalseconds += esp_timer_get_time() - ts_start;

    }

}

}
