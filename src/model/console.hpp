#pragma once

namespace vegaflow {

// Where the process's standard input comes from; only a visible console has a person who can answer a question.
enum class StdinKind { None, VisibleConsole, HiddenConsole, Pipe, File };

} // namespace vegaflow
