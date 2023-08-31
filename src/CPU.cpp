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

#include "CPU.h"
#include "ESPectrum.h"
#include "MemESP.h"
#include "Ports.h"
#include "hardconfig.h"
#include "Config.h"
#include "Video.h"
#include "Z80_JLS/z80.h"

#pragma GCC optimize ("O3")

static bool createCalled = false;

uint32_t CPU::microsPerFrame()
{
    if (Config::getArch() == "48K") return 19968;
    else                            return 19992;
}

uint32_t CPU::tstates = 0;
uint64_t CPU::global_tstates = 0;
uint32_t CPU::statesInFrame = 0;
uint32_t CPU::framecnt = 0;
uint8_t CPU::latetiming = 0;
uint8_t CPU::IntStart = 0;
uint8_t CPU::IntEnd = 0;

bool Z80Ops::is48 = true;

void CPU::setup()
{
    if (!createCalled) {
        Z80::create();
        createCalled = true;
    }
    
    CPU::latetiming = Config::AluTiming;    

    if (Config::getArch() == "48K") {
        VIDEO::getFloatBusData = &VIDEO::getFloatBusData48;
        Z80Ops::is48 = true;
        statesInFrame = TSTATES_PER_FRAME_48;
        CPU::IntStart = INT_START48;
        CPU::IntEnd = INT_END48 + CPU::latetiming;
    } else {
        VIDEO::getFloatBusData = &VIDEO::getFloatBusData128;
        Z80Ops::is48 = false;
        statesInFrame = TSTATES_PER_FRAME_128;
        CPU::IntStart = INT_START128;
        CPU::IntEnd = INT_END128 + CPU::latetiming;
    }
    
    tstates = 0;
    global_tstates = 0;

}

///////////////////////////////////////////////////////////////////////////////

void CPU::reset() {

    Z80::reset();
    
    CPU::latetiming = Config::AluTiming;

    if (Config::getArch() == "48K") {
        VIDEO::getFloatBusData = &VIDEO::getFloatBusData48;
        Z80Ops::is48 = true;
        statesInFrame = TSTATES_PER_FRAME_48;
        CPU::IntStart = INT_START48;
        CPU::IntEnd = INT_END48 + CPU::latetiming;
    } else {
        VIDEO::getFloatBusData = &VIDEO::getFloatBusData128;
        Z80Ops::is48 = false;
        statesInFrame = TSTATES_PER_FRAME_128;
        CPU::IntStart = INT_START128;
        CPU::IntEnd = INT_END128 + CPU::latetiming;
    }

    tstates = 0;
    global_tstates = 0;

}

///////////////////////////////////////////////////////////////////////////////

void IRAM_ATTR CPU::loop()
{

    while (tstates < IntEnd) Z80::execute();
    
    uint32_t stFrame = statesInFrame - IntEnd;
    while (tstates < stFrame) Z80::exec_nocheck();

    while (tstates < statesInFrame) Z80::execute();

    if (tstates & 0xFF000000) FlushOnHalt(); // If we're halted flush screen and update registers as needed

    global_tstates += statesInFrame; // increase global Tstates
    tstates -= statesInFrame;

    framecnt++;

}

void CPU::FlushOnHalt() {
        
    tstates &= 0x00FFFFFF;

    uint8_t page = Z80::getRegPC() >> 14;
    if (MemESP::ramContended[page]) {

        uint32_t stFrame = statesInFrame - latetiming;
        while (tstates < stFrame ) {
            VIDEO::Draw(4,true);
            Z80::incRegR(1);
        }

    } else {

        uint32_t pre_tstates = tstates;
        VIDEO::Flush(); // Draw the rest of the frame
        tstates = pre_tstates;
        
        pre_tstates += latetiming;
        uint32_t incr = (statesInFrame - pre_tstates) >> 2;
        if (pre_tstates & 0x03) incr++;
        tstates += (incr << 2);
        Z80::incRegR(incr & 0x000000FF);

    }

    Z80::checkINT();        

}

///////////////////////////////////////////////////////////////////////////////
// Z80Ops
///////////////////////////////////////////////////////////////////////////////

// Read byte from RAM
uint8_t IRAM_ATTR Z80Ops::peek8(uint16_t address) {
    uint8_t page = address >> 14;
    VIDEO::Draw(3,MemESP::ramContended[page]);
    return MemESP::ramCurrent[page][address & 0x3fff];
}

// Write byte to RAM
void IRAM_ATTR Z80Ops::poke8(uint16_t address, uint8_t value) {
    uint8_t page = address >> 14;
    VIDEO::Draw(3, MemESP::ramContended[page]);
    if (page != 0) MemESP::ramCurrent[page][address & 0x3fff] = value;
}

// Read word from RAM
uint16_t IRAM_ATTR Z80Ops::peek16(uint16_t address) {

    uint8_t page = address >> 14;

    if (page == ((address + 1) >> 14)) {    // Check if address is between two different pages

        if (MemESP::ramContended[page]) {
            VIDEO::Draw(3, true);
            VIDEO::Draw(3, true);            
        } else
            VIDEO::Draw(6, false);

        return ((MemESP::ramCurrent[page][(address & 0x3fff) + 1] << 8) | MemESP::ramCurrent[page][address & 0x3fff]);

    } else {

        // Order matters, first read lsb, then read msb, don't "optimize"
        uint8_t lsb = Z80Ops::peek8(address);
        uint8_t msb = Z80Ops::peek8(address + 1);
        return (msb << 8) | lsb;

    }

}

// Write word to RAM
void IRAM_ATTR Z80Ops::poke16(uint16_t address, RegisterPair word) {

    uint8_t page = address >> 14;

    if (page == ((address + 1) >> 14)) {    // Check if address is between two different pages

        if (MemESP::ramContended[page]) {
            VIDEO::Draw(3, true);
            VIDEO::Draw(3, true);            
        } else
            VIDEO::Draw(6, false);

        if (page != 0) {
            MemESP::ramCurrent[page][address & 0x3fff] = word.byte8.lo;
            MemESP::ramCurrent[page][(address & 0x3fff) + 1] = word.byte8.hi;
        }

    } else {

        // Order matters, first write lsb, then write msb, don't "optimize"
        Z80Ops::poke8(address, word.byte8.lo);
        Z80Ops::poke8(address + 1, word.byte8.hi);

    }

}

/* Put an address on bus lasting 'tstates' cycles */
void IRAM_ATTR Z80Ops::addressOnBus(uint16_t address, int32_t wstates) {
    if (MemESP::ramContended[address >> 14]) {
        for (int idx = 0; idx < wstates; idx++)
            VIDEO::Draw(1, true);
    } else
        VIDEO::Draw(wstates, false);
}

/* Callback to know when the INT signal is active */
bool IRAM_ATTR Z80Ops::isActiveINT(void) {
    int tmp = CPU::tstates + CPU::latetiming;
    if (tmp >= CPU::statesInFrame) tmp -= CPU::statesInFrame;
    return ((tmp >= CPU::IntStart) && (tmp < CPU::IntEnd));
}

