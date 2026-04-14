#include <print>

#include "simplettf/simplettf.hpp"

int main() {
    if (const auto font = simplettf::Font::load("/usr/share/fonts/open-sans/OpenSans-Regular.ttf")) {

    } else {
        std::println(stderr,"Error: {}", font.error());
    }
    return 0;
}
