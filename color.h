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
    : red  ((double)(std::get<0>(rgb) - range.first) / (range.second - range.first + 1)),
      green((double)(std::get<1>(rgb) - range.first) / (range.second - range.first + 1)),
      blue ((double)(std::get<2>(rgb) - range.first) / (range.second - range.first + 1)) {}
    color(double r, double g, double b)
    : red(r), green(g), blue(b) {}
    color() : red(0.0), green(0.0), blue(0.0) {}
    
    tupple3 convert(int range) const
    {
        int r = (int)(  red * range);
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
