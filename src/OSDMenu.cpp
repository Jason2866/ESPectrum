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

#include <string>
#include <algorithm>
#include <sys/stat.h>
#include "errno.h"

#include "esp_vfs.h"
#include "esp_vfs_fat.h"

using namespace std;

#include "FileUtils.h"
#include "Config.h"
#include "ESPectrum.h"
#include "CPU.h"
#include "Video.h"
#include "messages.h"
#include "OSDMain.h"
#include <math.h>
#include "ZXKeyb.h"
#include "pwm_audio.h"
#include "Z80_JLS/z80.h"
#include "Tape.h"

#define MENU_MAX_ROWS 14 // Was 18 but 14 allows virtual onscreen keyboard OSD work when Config::aspect_16_9  ///////
// Line type
#define IS_TITLE 0
#define IS_FOCUSED 1
#define IS_NORMAL 2
#define IS_INFO 3
// Scroll
#define UP true
#define DOWN false

extern Font Font6x8;

static uint8_t cols;                     // Maximum columns
static uint8_t mf_rows;                  // File menu maximum rows
static unsigned short real_rows;      // Real row count
static uint8_t virtual_rows;             // Virtual maximum rows on screen
static uint16_t w;                        // Width in pixels
static uint16_t h;                        // Height in pixels
static uint16_t x;                        // X vertical position
static uint16_t y;                        // Y horizontal position
static uint16_t prev_y[5];                // Y prev. position
static unsigned short menu_prevopt = 1;
static string menu;                   // Menu string
static unsigned short begin_row = 1;      // First real displayed row
static uint8_t focus = 1;                    // Focused virtual row
static uint8_t last_focus = 0;               // To check for changes
static unsigned short last_begin_row = 0; // To check for changes

DRAM_ATTR static const uint8_t click48[12]={ 0,0x16,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x16,0 };

DRAM_ATTR static const uint8_t click128[116]= { 0x00,0x16,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,
                                                0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,
                                                0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,
                                                0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,
                                                0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,
                                                0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,
                                                0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61,
                                                0x61,0x61,0x16,0x00
                                            };

IRAM_ATTR void OSD::click() {

    size_t written;

    pwm_audio_set_volume(ESP_DEFAULT_VOLUME);

    if (Z80Ops::is48) {
        pwm_audio_write((uint8_t *) click48, 12, &written,  5 / portTICK_PERIOD_MS);
    } else {
        pwm_audio_write((uint8_t *) click128, 116, &written, 5 / portTICK_PERIOD_MS);
    }

    pwm_audio_set_volume(ESPectrum::aud_volume);

    // printf("Written: %d\n",written);

}

uint16_t OSD::zxColor(uint8_t color, uint8_t bright) {
    if (bright) color += 8;
    return spectrum_colors[color];
}

// Get real row number for a virtual one
unsigned short OSD::menuRealRowFor(uint8_t virtual_row_num) { return begin_row + virtual_row_num - 1; }

// Get real row number for a virtual one
bool OSD::menuIsSub(uint8_t virtual_row_num) { 
    string line = rowGet(menu, menuRealRowFor(virtual_row_num));
    int n = line.find(ASCII_TAB);
    if (n == line.npos) return false;
    return (line.substr(n+1).find(">") != line.npos);
}

// Menu relative AT
void OSD::menuAt(short int row, short int col) {
    if (col < 0)
        col = cols - 2 - col;
    if (row < 0)
        row = virtual_rows - 2 - row;
    VIDEO::vga.setCursor(x + 1 + (col * OSD_FONT_W), y + 1 + (row * OSD_FONT_H));
}

// Print a virtual row
void OSD::menuPrintRow(uint8_t virtual_row_num, uint8_t line_type) {
    
    uint8_t margin;

    string line = rowGet(menu, menuRealRowFor(virtual_row_num)); // This line allows proper working for menus which needs scroll like virtual onscreen keyboard ///////
    
    switch (line_type) {
    case IS_TITLE:
        VIDEO::vga.setTextColor(OSD::zxColor(7, 1), OSD::zxColor(0, 0));
        margin = 2;
        break;
    case IS_FOCUSED:
        VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(5, 1));
        margin = (real_rows > virtual_rows ? 3 : 2);
        break;
    default:
        VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(7, 1));
        margin = (real_rows > virtual_rows ? 3 : 2);
    }

    if (line.find(ASCII_TAB) != line.npos) {
        line = line.substr(0,line.find(ASCII_TAB)) + string(cols - margin - line.length(),' ') + line.substr(line.find(ASCII_TAB)+1);
    }

    menuAt(virtual_row_num, 0);

    VIDEO::vga.print(" ");

    if (line.substr(0,9) == "ESPectrum") {
        VIDEO::vga.setTextColor(ESP_ORANGE, OSD::zxColor(0, 0));
        VIDEO::vga.print("ESP");        
        VIDEO::vga.setTextColor(OSD::zxColor(7, 1), OSD::zxColor(0, 0));        
        VIDEO::vga.print(("ectrum " + Config::getArch()).c_str());
        for (uint8_t i = line.length(); i < (cols - margin); i++)
            VIDEO::vga.print(" ");
    } else {
        if (line.length() < cols - margin) {
        VIDEO::vga.print(line.c_str());
        for (uint8_t i = line.length(); i < (cols - margin); i++)
            VIDEO::vga.print(" ");
        } else {
            VIDEO::vga.print(line.substr(0, cols - margin).c_str());
        }
    }

    VIDEO::vga.print(" ");

}

// Draw the complete menu
void OSD::WindowDraw() {

    // Set font
    VIDEO::vga.setFont(Font6x8);

    if (menu_level == 0) SaveRectpos = 0;

    if (menu_saverect) {
        // Save backbuffer data
        VIDEO::SaveRect[SaveRectpos] = x;
        VIDEO::SaveRect[SaveRectpos + 1] = y;
        VIDEO::SaveRect[SaveRectpos + 2] = w;
        VIDEO::SaveRect[SaveRectpos + 3] = h;
        SaveRectpos += 4;
        for (int  m = y; m < y + h; m++) {
            uint32_t *backbuffer32 = (uint32_t *)(VIDEO::vga.backBuffer[m]);
            for (int n = x >> 2; n < ((x + w) >> 2) + 1; n++) {
                VIDEO::SaveRect[SaveRectpos] = backbuffer32[n];
                SaveRectpos++;
            }
        }
        // printf("SaveRectPos: %04X\n",SaveRectpos << 2);
    }

    // Menu border
    VIDEO::vga.rect(x, y, w, h, OSD::zxColor(0, 0));

    // Title
    PrintRow(0, IS_TITLE);

    // Rainbow
    unsigned short rb_y = y + 8;
    unsigned short rb_paint_x = x + w - 30;
    uint8_t rb_colors[] = {2, 6, 4, 5};
    for (uint8_t c = 0; c < 4; c++) {
        for (uint8_t i = 0; i < 5; i++) {
            VIDEO::vga.line(rb_paint_x + i, rb_y, rb_paint_x + 8 + i, rb_y - 8, OSD::zxColor(rb_colors[c], 1));
        }
        rb_paint_x += 5;
    }

}

// #define REPDEL 140 // As in real ZX Spectrum (700 ms.)
// #define REPPER 20 // As in real ZX Spectrum (100 ms.)
// static int zxDelay = 0;
// static int lastzxKey = 0;

