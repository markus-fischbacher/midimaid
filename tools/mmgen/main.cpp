#include "Mmgen.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    std::string out;
    std::string err;
    const int code = mm::tools::run(args, out, err);
    std::cout << out;
    std::cerr << err;
    return code;
}
