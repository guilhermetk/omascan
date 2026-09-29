#pragma once

#include <QStringList>
#include <QTextStream>

// OmaScan without the window: `omascan scan` and friends, for scripts, key
// bindings and the Omarchy bar. Scanning uses the scanner and settings the
// window last used, from the same ~/.config/omascan/omascan.conf.
namespace Cli {

// How a command ended, for scripts to tell apart.
enum Exit {
    Ok = 0,
    Failed = 1,       // bad arguments, or the file could not be written
    NotSetUp = 2,     // no scanner chosen yet: run `omascan setup`
    Busy = 3,         // another scan, or another app, has the scanner
    ScanFailed = 4,   // the scanner could not be reached, jammed, ran out…
    Interrupted = 130,
};

// The names QSettings files everything under, shared with the window.
void setIdentity();
// Whether these arguments ask for a command rather than the window.
bool wanted(int argc, char *argv[]);
// Runs the command in `arguments` (the program name first). `in`, `out` and
// `err` are the terminal's, or a test's.
int run(const QStringList &arguments, QTextStream &in, QTextStream &out, QTextStream &err);

} // namespace Cli
