#pragma once

namespace so3d {

// Runs the command-line interface. Kept as a library entry point so the
// executable stays thin and downstream projects can embed the tool if needed.
int run_cli(int argc, char* argv[]);

} // namespace so3d

