#include "ControlLayer.hpp"
#include <iostream>
#include <string>

// Entry point. Accepts an optional step count as argv[1] (default: 1).
// Initializes the ControlLayer and runs the pipeline for the given number of steps.
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

    ControlLayer orchestrator("../control", "../data/datalake", "../data/datamarts");

    std::cout << "Running " << steps << " pipeline step(s)...\n";
    orchestrator.run(steps);

    std::cout << "Finished.\n";
    return 0;
}