// Run a new menu
unsigned short OSD::menuRun(string new_menu) {

    fabgl::VirtualKeyItem Menukey;    

    // newMenu(new_menu);

    menu = new_menu;

    // Position
    if (menu_level == 0) {
        x = (Config::aspect_16_9 ? 24 : 8);
        y = 8;
    } else {
        x = (Config::aspect_16_9 ? 24 : 8) + (60 * menu_level);
        if (menu_saverect) {
            y += (8 + (8 * menu_prevopt));
            prev_y[menu_level] = y;
        } else {
            y = prev_y[menu_level];
        }
    }

    // Rows
    real_rows = rowCount(menu);
    virtual_rows = (real_rows > MENU_MAX_ROWS ? MENU_MAX_ROWS : real_rows);
    // begin_row = last_begin_row = last_focus = focus = 1;

    // Columns
    cols = 0;
    uint8_t col_count = 0;
    for (unsigned short i = 0; i < menu.length(); i++) {
        if ((menu.at(i) == ASCII_TAB) || (menu.at(i) == ASCII_NL)) {
            if (col_count > cols) {
                cols = col_count;
            }
            while (menu.at(i) != ASCII_NL) i++;
            col_count = 0;
        }
        col_count++;
    }
    cols += 8;
    cols = (cols > 28 ? 28 : cols);

    // Size
    w = (cols * OSD_FONT_W) + 2;
    h = (virtual_rows * OSD_FONT_H) + 2;

    WindowDraw(); // Draw menu outline

    begin_row = 1;
    focus = menu_curopt;        
    last_begin_row = last_focus = 0;

    menuRedraw(); // Draw menu content

    // zxDelay = REPDEL;
    // lastzxKey = 0;

    while (1) {
        
        if (ZXKeyb::Exists) {

            ZXKbdRead();

            // ZXKeyb::process();

            // if (!bitRead(ZXKeyb::ZXcols[4], 3)) { // 6 DOWN
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_UP, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_UP, false, false);                
            //         if (lastzxKey == 1)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 1;
            //     }
            // } else
            // if (!bitRead(ZXKeyb::ZXcols[4], 4)) { // 7 UP (Yes, like the drink's name, I know... :D)
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_DOWN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_DOWN, false, false);                
            //         if (lastzxKey == 2)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 2;
            //     }
            // } else
            // if ((!bitRead(ZXKeyb::ZXcols[6], 0)) || (!bitRead(ZXKeyb::ZXcols[4], 0))) { // ENTER
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_RETURN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_RETURN, false, false);                
            //         if (lastzxKey == 3)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 3;
            //     }
            // } else
            // if ((!bitRead(ZXKeyb::ZXcols[7], 0)) || (!bitRead(ZXKeyb::ZXcols[4], 1))) { // BREAK
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_ESCAPE, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_ESCAPE, false, false);
            //         if (lastzxKey == 4)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 4;
            //     }
            // } else
            // if (!bitRead(ZXKeyb::ZXcols[3], 4)) { // LEFT
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEUP, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEUP, false, false);
            //         if (lastzxKey == 5)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 5;
            //     }
            // } else
            // if (!bitRead(ZXKeyb::ZXcols[4], 2)) { // RIGHT
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEDOWN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEDOWN, false, false);
            //         if (lastzxKey == 6)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 6;
            //     }
            // } else            
            // if (!bitRead(ZXKeyb::ZXcols[1], 1)) { // S (Capture screen)
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PRINTSCREEN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PRINTSCREEN, false, false);
            //         if (lastzxKey == 7)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 7;
            //     }
            // } else
            // {
            //     zxDelay = 0;
            //     lastzxKey = 0;
            // }

        }

        ESPectrum::readKbdJoy();

        // Process external keyboard
        if (ESPectrum::PS2Controller.keyboard()->virtualKeyAvailable()) {
            if (ESPectrum::readKbd(&Menukey)) {
                if (!Menukey.down) continue;
                if (Menukey.vk == fabgl::VK_UP) {
                    if (focus == 1 and begin_row > 1) {
                        menuScroll(DOWN);
                    } else {
                        last_focus = focus;
                        focus--;
                        if (focus < 1) {
                            focus = virtual_rows - 1;
                            last_begin_row = begin_row;
                            begin_row = real_rows - virtual_rows + 1;
                            menuRedraw();
                            menuPrintRow(focus, IS_FOCUSED);
                        }
                        else {
                            menuPrintRow(focus, IS_FOCUSED);
                            menuPrintRow(last_focus, IS_NORMAL);
                        }
                    }
                    click();
                } else if (Menukey.vk == fabgl::VK_DOWN) {
                    if (focus == virtual_rows - 1 && virtual_rows + begin_row - 1 < real_rows) {                
                        menuScroll(UP);
                    } else {
                        last_focus = focus;
                        focus++;
                        if (focus > virtual_rows - 1) {
                            focus = 1;
                            last_begin_row = begin_row;
                            begin_row = 1;
                            menuRedraw();
                            menuPrintRow(focus, IS_FOCUSED);
                        }
                        else {
                            menuPrintRow(focus, IS_FOCUSED);
                            menuPrintRow(last_focus, IS_NORMAL);                
                        }
                    }
                    click();
                } else if ((Menukey.vk == fabgl::VK_PAGEUP) || (Menukey.vk == fabgl::VK_LEFT)) {
                    if (begin_row > virtual_rows) {
                        focus = 1;
                        begin_row -= virtual_rows - 1;
                    } else {
                        focus = 1;
                        begin_row = 1;
                    }
                    menuRedraw();
                    click();
                } else if ((Menukey.vk == fabgl::VK_PAGEDOWN) || (Menukey.vk == fabgl::VK_RIGHT)) {
                    if (real_rows - begin_row  - virtual_rows > virtual_rows) {
                        focus = 1;
                        begin_row += virtual_rows - 1;
                    } else {
                        focus = virtual_rows - 1;
                        begin_row = real_rows - virtual_rows + 1;
                    }
                    menuRedraw();
                    click();
                } else if (Menukey.vk == fabgl::VK_HOME) {
                    focus = 1;
                    begin_row = 1;
                    menuRedraw();
                    click();
                } else if (Menukey.vk == fabgl::VK_END) {
                    focus = virtual_rows - 1;
                    begin_row = real_rows - virtual_rows + 1;
                    menuRedraw();
                    click();
                } else if (Menukey.vk == fabgl::VK_RETURN || Menukey.vk == fabgl::VK_SPACE) {
                    click();
                    menu_prevopt = menuRealRowFor(focus);
                    return menu_prevopt;
                } else if ((Menukey.vk == fabgl::VK_ESCAPE) || (Menukey.vk == fabgl::VK_F1)) {

                    if (menu_level!=0) {
                        // Restore backbuffer data
                        int j = SaveRectpos - (((w >> 2) + 1) * h);
                        //printf("SaveRectpos: %d; J b4 restore: %d\n",SaveRectpos, j);
                        SaveRectpos = j - 4;
                        for (int  m = y; m < y + h; m++) {
                            uint32_t *backbuffer32 = (uint32_t *)(VIDEO::vga.backBuffer[m]);
                            for (int n = x >> 2; n < ((x + w) >> 2) + 1; n++) {
                                backbuffer32[n] = VIDEO::SaveRect[j];
                                j++;
                            }
                        }
                        //printf("SaveRectpos: %d; J b4 restore: %d\n",SaveRectpos, j);
                        menu_saverect = false;                        
                    }

                    click();
                    return 0;
                }
            }
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);
        
        // if (zxDelay > 0) zxDelay--;

    }

}

