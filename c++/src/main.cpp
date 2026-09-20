#include "ControlLayer.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::cout << "=== Stage 1: C++ Search Engine Pipeline ===\n";

    int steps = 1;
    if (argc > 1) {
        try {
            steps = std::stoi(argv[1]);
        } catch (...) {
            steps = 1;
        }
    }

    // Initialize ControlLayer pointing to stage_1 control and data directories
    ControlLayer orchestrator("../control", "../data/datalake");

    std::cout << "Running " << steps << " pipeline step(s)...\n";
    orchestrator.run(steps);

    std::cout << "Finished.\n";
    return 0;
}
