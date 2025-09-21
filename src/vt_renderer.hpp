#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace VT {
    struct Cell {
        char32_t ch = U' ';
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool reverse = false;
        uint32_t fg_color = 0xCCCCCC; // Default white-ish
        uint32_t bg_color = 0x000000; // Default black
        bool has_fg_color = false;
        bool has_bg_color = false;
    };

    class Renderer {
    private:
        std::vector<std::vector<Cell>> grid;
        int width, height;
        int cursor_x, cursor_y;
        Cell current_style;
        
        // Helper functions
        void ensureValidCursor();
        void parseCSI(const std::string& sequence);
        void parseSGR(const std::vector<int>& params);
        uint32_t ansi256ToRgb(int color);
        std::string rgbToHex(uint32_t rgb);
        std::string utf8FromCodepoint(char32_t cp);
        
    public:
        Renderer(int w = 120, int h = 30);
        void resize(int w, int h);
        void clear();
        void processSequence(const std::string& ansi_text);
        std::string renderToResoniteHTML();
        
        // Getters
        int getWidth() const { return width; }
        int getHeight() const { return height; }
        int getCursorX() const { return cursor_x; }
        int getCursorY() const { return cursor_y; }
    };
}