// Scroll
void OSD::menuScroll(bool dir) {
    if ((dir == DOWN) && (begin_row > 1)) {
        last_begin_row = begin_row;
        begin_row--;
    } else if (dir == UP and (begin_row + virtual_rows - 1) < real_rows) {
        last_begin_row = begin_row;
        begin_row++;
    } else {
        return;
    }
    menuRedraw();
}

// Redraw inside rows
void OSD::menuRedraw() {
    if ((focus != last_focus) || (begin_row != last_begin_row)) {

        for (uint8_t row = 1; row < virtual_rows; row++) {
            if (row == focus) {
                menuPrintRow(row, IS_FOCUSED);
            } else {
                menuPrintRow(row, IS_NORMAL);
            }
        }
        menuScrollBar(begin_row);
        last_focus = focus;
        last_begin_row = begin_row;

    }

}

// Draw menu scroll bar
void OSD::menuScrollBar(unsigned short br) {

    if (real_rows > virtual_rows) {
        // Top handle
        menuAt(1, -1);
        if (br > 1) {
            VIDEO::vga.setTextColor(OSD::zxColor(7, 0), OSD::zxColor(0, 0));
            VIDEO::vga.print("+");
        } else {
            VIDEO::vga.setTextColor(OSD::zxColor(7, 0), OSD::zxColor(0, 0));
            VIDEO::vga.print("-");
        }

        // Complete bar
        unsigned short holder_x = x + (OSD_FONT_W * (cols - 1)) + 1;
        unsigned short holder_y = y + (OSD_FONT_H * 2);
        unsigned short holder_h = OSD_FONT_H * (virtual_rows - 3);
        unsigned short holder_w = OSD_FONT_W;
        VIDEO::vga.fillRect(holder_x, holder_y, holder_w, holder_h + 1, OSD::zxColor(7, 0));
        holder_y++;

        // Scroll bar
        unsigned long shown_pct = round(((float)virtual_rows / (float)real_rows) * 100.0);
        unsigned long begin_pct = round(((float)(br - 1) / (float)real_rows) * 100.0);
        unsigned long bar_h = round(((float)holder_h / 100.0) * (float)shown_pct);
        unsigned long bar_y = round(((float)holder_h / 100.0) * (float)begin_pct);
        
        while ((bar_y + bar_h) >= holder_h) {
            bar_h--;
        }

        if (bar_h == 0) bar_h = 1;

        VIDEO::vga.fillRect(holder_x + 1, holder_y + bar_y, holder_w - 2, bar_h, OSD::zxColor(0, 0));

        // Bottom handle
        menuAt(-1, -1);
        if ((br + virtual_rows - 1) < real_rows) {
            VIDEO::vga.setTextColor(OSD::zxColor(7, 0), OSD::zxColor(0, 0));
            VIDEO::vga.print("+");
        } else {
            VIDEO::vga.setTextColor(OSD::zxColor(7, 0), OSD::zxColor(0, 0));
            VIDEO::vga.print("-");
        }
    }
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}
// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

// // trim from start (copying)
// static inline std::string ltrim_copy(std::string s) {
//     ltrim(s);
//     return s;
// }

// // trim from end (copying)
// static inline std::string rtrim_copy(std::string s) {
//     rtrim(s);
//     return s;
// }

// // trim from both ends (copying)
// static inline std::string trim_copy(std::string s) {
//     trim(s);
//     return s;
// }

FILE *dirfile;

