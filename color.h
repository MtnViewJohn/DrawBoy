//
//  color.h
//  DrawBoy
//
//  Created by John Horigan on 12/15/24.
//

#ifndef color_h
#define color_h

#include <tuple>

class color {
public:
    using tupple3 = std::tuple<int,int,int>;
    
    // Maps integer range [low,high] to double range [0,1)
    color(tupple3 rgb, std::pair<int,int> range)
    : red  ((double)(std::get<0>(rgb) - range.first) / ((range.second - range.first) + 1.0)),
      green((double)(std::get<1>(rgb) - range.first) / ((range.second - range.first) + 1.0)),
      blue ((double)(std::get<2>(rgb) - range.first) / ((range.second - range.first) + 1.0))
    {
        if (std::get<0>(rgb) > range.second || std::get<1>(rgb) > range.second || std::get<2>(rgb) > range.second)
            throw std::runtime_error("Illegal color value.");
    }
    color(double r, double g, double b)
    : red(r), green(g), blue(b) {}
    color() : red(0.0), green(0.0), blue(0.0) {}
    color(const char* rgbhex)
    {
        errno = 0;
        int v = (int)std::strtol(rgbhex, nullptr, 16);
        if (errno || v < 0)
            throw std::runtime_error("Invalid color specification.");
        
        switch (std::strlen(rgbhex)) {
            case 3:
                red   = ((v & 0xf00) >> 8) / 16.0;  // [0,15] -> [0,1)
                green = ((v & 0x0f0) >> 4) / 16.0;
                blue  = ((v & 0x00f) >> 0) / 16.0;
                break;
            case 6:
                red   = ((v & 0xff0000) >> 16) / 256.0; // [0,255] -> [0,1)
                green = ((v & 0x00ff00) >>  8) / 256.0;
                blue  = ((v & 0x0000ff) >>  0) / 256.0;
                break;
            default:
                throw std::runtime_error("Color specification must be 3 or 6 hex digits.");
        }
    }
    
    tupple3 convert(int range) const
    {
        int r = (int)(  red * range); // [0,1) -> [0,range-1]
        int g = (int)(green * range);
        int b = (int)( blue * range);
        return {r, g, b};
    }
    int convertGray(int range) const
    {
        if (red != green || red != blue) return -1;
        int w = (int)(  red * range);
        return w;
    }
    bool useWhiteText() const
    { return (red * 0.299) + (green * 0.587) + (blue * 0.114) < 0.5; }

    double red;
    double green;
    double blue;
};


#endif /* color_h */
