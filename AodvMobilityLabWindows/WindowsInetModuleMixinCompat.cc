// INET 4.7.0 does not export the ModuleMixin expression/display helpers from
// its Windows DLL. Compile its own implementation into this project so both
// OMNeT++ debug and release builds can link. The included INET source is
// licensed LGPL-3.0-or-later by OpenSim Ltd.
#ifdef _WIN32
#include "inet/common/ModuleMixin.cc"
#endif