// Run a new file menu
string OSD::fileDialog(string &fdir, string title, uint8_t ftype, uint8_t mfcols, uint8_t mfrows) {

    struct stat stat_buf;
    bool reIndex;
    
    // Position
    if (menu_level == 0) {
        x = (Config::aspect_16_9 ? 24 : 4);
        y = (Config::aspect_16_9 ? 4 : 4);
    } else {
        x = (Config::aspect_16_9 ? 24 : 8) + (60 * menu_level);
        y = 8 + (16 * menu_level);
    }

    // Columns and Rows
    cols = mfcols;
    mf_rows = mfrows + (Config::aspect_16_9 ? 0 : 2);

    // printf("Focus: %d, Begin_row: %d, mf_rows: %d\n",(int) FileUtils::fileTypes[ftype].focus,(int) FileUtils::fileTypes[ftype].begin_row,(int) mf_rows);
    if (FileUtils::fileTypes[ftype].focus > mf_rows - 1) {
        FileUtils::fileTypes[ftype].begin_row += FileUtils::fileTypes[ftype].focus - (mf_rows - 1);
        FileUtils::fileTypes[ftype].focus = mf_rows - 1;
    }

    // Size
    w = (cols * OSD_FONT_W) + 2;
    h = (mf_rows * OSD_FONT_H) + 2;
    
    // menu = title + "\n" + fdir + "\n";
    menu = title + "\n" + ( fdir.length() == 1 ? fdir : fdir.substr(0,fdir.length()-1)) + "\n";
    WindowDraw(); // Draw menu outline
    fd_PrintRow(1, IS_INFO);    // Path

    // Draw blank rows
    for (uint8_t row = 2; row < mf_rows; row++) {
        VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(7, 1));
        menuAt(row, 0);
        VIDEO::vga.print(std::string(cols, ' ').c_str());
    }

    while(1) {

        reIndex = false;
        string filedir = FileUtils::MountPoint + fdir;

        // Open dir file for read
        dirfile = fopen((filedir + FileUtils::fileTypes[ftype].indexFilename).c_str(), "r");
        if (dirfile == NULL) {

            // printf("No dir file found: reindexing\n");
            reIndex = true;

        } else {

            // Read dir hash from file
            stat((filedir + FileUtils::fileTypes[ftype].indexFilename).c_str(), &stat_buf);
            fseek(dirfile, (stat_buf.st_size >> 5) << 5,SEEK_SET);
            char fhash[32];
            fgets(fhash, sizeof(fhash), dirfile);
            // printf("File Hash: %s\n",fhash);
            rewind(dirfile);

            // Calc dir hash
            DIR *dir;
            struct dirent* de;

            std::vector<std::string> filexts;
            size_t pos = 0;
            string ss = FileUtils::fileTypes[ftype].fileExts;
            while ((pos = ss.find(",")) != std::string::npos) {
                filexts.push_back(ss.substr(0, pos));
                ss.erase(0, pos + 1);
            }
            filexts.push_back(ss.substr(0));

            unsigned long hash = 0, high; // Name checksum variables

            string fdir = filedir.substr(0,filedir.length() - 1);
            if ((dir = opendir(fdir.c_str())) != nullptr) {
                
                while ((de = readdir(dir)) != nullptr) {
                    string fname = de->d_name;
                    if (de->d_type == DT_REG || de->d_type == DT_DIR) {
                        if (fname.compare(0,1,".") != 0) {
                            // printf("Fname: %s Fname size: %d\n",fname.c_str(),fname.size());
                            if ((de->d_type == DT_DIR) || ((fname.size() > 3) && (std::find(filexts.begin(),filexts.end(),fname.substr(fname.size()-4)) != filexts.end()))) {
                                // Calculate name checksum
                                for (int i = 0; i < fname.length(); i++) {
                                    hash = (hash << 4) + fname[i];
                                    if (high = hash & 0xF0000000) hash ^= high >> 24;
                                    hash &= ~high;
                                }
                            }
                        }
                    }
                }

                // printf("Hashcode : %lu\n",hash);
                
                closedir(dir);

            } else {

                printf("Error opening %s\n",filedir.c_str());
                return "";

            }

            filexts.clear(); // Clear vector
            std::vector<std::string>().swap(filexts); // free memory   

            // If calc hash and file hash are different refresh dir index
            if (stoul(fhash) != hash) {
                fclose(dirfile);
                reIndex = true;
            }

        }

        // Force reindex (for testing)
        // fclose(dirfile);
        // reIndex = true;

        // There was no index or hashes are different: reIndex
        if (reIndex) {

            // OSD::osdCenteredMsg("Please wait: sorting directory", LEVEL_INFO, 0); // TO DO: Move this into DirtoFile function
            FileUtils::DirToFile(filedir, ftype); // Prepare filelist

            stat((filedir + FileUtils::fileTypes[ftype].indexFilename).c_str(), &stat_buf);

            dirfile = fopen((filedir + FileUtils::fileTypes[ftype].indexFilename).c_str(), "r");
            if (dirfile == NULL) {
                printf("Error opening index file\n");
                return "";
            }

            // Reset position
            FileUtils::fileTypes[ftype].begin_row = FileUtils::fileTypes[ftype].focus = 2;

        }

        real_rows = (stat_buf.st_size / 64) + 2; // Add 2 for title and status bar
        virtual_rows = (real_rows > mf_rows ? mf_rows : real_rows);
        
        // printf("Real rows: %d; st_size: %d; Virtual rows: %d\n",real_rows,stat_buf.st_size,virtual_rows);

        last_begin_row = last_focus = 0;

        // printf("Focus: %d, Begin_row: %d, real_rows: %d, mf_rows: %d\n",(int) FileUtils::fileTypes[ftype].focus,(int) FileUtils::fileTypes[ftype].begin_row,(int) real_rows, (int) mf_rows);
        if ((real_rows > mf_rows) && ((FileUtils::fileTypes[ftype].begin_row + mf_rows - 2) > real_rows)) {
            FileUtils::fileTypes[ftype].focus += (FileUtils::fileTypes[ftype].begin_row + mf_rows - 2) - real_rows;
            FileUtils::fileTypes[ftype].begin_row = real_rows - (mf_rows - 2);
            // printf("Focus: %d, BeginRow: %d\n",FileUtils::fileTypes[ftype].focus,FileUtils::fileTypes[ftype].begin_row);
        }

        fd_Redraw(title, fdir, ftype); // Draw content

        fabgl::VirtualKeyItem Menukey;
        while (1) {

            if (ZXKeyb::Exists) {

                OSD::ZXKbdRead();

            }

            ESPectrum::readKbdJoy();

            // Process external keyboard
            if (ESPectrum::PS2Controller.keyboard()->virtualKeyAvailable()) {
                if (ESPectrum::readKbd(&Menukey)) {
                    if (!Menukey.down) continue;

                    // Search first ocurrence of letter if we're not on that letter yet
                    if (((Menukey.vk >= fabgl::VK_a) && (Menukey.vk <= fabgl::VK_Z)) || ((Menukey.vk >= fabgl::VK_0) && (Menukey.vk <= fabgl::VK_9))) {
                        int fsearch;
                        if (Menukey.vk<=fabgl::VK_9)
                            fsearch = Menukey.vk + 46;
                        else if (Menukey.vk<=fabgl::VK_z)
                            fsearch = Menukey.vk + 75;
                        else if (Menukey.vk<=fabgl::VK_Z)
                            fsearch = Menukey.vk + 17;
                        uint8_t letra = rowGet(menu,FileUtils::fileTypes[ftype].focus).at(0);
                        // printf("%d %d\n",(int)letra,fsearch);
                        if (letra != fsearch) { 
                            // Seek first ocurrence of letter/number
                            long prevpos = ftell(dirfile);
                            char buf[128];
                            int cnt = 0;
                            fseek(dirfile,0,SEEK_SET);
                            while(!feof(dirfile)) {
                                fgets(buf, sizeof(buf), dirfile);
                                // printf("%c %d\n",buf[0],int(buf[0]));
                                if (buf[0] == char(fsearch)) break;
                                cnt++;
                            }
                            // printf("Cnt: %d Letra: %d\n",cnt,int(letra));
                            if (!feof(dirfile)) {
                                last_begin_row = FileUtils::fileTypes[ftype].begin_row;
                                last_focus = FileUtils::fileTypes[ftype].focus;                                    
                                if (real_rows > virtual_rows) {
                                    int m = cnt + virtual_rows - real_rows;
                                    if (m > 0) {
                                        FileUtils::fileTypes[ftype].focus = m + 2;
                                        FileUtils::fileTypes[ftype].begin_row = cnt - m + 2;
                                    } else {
                                        FileUtils::fileTypes[ftype].focus = 2;
                                        FileUtils::fileTypes[ftype].begin_row = cnt + 2;
                                    }
                                } else {
                                    FileUtils::fileTypes[ftype].focus = cnt + 2;
                                    FileUtils::fileTypes[ftype].begin_row = 2;
                                }
                                // printf("Real rows: %d; Virtual rows: %d\n",real_rows,virtual_rows);
                                // printf("Focus: %d, Begin_row: %d\n",(int) FileUtils::fileTypes[ftype].focus,(int) FileUtils::fileTypes[ftype].begin_row);
                                fd_Redraw(title,fdir,ftype);
                            } else
                                fseek(dirfile,prevpos,SEEK_SET);
                        }
                    } else if (Menukey.vk == fabgl::VK_UP) {
                        if (FileUtils::fileTypes[ftype].focus == 2 && FileUtils::fileTypes[ftype].begin_row > 2) {
                            last_begin_row = FileUtils::fileTypes[ftype].begin_row;
                            FileUtils::fileTypes[ftype].begin_row--;
                            fd_Redraw(title, fdir, ftype);
                        } else if (FileUtils::fileTypes[ftype].focus > 2) {
                            last_focus = FileUtils::fileTypes[ftype].focus;
                            fd_PrintRow(FileUtils::fileTypes[ftype].focus--, IS_NORMAL);
                            fd_PrintRow(FileUtils::fileTypes[ftype].focus, IS_FOCUSED);
                            // printf("Focus: %d, Lastfocus: %d\n",FileUtils::fileTypes[ftype].focus,(int) last_focus);
                        }
                        click();
                    } else if (Menukey.vk == fabgl::VK_DOWN) {
                        if (FileUtils::fileTypes[ftype].focus == virtual_rows - 1 && FileUtils::fileTypes[ftype].begin_row + virtual_rows - 2 < real_rows) {
                            last_begin_row = FileUtils::fileTypes[ftype].begin_row;
                            FileUtils::fileTypes[ftype].begin_row++;
                            fd_Redraw(title, fdir, ftype);
                        } else if (FileUtils::fileTypes[ftype].focus < virtual_rows - 1) {
                            last_focus = FileUtils::fileTypes[ftype].focus;
                            fd_PrintRow(FileUtils::fileTypes[ftype].focus++, IS_NORMAL);
                            fd_PrintRow(FileUtils::fileTypes[ftype].focus, IS_FOCUSED);
                            // printf("Focus: %d, Lastfocus: %d\n",FileUtils::fileTypes[ftype].focus,(int) last_focus);
                        }
                        click();
                    } else if ((Menukey.vk == fabgl::VK_PAGEUP) || (Menukey.vk == fabgl::VK_LEFT)) {
                        if (FileUtils::fileTypes[ftype].begin_row > virtual_rows) {
                            FileUtils::fileTypes[ftype].focus = 2;
                            FileUtils::fileTypes[ftype].begin_row -= virtual_rows - 2;
                        } else {
                            FileUtils::fileTypes[ftype].focus = 2;
                            FileUtils::fileTypes[ftype].begin_row = 2;
                        }
                        fd_Redraw(title, fdir, ftype);
                        click();
                    } else if ((Menukey.vk == fabgl::VK_PAGEDOWN) || (Menukey.vk == fabgl::VK_RIGHT)) {
                        if (real_rows - FileUtils::fileTypes[ftype].begin_row  - virtual_rows > virtual_rows) {
                            FileUtils::fileTypes[ftype].focus = 2;
                            FileUtils::fileTypes[ftype].begin_row += virtual_rows - 2;
                        } else {
                            FileUtils::fileTypes[ftype].focus = virtual_rows - 1;
                            FileUtils::fileTypes[ftype].begin_row = real_rows - virtual_rows + 2;
                        }
                        fd_Redraw(title, fdir, ftype);
                        click();
                    } else if (Menukey.vk == fabgl::VK_HOME) {
                        last_focus = FileUtils::fileTypes[ftype].focus;
                        last_begin_row = FileUtils::fileTypes[ftype].begin_row;
                        FileUtils::fileTypes[ftype].focus = 2;
                        FileUtils::fileTypes[ftype].begin_row = 2;
                        fd_Redraw(title, fdir, ftype);
                        click();
                    } else if (Menukey.vk == fabgl::VK_END) {
                        last_focus = FileUtils::fileTypes[ftype].focus;
                        last_begin_row = FileUtils::fileTypes[ftype].begin_row;                        
                        FileUtils::fileTypes[ftype].focus = virtual_rows - 1;
                        FileUtils::fileTypes[ftype].begin_row = real_rows - virtual_rows + 2;
                        // printf("Focus: %d, Lastfocus: %d\n",FileUtils::fileTypes[ftype].focus,(int) last_focus);                        
                        fd_Redraw(title, fdir, ftype);
                        click();
                    } else if (Menukey.vk == fabgl::VK_BACKSPACE) {
                        if (fdir != "/") {

                            fclose(dirfile);
                            dirfile = NULL;

                            fdir.pop_back();
                            fdir = fdir.substr(0,fdir.find_last_of("/") + 1);

                            FileUtils::fileTypes[ftype].begin_row = FileUtils::fileTypes[ftype].focus = 2;
                            // printf("Fdir: %s\n",fdir.c_str());
                            break;

                        }                         
                    } else if (Menukey.vk == fabgl::VK_RETURN || Menukey.vk == fabgl::VK_SPACE) {

                        fclose(dirfile);
                        dirfile = NULL;

                        filedir = rowGet(menu,FileUtils::fileTypes[ftype].focus);
                        // printf("%s\n",filedir.c_str());
                        if (filedir[0] == ASCII_SPC) {
                            if (filedir[1] == ASCII_SPC) {
                                fdir.pop_back();
                                fdir = fdir.substr(0,fdir.find_last_of("/") + 1);
                            } else {
                                filedir.erase(0,1);
                                trim(filedir);
                                fdir = fdir + filedir + "/";
                            }
                            FileUtils::fileTypes[ftype].begin_row = FileUtils::fileTypes[ftype].focus = 2;
                            // printf("Fdir: %s\n",fdir.c_str());
                            break;
                        } else {

                            if (menu_saverect) {
                                // Restore backbuffer data
                                int j = SaveRectpos - (((w >> 2) + 1) * h);
                                SaveRectpos = j - 4;
                                for (int  m = y; m < y + h; m++) {
                                    uint32_t *backbuffer32 = (uint32_t *)(VIDEO::vga.backBuffer[m]);
                                    for (int n = x >> 2; n < ((x + w) >> 2) + 1; n++) {
                                        backbuffer32[n] = VIDEO::SaveRect[j];
                                        j++;
                                    }
                                }
                                menu_saverect = false;                                
                            }

                            rtrim(filedir);
                            click();
                            return (Menukey.vk == fabgl::VK_RETURN ? "R" : "S") + filedir;
                        }

                    } else if (Menukey.vk == fabgl::VK_ESCAPE) {

                        // Restore backbuffer data
                        if (menu_saverect) {
                            int j = SaveRectpos - (((w >> 2) + 1) * h);
                            SaveRectpos = j - 4;
                            for (int  m = y; m < y + h; m++) {
                                uint32_t *backbuffer32 = (uint32_t *)(VIDEO::vga.backBuffer[m]);
                                for (int n = x >> 2; n < ((x + w) >> 2) + 1; n++) {
                                    backbuffer32[n] = VIDEO::SaveRect[j];
                                    j++;
                                }
                            }
                            menu_saverect = false;
                        }

                        fclose(dirfile);
                        dirfile = NULL;
                        click();
                        return "";
                    }
                }

            } /*else {

                // TO DO: COUNT TIME TO SIGNAL START OF FOCUSED LINE SCROLL

            }*/

            // TO DO: SCROLL FOCUSED LINE IF SIGNALED

            vTaskDelay(5 / portTICK_PERIOD_MS);

        }

    }

}


