#pragma once
#include <stdint.h>
#include <stdio.h>
#include <map>
#include <set>
#include <string>

// Presents a host folder as a FAT16 volume: builds a disk image from the folder,
// and later copies whatever was changed on the volume back into the folder.
class FatFolder {
	struct Snapshot { uint64_t size, hash; };

	std::string folder;
	// what the volume held when it was built, by path relative to the folder
	std::map<std::string, Snapshot> files;
	std::set<std::string> dirs;

public:
	bool build(const std::string &folder, const std::string &imagePath);
	bool syncBack(FILE *image);
};
