// Clean-room CoreGraphics + QuartzCore (CALayer) implementation for finn-darwin.
// Provides real RGBA8 CGBitmapContext rasterization, CGColorSpace, CGColor,
// CGRect geometry primitives, and CALayer Objective-C class.

#ifndef FINN_DARWIN_COREGRAPHICS_HPP
#define FINN_DARWIN_COREGRAPHICS_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin {

// Initializes CoreGraphics CF types and registers the QuartzCore CALayer ObjC class.
void init_coregraphics();

// Populates the loader symbol table with CoreGraphics & QuartzCore C symbols.
void register_coregraphics_symbols(std::unordered_map<std::string, void*>* table);

}  // namespace finn::darwin

#endif
