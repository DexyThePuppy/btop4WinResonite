#include "vt_renderer.hpp"
#include <sstream>
#include <regex>
#include <algorithm>
#include <iomanip>

namespace VT {

Renderer::Renderer(int w, int h) : width(w), height(h), cursor_x(0), cursor_y(0) {
    grid.resize(height);
    for (auto& row : grid) {
        row.resize(width);
    }
    
    // Set default style
    current_style.ch = U' ';
    current_style.bold = false;
    current_style.italic = false;
    current_style.underline = false;
    current_style.reverse = false;
    current_style.fg_color = 0xCCCCCC;
    current_style.bg_color = 0x000000;
    current_style.has_fg_color = false;
    current_style.has_bg_color = false;
}

void Renderer::resize(int w, int h) {
    width = w;
    height = h;
    grid.clear();
    grid.resize(height);
    for (auto& row : grid) {
        row.resize(width);
    }
    cursor_x = cursor_y = 0;
}

void Renderer::clear() {
    Cell empty_cell;
    empty_cell.ch = U' ';
    empty_cell.bold = false;
    empty_cell.italic = false;
    empty_cell.underline = false;
    empty_cell.reverse = false;
    empty_cell.fg_color = 0xCCCCCC;
    empty_cell.bg_color = 0x000000;
    empty_cell.has_fg_color = false;
    empty_cell.has_bg_color = false;
    
    for (auto& row : grid) {
        std::fill(row.begin(), row.end(), empty_cell);
    }
    cursor_x = cursor_y = 0;
}

void Renderer::ensureValidCursor() {
    cursor_x = std::max(0, std::min(cursor_x, width - 1));
    cursor_y = std::max(0, std::min(cursor_y, height - 1));
}

uint32_t Renderer::ansi256ToRgb(int color) {
    // Standard 16 colors
    if (color < 16) {
        static const uint32_t colors16[] = {
            0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
            0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF
        };
        return colors16[color];
    }
    
    // 216 color cube (16-231)
    if (color >= 16 && color <= 231) {
        int c = color - 16;
        int r = c / 36;
        int g = (c % 36) / 6;
        int b = c % 6;
        
        auto component = [](int val) -> int {
            if (val == 0) return 0;
            return 55 + val * 40;
        };
        
        return (component(r) << 16) | (component(g) << 8) | component(b);
    }
    
    // 24 grayscale colors (232-255)
    if (color >= 232 && color <= 255) {
        int gray = 8 + (color - 232) * 10;
        return (gray << 16) | (gray << 8) | gray;
    }
    
    return 0xCCCCCC; // Default
}

std::string Renderer::rgbToHex(uint32_t rgb) {
    std::stringstream ss;
    ss << "#" << std::hex << std::setfill('0') << std::setw(6) << (rgb & 0xFFFFFF);
    return ss.str();
}

std::string Renderer::utf8FromCodepoint(char32_t cp) {
    std::string result;
    if (cp <= 0x7F) {
        result += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return result;
}

void Renderer::parseSGR(const std::vector<int>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        int code = params[i];
        
        switch (code) {
            case 0: // Reset
                current_style.bold = false;
                current_style.italic = false;
                current_style.underline = false;
                current_style.reverse = false;
                current_style.has_fg_color = false;
                current_style.has_bg_color = false;
                break;
            case 1: // Bold
                current_style.bold = true;
                break;
            case 3: // Italic
                current_style.italic = true;
                break;
            case 4: // Underline
                current_style.underline = true;
                break;
            case 7: // Reverse
                current_style.reverse = true;
                break;
            case 22: // Bold off
                current_style.bold = false;
                break;
            case 23: // Italic off
                current_style.italic = false;
                break;
            case 24: // Underline off
                current_style.underline = false;
                break;
            case 27: // Reverse off
                current_style.reverse = false;
                break;
            case 39: // Default foreground
                current_style.has_fg_color = false;
                break;
            case 49: // Default background
                current_style.has_bg_color = false;
                break;
            case 38: // Extended foreground color
                if (i + 1 < params.size()) {
                    if (params[i + 1] == 2 && i + 4 < params.size()) { // 24-bit RGB
                        current_style.fg_color = (params[i + 2] << 16) | (params[i + 3] << 8) | params[i + 4];
                        current_style.has_fg_color = true;
                        i += 4;
                    } else if (params[i + 1] == 5 && i + 2 < params.size()) { // 256-color
                        current_style.fg_color = ansi256ToRgb(params[i + 2]);
                        current_style.has_fg_color = true;
                        i += 2;
                    }
                }
                break;
            case 48: // Extended background color
                if (i + 1 < params.size()) {
                    if (params[i + 1] == 2 && i + 4 < params.size()) { // 24-bit RGB
                        current_style.bg_color = (params[i + 2] << 16) | (params[i + 3] << 8) | params[i + 4];
                        current_style.has_bg_color = true;
                        i += 4;
                    } else if (params[i + 1] == 5 && i + 2 < params.size()) { // 256-color
                        current_style.bg_color = ansi256ToRgb(params[i + 2]);
                        current_style.has_bg_color = true;
                        i += 2;
                    }
                }
                break;
        }
    }
}

void Renderer::parseCSI(const std::string& sequence) {
    if (sequence.empty()) return;
    
    char final_byte = sequence.back();
    std::string params = sequence.substr(0, sequence.length() - 1);
    
    // Parse parameters
    std::vector<int> nums;
    std::stringstream ss(params);
    std::string token;
    
    while (std::getline(ss, token, ';')) {
        if (!token.empty()) {
            try {
                nums.push_back(std::stoi(token));
            } catch (...) {
                nums.push_back(0);
            }
        } else {
            nums.push_back(1); // Default parameter
        }
    }
    
    if (nums.empty()) {
        nums.push_back(1); // Default parameter
    }
    
    switch (final_byte) {
        case 'H': // Cursor Position
        case 'f': {
            int row = nums.size() > 0 ? nums[0] : 1;
            int col = nums.size() > 1 ? nums[1] : 1;
            cursor_y = row - 1; // Convert to 0-based
            cursor_x = col - 1;
            ensureValidCursor();
            break;
        }
        case 'A': // Cursor Up
            cursor_y -= nums[0];
            ensureValidCursor();
            break;
        case 'B': // Cursor Down
            cursor_y += nums[0];
            ensureValidCursor();
            break;
        case 'C': // Cursor Forward
            cursor_x += nums[0];
            ensureValidCursor();
            break;
        case 'D': // Cursor Back
            cursor_x -= nums[0];
            ensureValidCursor();
            break;
        case 'E': // Cursor Next Line
            cursor_y += nums[0];
            cursor_x = 0;
            ensureValidCursor();
            break;
        case 'F': // Cursor Previous Line
            cursor_y -= nums[0];
            cursor_x = 0;
            ensureValidCursor();
            break;
        case 'G': // Cursor Horizontal Absolute
            cursor_x = nums[0] - 1; // Convert to 0-based
            ensureValidCursor();
            break;
        case 'J': // Erase in Display
            if (nums[0] == 0) { // Clear from cursor to end of display
                // Clear from cursor to end of current line
                for (int x = cursor_x; x < width; ++x) {
                    grid[cursor_y][x] = Cell();
                }
                // Clear all lines below current line
                for (int y = cursor_y + 1; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        grid[y][x] = Cell();
                    }
                }
            } else if (nums[0] == 1) { // Clear from start of display to cursor
                // Clear all lines above current line
                for (int y = 0; y < cursor_y; ++y) {
                    for (int x = 0; x < width; ++x) {
                        grid[y][x] = Cell();
                    }
                }
                // Clear from start of current line to cursor
                for (int x = 0; x <= cursor_x; ++x) {
                    grid[cursor_y][x] = Cell();
                }
            } else if (nums[0] == 2) { // Clear entire screen
                clear();
            }
            break;
        case 'K': // Erase in Line
            if (nums[0] == 0) { // Clear from cursor to end of line
                for (int x = cursor_x; x < width; ++x) {
                    grid[cursor_y][x] = Cell();
                }
            } else if (nums[0] == 1) { // Clear from start of line to cursor
                for (int x = 0; x <= cursor_x; ++x) {
                    grid[cursor_y][x] = Cell();
                }
            } else if (nums[0] == 2) { // Clear entire line
                for (int x = 0; x < width; ++x) {
                    grid[cursor_y][x] = Cell();
                }
            }
            break;
        case 's': // Save cursor position
            // For now, just ignore - btop doesn't rely heavily on this
            break;
        case 'u': // Restore cursor position  
            // For now, just ignore - btop doesn't rely heavily on this
            break;
        case 'm': // SGR (Select Graphic Rendition)
            parseSGR(nums);
            break;
    }
}

