#pragma once

namespace filewarden {

// Parses argv and dispatches to the requested subcommand. Returns the
// process exit code.
int runCli(int argc, char** argv);

} // namespace filewarden