void OSD::ZXKbdRead() {

    #define REPDEL 140 // As in real ZX Spectrum (700 ms.) if this function is called every 5 ms. 
    #define REPPER 20 // As in real ZX Spectrum (100 ms.) if this function is called every 5 ms. 

    static int zxDel = REPDEL;
    static int lastzxK = fabgl::VK_NONE;

    ZXKeyb::process();

    fabgl::VirtualKey injectKey = fabgl::VK_NONE;

    if (bitRead(ZXKeyb::ZXcols[7], 1)) { // Not Symbol Shift pressed ?

        if (!bitRead(ZXKeyb::ZXcols[4], 3)) injectKey = fabgl::VK_UP; // 7 -> UP
        else if (!bitRead(ZXKeyb::ZXcols[4], 4)) injectKey = fabgl::VK_DOWN; // 6 -> DOWN
        else if (!bitRead(ZXKeyb::ZXcols[6], 0)) injectKey = fabgl::VK_RETURN; // ENTER
        else if ((!bitRead(ZXKeyb::ZXcols[0], 0)) && (!bitRead(ZXKeyb::ZXcols[4], 0))) injectKey = fabgl::VK_BACKSPACE; // CS + 0 -> BACKSPACE
        else if (!bitRead(ZXKeyb::ZXcols[4], 0)) injectKey = fabgl::VK_SPACE; // 0 -> SPACE
        else if ((!bitRead(ZXKeyb::ZXcols[7], 0)) || (!bitRead(ZXKeyb::ZXcols[4], 1))) injectKey = fabgl::VK_ESCAPE; // BREAK -> ESCAPE
        else if (!bitRead(ZXKeyb::ZXcols[3], 4)) injectKey = fabgl::VK_PAGEUP; // 5 -> PGUP
        else if (!bitRead(ZXKeyb::ZXcols[4], 2)) injectKey = fabgl::VK_PAGEDOWN; // 8 -> PGDOWN
        else if (!bitRead(ZXKeyb::ZXcols[1], 1)) injectKey = fabgl::VK_PRINTSCREEN; // S -> PRINTSCREEN
        else if (!bitRead(ZXKeyb::ZXcols[5], 0)) injectKey = fabgl::VK_PAUSE; // P -> PAUSE

    } else {

        if (!bitRead(ZXKeyb::ZXcols[0], 1)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_Z : fabgl::VK_z;
        else if (!bitRead(ZXKeyb::ZXcols[0], 2)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_X : fabgl::VK_x;
        else if (!bitRead(ZXKeyb::ZXcols[0], 3)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_C : fabgl::VK_c;
        else if (!bitRead(ZXKeyb::ZXcols[0], 4)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_V : fabgl::VK_v;

        else if (!bitRead(ZXKeyb::ZXcols[1], 0)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_A : fabgl::VK_a;
        else if (!bitRead(ZXKeyb::ZXcols[1], 1)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_S : fabgl::VK_s;
        else if (!bitRead(ZXKeyb::ZXcols[1], 2)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_D : fabgl::VK_d;
        else if (!bitRead(ZXKeyb::ZXcols[1], 3)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_F : fabgl::VK_f;
        else if (!bitRead(ZXKeyb::ZXcols[1], 4)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_G : fabgl::VK_g;

        else if (!bitRead(ZXKeyb::ZXcols[2], 0)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_Q : fabgl::VK_q;
        else if (!bitRead(ZXKeyb::ZXcols[2], 1)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_W : fabgl::VK_w;
        else if (!bitRead(ZXKeyb::ZXcols[2], 2)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_E : fabgl::VK_e;
        else if (!bitRead(ZXKeyb::ZXcols[2], 3)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_R : fabgl::VK_r;
        else if (!bitRead(ZXKeyb::ZXcols[2], 4)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_T : fabgl::VK_t;

        else if (!bitRead(ZXKeyb::ZXcols[5], 0)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_P : fabgl::VK_p;
        else if (!bitRead(ZXKeyb::ZXcols[5], 1)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_O : fabgl::VK_o;
        else if (!bitRead(ZXKeyb::ZXcols[5], 2)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_I : fabgl::VK_i;
        else if (!bitRead(ZXKeyb::ZXcols[5], 3)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_U : fabgl::VK_u;
        else if (!bitRead(ZXKeyb::ZXcols[5], 4)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_Y : fabgl::VK_y;

        else if (!bitRead(ZXKeyb::ZXcols[6], 1)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_L : fabgl::VK_l;
        else if (!bitRead(ZXKeyb::ZXcols[6], 2)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_K : fabgl::VK_k;
        else if (!bitRead(ZXKeyb::ZXcols[6], 3)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_J : fabgl::VK_j;
        else if (!bitRead(ZXKeyb::ZXcols[6], 4)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_H : fabgl::VK_h;

        else if (!bitRead(ZXKeyb::ZXcols[7], 2)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_M : fabgl::VK_m;
        else if (!bitRead(ZXKeyb::ZXcols[7], 3)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_N : fabgl::VK_n;
        else if (!bitRead(ZXKeyb::ZXcols[7], 4)) injectKey = !bitRead(ZXKeyb::ZXcols[0], 0) ? fabgl::VK_B : fabgl::VK_b;

        else if (!bitRead(ZXKeyb::ZXcols[3], 0)) injectKey = fabgl::VK_1;
        else if (!bitRead(ZXKeyb::ZXcols[3], 1)) injectKey = fabgl::VK_2;
        else if (!bitRead(ZXKeyb::ZXcols[3], 2)) injectKey = fabgl::VK_3;
        else if (!bitRead(ZXKeyb::ZXcols[3], 3)) injectKey = fabgl::VK_4;
        else if (!bitRead(ZXKeyb::ZXcols[3], 4)) injectKey = fabgl::VK_5;                        

        else if (!bitRead(ZXKeyb::ZXcols[4], 0)) injectKey = fabgl::VK_0;
        else if (!bitRead(ZXKeyb::ZXcols[4], 1)) injectKey = fabgl::VK_9;
        else if (!bitRead(ZXKeyb::ZXcols[4], 2)) injectKey = fabgl::VK_8;
        else if (!bitRead(ZXKeyb::ZXcols[4], 3)) injectKey = fabgl::VK_7;
        else if (!bitRead(ZXKeyb::ZXcols[4], 4)) injectKey = fabgl::VK_6;

    }

    if (injectKey != fabgl::VK_NONE) {
        if (zxDel == 0) {
            ESPectrum::PS2Controller.keyboard()->injectVirtualKey(injectKey, true, false);
            ESPectrum::PS2Controller.keyboard()->injectVirtualKey(injectKey, false, false);
            zxDel = lastzxK == injectKey ? REPPER : REPDEL;
            lastzxK = injectKey;
        }
    } else {
        zxDel = 0;
        lastzxK = fabgl::VK_NONE;
    }

    if (zxDel > 0) zxDel--;

}

// Print a virtual row
void OSD::PrintRow(uint8_t virtual_row_num, uint8_t line_type) {
    
    uint8_t margin;

    string line = rowGet(menu, virtual_row_num);
    
    switch (line_type) {
    case IS_TITLE:
        VIDEO::vga.setTextColor(OSD::zxColor(7, 1), OSD::zxColor(0, 0));
        margin = 2;
        break;
    case IS_FOCUSED:
        VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(5, 1));
        margin = (real_rows > virtual_rows ? 3 : 2);
        break;
    default:
        VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(7, 1));
        margin = (real_rows > virtual_rows ? 3 : 2);
    }

    if (line.find(ASCII_TAB) != line.npos) {
        line = line.substr(0,line.find(ASCII_TAB)) + string(cols - margin - line.length(),' ') + line.substr(line.find(ASCII_TAB)+1);
    }

    menuAt(virtual_row_num, 0);

    VIDEO::vga.print(" ");

    if ((virtual_row_num == 0) && (line.substr(0,9) == "ESPectrum")) {
        VIDEO::vga.setTextColor(ESP_ORANGE, OSD::zxColor(0, 0));
        VIDEO::vga.print("ESP");        
        VIDEO::vga.setTextColor(OSD::zxColor(7, 1), OSD::zxColor(0, 0));        
        VIDEO::vga.print(("ectrum " + Config::getArch()).c_str());
        for (uint8_t i = line.length(); i < (cols - margin); i++)
            VIDEO::vga.print(" ");
    } else {
        if (line.length() < cols - margin) {
        VIDEO::vga.print(line.c_str());
        for (uint8_t i = line.length(); i < (cols - margin); i++)
            VIDEO::vga.print(" ");
        } else {
            VIDEO::vga.print(line.substr(0, cols - margin).c_str());
        }
    }

    VIDEO::vga.print(" ");

}

// Print a virtual row
void OSD::fd_PrintRow(uint8_t virtual_row_num, uint8_t line_type) {
    
    uint8_t margin;

    string line = rowGet(menu, virtual_row_num);
    
    switch (line_type) {
    case IS_TITLE:
        VIDEO::vga.setTextColor(OSD::zxColor(7, 1), OSD::zxColor(0, 0));
        margin = 2;
        break;
    case IS_INFO:
        VIDEO::vga.setTextColor(OSD::zxColor(7, 1), OSD::zxColor(5, 0));
        margin = (real_rows > virtual_rows ? 3 : 2);
        break;
    case IS_FOCUSED:
        VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(5, 1));
        margin = (real_rows > virtual_rows ? 3 : 2);
        break;
    default:
        VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(7, 1));
        margin = (real_rows > virtual_rows ? 3 : 2);
    }

    menuAt(virtual_row_num, 0);

    VIDEO::vga.print(" ");

    if (line[0] == ASCII_SPC) {
        // Directory
        ltrim(line);
        if (line.length() < cols - margin)
            line = line + std::string(cols - margin - line.length(), ' ');
        line = line.substr(0,cols - margin - 6) + " <DIR>";
    } else {
        if (line.length() < cols - margin) {
            line = line + std::string(cols - margin - line.length(), ' ');
            line = line.substr(0, cols - margin);
        } else {
            if (line_type == IS_INFO) {
                // printf("%s %d\n",line.c_str(),line.length() - (cols - margin));
                line = ".." + line.substr(line.length() - (cols - margin) + 2);
                // printf("%s\n",line.c_str());                
            } else
                line = line.substr(0, cols - margin);
        }

    }
    
    VIDEO::vga.print(line.c_str());

    VIDEO::vga.print(" ");

}

// Redraw inside rows
void OSD::fd_Redraw(string title, string fdir, uint8_t ftype) {
    
    if ((FileUtils::fileTypes[ftype].focus != last_focus) || (FileUtils::fileTypes[ftype].begin_row != last_begin_row)) {

        // printf("fd_Redraw\n");

        // Read bunch of rows
        fseek(dirfile, (FileUtils::fileTypes[ftype].begin_row - 2) * 64,SEEK_SET);
        menu = title + "\n" + ( fdir.length() == 1 ? fdir : fdir.substr(0,fdir.length()-1)) + "\n";
        for (int i = 2; i < virtual_rows; i++) {
            char buf[128];
            fgets(buf, sizeof(buf), dirfile);
            if (feof(dirfile)) break;
            menu += buf;
        }

        fd_PrintRow(1, IS_INFO); // Print status bar
        
        uint8_t row = 2;
        for (; row < virtual_rows; row++) {
            if (row == FileUtils::fileTypes[ftype].focus) {
                fd_PrintRow(row, IS_FOCUSED);
            } else {
                fd_PrintRow(row, IS_NORMAL);
            }
        }

        if (real_rows > virtual_rows) {        
            menuScrollBar(FileUtils::fileTypes[ftype].begin_row);
        } else {
            for (; row < mf_rows; row++) {
                VIDEO::vga.setTextColor(OSD::zxColor(0, 1), OSD::zxColor(7, 1));
                menuAt(row, 0);
                VIDEO::vga.print(std::string(cols, ' ').c_str());
            }
        }
        
        last_focus = FileUtils::fileTypes[ftype].focus;
        last_begin_row = FileUtils::fileTypes[ftype].begin_row;
    }

}

string tapeBlockReadData(int Blocknum) {

    int tapeContentIndex=0;
    int tapeBlkLen=0;
    string blktype;
    char buf[48];
    char fname[10];

    tapeContentIndex = Tape::CalcTapBlockPos(Blocknum);

    // Analyze .tap file
    tapeBlkLen=(readByteFile(Tape::tape) | (readByteFile(Tape::tape) << 8));

    // Read the flag byte from the block.
    // If the last block is a fragmented data block, there is no flag byte, so set the flag to 255
    // to indicate a data block.
    uint8_t flagByte;
    if (tapeContentIndex + 2 < Tape::tapeFileSize) {
        flagByte = readByteFile(Tape::tape);
    } else {
        flagByte = 255;
    }

    // Process the block depending on if it is a header or a data block.
    // Block type 0 should be a header block, but it happens that headerless blocks also
    // have block type 0, so we need to check the block length as well.
    if (flagByte == 0 && tapeBlkLen == 19) { // This is a header.

        // Get the block type.
        uint8_t blocktype = readByteFile(Tape::tape);

        switch (blocktype) {
        case 0: 
            blktype = "Program      ";
            break;
        case 1: 
            blktype = "Number array ";
            break;
        case 2: 
            blktype = "Char array   ";
            break;
        case 3: 
            blktype = "Code         ";
            break;
        case 4: 
            blktype = "Data block   ";
            break;
        case 5: 
            blktype = "Info         ";
            break;
        case 6: 
            blktype = "Unassigned   ";
            break;
        default:
            blktype = "Unassigned   ";
            break;
        }

        // Get the filename.
        if (blocktype > 5) {
            fname[0] = '\0';
        } else {
            for (int i = 0; i < 10; i++) {
                fname[i] = readByteFile(Tape::tape);
            }
            fname[10]='\0';
        }

    } else {

        blktype = "Data block   ";
        fname[0]='\0';

    }

    snprintf(buf, sizeof(buf), "%04d %s %10s % 6d\n", Blocknum + 1, blktype.c_str(), fname, tapeBlkLen);

    return buf;

}

// Redraw inside rows
void OSD::tapemenuRedraw(string title) {

    if ((focus != last_focus) || (begin_row != last_begin_row)) {

        // Read bunch of rows
        menu = title + "\n";
        for (int i = begin_row - 1; i < virtual_rows + begin_row - 2; i++) {
            if (i > Tape::tapeNumBlocks) break;
            menu += tapeBlockReadData(i);
        }

        for (uint8_t row = 1; row < virtual_rows; row++) {
            if (row == focus) {
                PrintRow(row, IS_FOCUSED);
            } else {
                PrintRow(row, IS_NORMAL);
            }
        }
        
        menuScrollBar(begin_row);
        
        last_focus = focus;
        last_begin_row = begin_row;

    }
}

// Tape Browser Menu
int OSD::menuTape(string title) {

    fabgl::VirtualKeyItem Menukey;

    uint32_t tapeBckPos = ftell(Tape::tape);

    // Tape::TapeListing.erase(Tape::TapeListing.begin(),Tape::TapeListing.begin() + 2);

    real_rows = Tape::tapeNumBlocks + 1;
    virtual_rows = (real_rows > 19 ? 19 : real_rows);
    // begin_row = last_begin_row = last_focus = focus = 1;
    
    if (Tape::tapeCurBlock > 17) {
        begin_row = Tape::tapeCurBlock - 16;
        focus = 18;
    } else{
        begin_row = 1;    
        focus = Tape::tapeCurBlock + 1;
    }
    // last_focus = focus;
    // last_begin_row = begin_row;
    menu_curopt = focus;

    // // Get first bunch of rows
    // menu = title + "\n";
    // for (int i = (begin_row - 1); i < (begin_row - 1) + (virtual_rows - 1); i++) {
    //     if (i > Tape::tapeNumBlocks) break;
    //     menu += tapeBlockReadData(i);
    // }

    // printf(menu.c_str());

    // Position
    if (menu_level == 0) {
        x = (Config::aspect_16_9 ? 24 : 8);
        y = 8;
    } else {
        x = (Config::aspect_16_9 ? 24 : 8) + (60 * menu_level);
        y = 8 + (16 * menu_level);
    }

    // Columns
    cols = 39; // 36 for block info + 2 pre and post space + 1 for scrollbar

    // Size
    w = (cols * OSD_FONT_W) + 2;
    h = (virtual_rows * OSD_FONT_H) + 2;

    menu = title + "\n";
    WindowDraw();

    last_begin_row = last_focus = 0;

    tapemenuRedraw(title);

    // zxDelay = REPDEL;
    // lastzxKey = 0;

    while (1) {

        if (ZXKeyb::Exists) {

            ZXKbdRead();

            // ZXKeyb::process();

            // if (!bitRead(ZXKeyb::ZXcols[4], 3)) { // 6 DOWN
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_UP, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_UP, false, false);                
            //         if (lastzxKey == 1)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 1;
            //     }
            // } else
            // if (!bitRead(ZXKeyb::ZXcols[4], 4)) { // 7 UP (Yes, like the drink's name, I know... :D)
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_DOWN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_DOWN, false, false);                
            //         if (lastzxKey == 2)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 2;
            //     }
            // } else
            // if ((!bitRead(ZXKeyb::ZXcols[6], 0)) || (!bitRead(ZXKeyb::ZXcols[4], 0))) { // ENTER
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_RETURN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_RETURN, false, false);                
            //         if (lastzxKey == 3)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 3;
            //     }
            // } else
            // if ((!bitRead(ZXKeyb::ZXcols[7], 0)) || (!bitRead(ZXKeyb::ZXcols[4], 1))) { // BREAK        
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_ESCAPE, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_ESCAPE, false, false);                
            //         if (lastzxKey == 4)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 4;
            //     }
            // } else
            // if (!bitRead(ZXKeyb::ZXcols[3], 4)) { // LEFT
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEUP, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEUP, false, false);
            //         if (lastzxKey == 5)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 5;
            //     }
            // } else
            // if (!bitRead(ZXKeyb::ZXcols[4], 2)) { // RIGHT
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEDOWN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PAGEDOWN, false, false);                
            //         if (lastzxKey == 6)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 6;
            //     }
            // } else
            // if (!bitRead(ZXKeyb::ZXcols[1], 1)) { // S (Capture screen)
            //     if (zxDelay == 0) {
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PRINTSCREEN, true, false);
            //         ESPectrum::PS2Controller.keyboard()->injectVirtualKey(fabgl::VK_PRINTSCREEN, false, false);
            //         if (lastzxKey == 7)
            //             zxDelay = REPPER;
            //         else
            //             zxDelay = REPDEL;
            //         lastzxKey = 7;
            //     }
            // } else
            // {
            //     zxDelay = 0;
            //     lastzxKey = 0;
            // }

        }

        ESPectrum::readKbdJoy();

        // Process external keyboard
        if (ESPectrum::PS2Controller.keyboard()->virtualKeyAvailable()) {
            if (ESPectrum::readKbd(&Menukey)) {

                if (!Menukey.down) continue;

                if (Menukey.vk == fabgl::VK_UP) {
                    if (focus == 1 and begin_row > 1) {
                        if (begin_row > 1) {
                            last_begin_row = begin_row;
                            begin_row--;
                        }
                        tapemenuRedraw(title);
                        click();                        
                    } else if (focus > 1) {
                        last_focus = focus;
                        focus--;
                        PrintRow(focus, IS_FOCUSED);
                        PrintRow(focus + 1, IS_NORMAL);
                        click();
                    }
                } else if (Menukey.vk == fabgl::VK_DOWN) {
                    if (focus == virtual_rows - 1) {
                        if ((begin_row + virtual_rows - 1) < real_rows) {
                            last_begin_row = begin_row;
                            begin_row++;
                            tapemenuRedraw(title);
                            click();
                        }
                    } else if (focus < virtual_rows - 1) {
                        last_focus = focus;
                        focus++;
                        PrintRow(focus, IS_FOCUSED);
                        PrintRow(focus - 1, IS_NORMAL);
                        click();
                    }
                } else if ((Menukey.vk == fabgl::VK_PAGEUP) || (Menukey.vk == fabgl::VK_LEFT)) {
                    // printf("%u\n",begin_row);
                    if (begin_row > virtual_rows) {
                        last_focus = focus;
                        last_begin_row = begin_row;                    
                        focus = 1;
                        begin_row -= virtual_rows - 1;
                        tapemenuRedraw(title);
                        click();
                    } else {
                        last_focus = focus;
                        last_begin_row = begin_row;                    
                        focus = 1;
                        begin_row = 1;
                        tapemenuRedraw(title);
                        click();
                    }
                } else if ((Menukey.vk == fabgl::VK_PAGEDOWN) || (Menukey.vk == fabgl::VK_RIGHT)) {
                    if (real_rows - begin_row  - virtual_rows > virtual_rows) {
                        last_focus = focus;
                        last_begin_row = begin_row;                    
                        focus = 1;
                        begin_row += virtual_rows - 1;
                        tapemenuRedraw(title);
                        click();
                    } else {
                        last_focus = focus;
                        last_begin_row = begin_row;                    
                        focus = virtual_rows - 1;
                        begin_row = real_rows - virtual_rows + 1;
                        tapemenuRedraw(title);
                        click();
                    }
                } else if (Menukey.vk == fabgl::VK_HOME) {
                    last_focus = focus;
                    last_begin_row = begin_row;                    
                    focus = 1;
                    begin_row = 1;
                    tapemenuRedraw(title);
                    click();
                } else if (Menukey.vk == fabgl::VK_END) {
                    last_focus = focus;
                    last_begin_row = begin_row;                    
                    focus = virtual_rows - 1;
                    begin_row = real_rows - virtual_rows + 1;
                    tapemenuRedraw(title);
                    click();
                } else if (Menukey.vk == fabgl::VK_RETURN || Menukey.vk == fabgl::VK_SPACE) {
                    click();
                    Tape::CalcTapBlockPos(begin_row + focus - 2);
                    // printf("Ret value: %d\n", begin_row + focus - 2);
                    return (begin_row + focus - 2);
                } else if (Menukey.vk == fabgl::VK_ESCAPE) {

                    // if (Tape::tapeStatus==TAPE_LOADING) {
                        fseek(Tape::tape, tapeBckPos, SEEK_SET);
                    // }

                    if (menu_level!=0) {
                        // Restore backbuffer data
                        int j = SaveRectpos - (((w >> 2) + 1) * h);
                        SaveRectpos = j - 4;
                        for (int  m = y; m < y + h; m++) {
                            uint32_t *backbuffer32 = (uint32_t *)(VIDEO::vga.backBuffer[m]);
                            for (int n = x >> 2; n < ((x + w) >> 2) + 1; n++) {
                                backbuffer32[n] = VIDEO::SaveRect[j];
                                j++;
                            }
                        }
                        menu_saverect = false;
                    }

                    click();
                    return -1;
                }
            }
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);    

        // if (zxDelay > 0) zxDelay--;

    }
}