void Renderer::processSequence(const std::string& ansi_text) {
    for (size_t i = 0; i < ansi_text.length(); ++i) {
        unsigned char ch = static_cast<unsigned char>(ansi_text[i]);
        
        if (ch == 0x1B && i + 1 < ansi_text.length() && ansi_text[i + 1] == '[') {
            // CSI sequence
            size_t start = i + 2;
            size_t end = start;
            
            // Find the end of the CSI sequence
            while (end < ansi_text.length()) {
                unsigned char term = static_cast<unsigned char>(ansi_text[end]);
                if (term >= 0x40 && term <= 0x7E) { // Final byte
                    break;
                }
                end++;
            }
            
            if (end < ansi_text.length()) {
                std::string csi_params = ansi_text.substr(start, end - start + 1);
                parseCSI(csi_params);
                i = end; // Skip past the CSI sequence
            }
        } else if (ch == '\n') {
            cursor_y++;
            cursor_x = 0;
            ensureValidCursor();
        } else if (ch == '\r') {
            cursor_x = 0;
        } else if (ch >= 0x20) { // Printable character or UTF-8 start
            char32_t codepoint = 0;
            size_t utf8_len = 1;
            
            // Decode UTF-8 character
            if ((ch & 0x80) == 0) {
                // ASCII character (0xxxxxxx)
                codepoint = ch;
            } else if ((ch & 0xE0) == 0xC0) {
                // 2-byte UTF-8 (110xxxxx 10xxxxxx)
                if (i + 1 < ansi_text.length()) {
                    unsigned char ch2 = static_cast<unsigned char>(ansi_text[i + 1]);
                    if ((ch2 & 0xC0) == 0x80) {
                        codepoint = ((ch & 0x1F) << 6) | (ch2 & 0x3F);
                        utf8_len = 2;
                    } else {
                        codepoint = 0xFFFD; // Replacement character
                    }
                } else {
                    codepoint = 0xFFFD;
                }
            } else if ((ch & 0xF0) == 0xE0) {
                // 3-byte UTF-8 (1110xxxx 10xxxxxx 10xxxxxx)
                if (i + 2 < ansi_text.length()) {
                    unsigned char ch2 = static_cast<unsigned char>(ansi_text[i + 1]);
                    unsigned char ch3 = static_cast<unsigned char>(ansi_text[i + 2]);
                    if ((ch2 & 0xC0) == 0x80 && (ch3 & 0xC0) == 0x80) {
                        codepoint = ((ch & 0x0F) << 12) | ((ch2 & 0x3F) << 6) | (ch3 & 0x3F);
                        utf8_len = 3;
                    } else {
                        codepoint = 0xFFFD;
                    }
                } else {
                    codepoint = 0xFFFD;
                }
            } else if ((ch & 0xF8) == 0xF0) {
                // 4-byte UTF-8 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
                if (i + 3 < ansi_text.length()) {
                    unsigned char ch2 = static_cast<unsigned char>(ansi_text[i + 1]);
                    unsigned char ch3 = static_cast<unsigned char>(ansi_text[i + 2]);
                    unsigned char ch4 = static_cast<unsigned char>(ansi_text[i + 3]);
                    if ((ch2 & 0xC0) == 0x80 && (ch3 & 0xC0) == 0x80 && (ch4 & 0xC0) == 0x80) {
                        codepoint = ((ch & 0x07) << 18) | ((ch2 & 0x3F) << 12) | ((ch3 & 0x3F) << 6) | (ch4 & 0x3F);
                        utf8_len = 4;
                    } else {
                        codepoint = 0xFFFD;
                    }
                } else {
                    codepoint = 0xFFFD;
                }
            } else {
                // Invalid UTF-8 start byte
                codepoint = 0xFFFD;
            }
            
            // Place the character in the grid
            if (cursor_y >= 0 && cursor_y < height && cursor_x >= 0 && cursor_x < width) {
                Cell& cell = grid[cursor_y][cursor_x];
                cell = current_style;
                cell.ch = codepoint;
                cursor_x++;
                
                if (cursor_x >= width) {
                    cursor_x = 0;
                    cursor_y++;
                    ensureValidCursor();
                }
            }
            
            // Skip the additional UTF-8 bytes
            i += utf8_len - 1;
        }
    }
}

