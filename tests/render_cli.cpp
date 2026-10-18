// Test helper: reads NUL-separated Markdown documents on stdin, writes the rendered HTML
// of each (also NUL-separated) to stdout. Used by tests/run_spec.py.
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <iterator>
#include <string>

#include "../src/markdown.h"

int main() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::string in((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while (pos <= in.size()) {
        size_t e = in.find('\0', pos);
        if (e == std::string::npos) e = in.size();
        std::string out = RenderMarkdown(in.substr(pos, e - pos), "");
        std::cout << out << '\0';
        pos = e + 1;
    }
    return 0;
}