std::string Renderer::renderToResoniteHTML() {
    std::string result;
    result.reserve(grid.size() * grid[0].size() * 20); // Pre-allocate
    
    // Find the last line with content to avoid rendering trailing empty lines
    int last_content_line = height - 1;
    for (int y = height - 1; y >= 0; --y) {
        bool line_has_content = false;
        for (int x = 0; x < width; ++x) {
            if (grid[y][x].ch != U' ' || grid[y][x].has_bg_color) {
                line_has_content = true;
                break;
            }
        }
        if (line_has_content) {
            last_content_line = y;
            break;
        }
    }
    
    for (int y = 0; y <= last_content_line; ++y) {
        
        Cell last_style;
        bool style_open = false;
        bool first_char_in_line = true;
        
        for (int x = 0; x < width; ++x) {
            const Cell& cell = grid[y][x];
            
            // Check if we need to change style
            bool style_changed = first_char_in_line || (
                cell.bold != last_style.bold ||
                cell.italic != last_style.italic ||
                cell.underline != last_style.underline ||
                cell.reverse != last_style.reverse ||
                (cell.has_fg_color != last_style.has_fg_color) ||
                (cell.has_bg_color != last_style.has_bg_color) ||
                (cell.has_fg_color && last_style.has_fg_color && cell.fg_color != last_style.fg_color) ||
                (cell.has_bg_color && last_style.has_bg_color && cell.bg_color != last_style.bg_color)
            );
            
            if (style_changed) {
                // Close previous style if not first character in line
                if (style_open && !first_char_in_line) {
                    result += "</closeall>";
                }
                style_open = false;
                
                // Open new style only if there are any style attributes
                if (cell.bold || cell.italic || cell.underline || cell.reverse || 
                    cell.has_fg_color || cell.has_bg_color) {
                    
                    if (cell.has_fg_color) {
                        result += "<color=" + rgbToHex(cell.fg_color) + ">";
                    }
                    if (cell.has_bg_color) {
                        result += "<mark=" + rgbToHex(cell.bg_color) + ">";
                    }
                    if (cell.bold) {
                        result += "<b>";
                    }
                    if (cell.italic) {
                        result += "<i>";
                    }
                    if (cell.underline) {
                        result += "<u>";
                    }
                    if (cell.reverse) {
                        result += "<reverse>";
                    }
                    
                    style_open = true;
                }
                
                last_style = cell;
                first_char_in_line = false;
            }
            
            // Add the character
            if (cell.ch == U' ') {
                result += " ";
            } else {
                result += utf8FromCodepoint(cell.ch);
            }
        }
        
        // Close any open style at end of line
        if (style_open) {
            result += "</closeall>";
            style_open = false;
        }
        
        // Add line break except for the last rendered line
        if (y < last_content_line) {
            result += "<br>";
        }
    }
    
    return result;
}

} // namespace VT
