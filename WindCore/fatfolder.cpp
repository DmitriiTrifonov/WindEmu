#include "fatfolder.h"
#include "cfcard.h"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <vector>
#ifndef _WIN32
#include <utime.h>
#endif

namespace fs = std::filesystem;

namespace {

const uint32_t SectorSize = 512;
const uint32_t PartitionStart = 32; // in sectors, as on a card formatted by a PC
const uint32_t RootEntries = 512;
const uint32_t RootSectors = RootEntries * 32 / SectorSize;
const uint64_t MinVolumeSize = 64ull << 20;
const uint64_t FreeSpace = 64ull << 20; // room for EPOC to add files
const uint32_t MinClusters = 4100, MaxClusters = 65500; // safely within FAT16's range
const char VolumeLabel[12] = "WINDEMU    ";

const uint64_t HashStart = 0xCBF29CE484222325; // FNV-1a
uint64_t hashBytes(uint64_t hash, const uint8_t *data, size_t len) {
	for (size_t i = 0; i < len; i++)
		hash = (hash ^ data[i]) * 0x100000001B3;
	return hash;
}

bool utf8ToUtf16(const std::string &in, std::u16string &out) {
	out.clear();
	for (size_t i = 0; i < in.size();) {
		uint8_t c = in[i++];
		uint32_t cp;
		int more;
		if (c < 0x80) { cp = c; more = 0; }
		else if ((c >> 5) == 6) { cp = c & 0x1F; more = 1; }
		else if ((c >> 4) == 0xE) { cp = c & 0xF; more = 2; }
		else if ((c >> 3) == 0x1E) { cp = c & 7; more = 3; }
		else return false;
		while (more--) {
			if (i >= in.size() || (in[i] & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (in[i++] & 0x3F);
		}
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
			return false;
		if (cp >= 0x10000) {
			cp -= 0x10000;
			out += (char16_t)(0xD800 + (cp >> 10));
			out += (char16_t)(0xDC00 + (cp & 0x3FF));
		} else {
			out += (char16_t)cp;
		}
	}
	return true;
}

std::string utf16ToUtf8(const std::u16string &in) {
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		uint32_t cp = in[i];
		if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] < 0xE000)
			cp = 0x10000 + ((cp - 0xD800) << 10) + (in[++i] - 0xDC00);
		else if (cp >= 0xD800 && cp < 0xE000)
			cp = 0xFFFD;
		if (cp < 0x80) {
			out += (char)cp;
		} else if (cp < 0x800) {
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		} else {
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}
	return out;
}

bool readAt(FILE *f, uint64_t offset, void *data, size_t len) {
	return fseek(f, (long)offset, SEEK_SET) == 0 && fread(data, 1, len, f) == len;
}

bool writeAt(FILE *f, uint64_t offset, const void *data, size_t len) {
	return fseek(f, (long)offset, SEEK_SET) == 0 && fwrite(data, 1, len, f) == len;
}

void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
uint16_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

void fatDateTime(time_t t, uint16_t &date, uint16_t &time) {
	struct tm *tm = localtime(&t);
	if (!tm || tm->tm_year < 80) {
		date = (1 << 5) | 1; // 1980-01-01
		time = 0;
		return;
	}
	date = ((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday;
	time = (tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2);
}

time_t hostTime(uint16_t date, uint16_t time) {
	struct tm tm = {};
	tm.tm_year = (date >> 9) + 80;
	tm.tm_mon = ((date >> 5) & 0xF) - 1;
	tm.tm_mday = date & 0x1F;
	tm.tm_hour = time >> 11;
	tm.tm_min = (time >> 5) & 0x3F;
	tm.tm_sec = (time & 0x1F) * 2;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

void setModificationTime(const fs::path &path, time_t t) {
#ifndef _WIN32
	struct utimbuf times = {t, t};
	utime(path.c_str(), &times);
#else
	(void)path; (void)t;
#endif
}

uint8_t shortNameChecksum(const uint8_t name[11]) {
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++)
		sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
	return sum;
}

struct Node {
	std::string name; // as on the host
	std::u16string utf16Name;
	fs::path hostPath;
	bool isDir = false;
	uint64_t size = 0;
	time_t mtime = 0;
	std::vector<Node> children;

	uint8_t shortName[11];
	bool needsLongName = false;
	uint32_t firstCluster = 0, clusterCount = 0;

	uint32_t entryCount() const {
		return 1 + (needsLongName ? (uint32_t)(utf16Name.size() + 12) / 13 : 0);
	}
	uint32_t dirEntryCount(bool isRoot) const {
		uint32_t count = isRoot ? 1 : 2; // volume label, or . and ..
		for (const Node &child : children)
			count += child.entryCount();
		return count;
	}
};

bool validName(const std::string &name, std::u16string &utf16) {
	if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ')
		return false;
	for (unsigned char c : name)
		if (c < 0x20 || strchr("\\/:*?\"<>|", c))
			return false;
	return utf8ToUtf16(name, utf16) && utf16.size() <= 255;
}

bool scanFolder(Node &dir) {
	std::error_code ec;
	std::vector<fs::directory_entry> entries;
	for (fs::directory_iterator it(dir.hostPath, ec), end; !ec && it != end; it.increment(ec))
		entries.push_back(*it);
	if (ec) {
		fprintf(stderr, "CF folder: can't read %s: %s\n", dir.hostPath.string().c_str(), ec.message().c_str());
		return false;
	}
	std::sort(entries.begin(), entries.end(), [](const fs::directory_entry &a, const fs::directory_entry &b) {
		return a.path().filename() < b.path().filename();
	});

	std::set<std::u16string> seen;
	for (const fs::directory_entry &entry : entries) {
		Node child;
		child.name = entry.path().filename().string();
		child.hostPath = entry.path();
		if (!validName(child.name, child.utf16Name)) {
			fprintf(stderr, "CF folder: skipping %s, its name can't be used on FAT\n", child.hostPath.string().c_str());
			continue;
		}
		// FAT ignores case, so two names that differ only in case would collide
		std::u16string folded = child.utf16Name;
		for (char16_t &c : folded)
			if (c >= 'a' && c <= 'z')
				c -= 32;
		if (!seen.insert(folded).second) {
			fprintf(stderr, "CF folder: skipping %s, another name differs from it only in case\n", child.hostPath.string().c_str());
			continue;
		}

		struct stat st;
		if (stat(child.hostPath.string().c_str(), &st) != 0)
			continue;
		child.mtime = st.st_mtime;
		if (S_ISDIR(st.st_mode)) {
			if (entry.is_symlink(ec)) {
				fprintf(stderr, "CF folder: skipping %s, a link to a folder\n", child.hostPath.string().c_str());
				continue;
			}
			child.isDir = true;
			if (!scanFolder(child))
				return false;
		} else if (S_ISREG(st.st_mode)) {
			if ((uint64_t)st.st_size >= 0xFFFFFFFFull) {
				fprintf(stderr, "CF folder: skipping %s, too big for FAT\n", child.hostPath.string().c_str());
				continue;
			}
			child.size = st.st_size;
		} else {
			continue;
		}
		dir.children.push_back(std::move(child));
	}
	return true;
}

const char *ShortNamePunctuation = "!#$%&'()-@^_`{}~";

// Picks an 8.3 name for each entry of the folder, and works out which ones also need a long name
void assignShortNames(Node &dir) {
	std::set<std::string> taken;
	for (Node &node : dir.children) {
		std::string base = node.name, ext;
		size_t dot = node.name.rfind('.');
		if (dot != std::string::npos && dot > 0) {
			base = node.name.substr(0, dot);
			ext = node.name.substr(dot + 1);
		}
		bool lossy = false;
		auto convert = [&lossy](const std::string &in, size_t maxLen) {
			std::string out;
			for (unsigned char c : in) {
				if (c == '.' || c == ' ') {
					lossy = true;
					continue;
				}
				if (c >= 'a' && c <= 'z') {
					c -= 32;
					lossy = true; // the long name keeps the case
				} else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(ShortNamePunctuation, c))) {
					c = '_';
					lossy = true;
				}
				out += (char)c;
			}
			if (out.size() > maxLen) {
				out.resize(maxLen);
				lossy = true;
			}
			return out;
		};
		std::string shortBase = convert(base, 8), shortExt = convert(ext, 3);
		if (shortBase.empty()) {
			shortBase = "_";
			lossy = true;
		}

		node.needsLongName = lossy || taken.count(shortBase + "." + shortExt);
		if (node.needsLongName) {
			for (int n = 1; ; n++) {
				std::string tail = "~" + std::to_string(n);
				std::string candidate = shortBase.substr(0, std::min(shortBase.size(), 8 - tail.size())) + tail;
				if (!taken.count(candidate + "." + shortExt)) {
					shortBase = candidate;
					break;
				}
			}
		}
		taken.insert(shortBase + "." + shortExt);
		memset(node.shortName, ' ', 11);
		memcpy(node.shortName, shortBase.data(), shortBase.size());
		memcpy(node.shortName + 8, shortExt.data(), shortExt.size());

		if (node.isDir)
			assignShortNames(node);
	}
}

void countClusters(const Node &dir, uint32_t clusterBytes, uint64_t &clusters) {
	for (const Node &node : dir.children) {
		if (node.isDir) {
			clusters += std::max<uint64_t>(1, (node.dirEntryCount(false) * 32 + clusterBytes - 1) / clusterBytes);
			countClusters(node, clusterBytes, clusters);
		} else {
			clusters += (node.size + clusterBytes - 1) / clusterBytes;
		}
	}
}

void allocateClusters(Node &dir, uint32_t clusterBytes, uint32_t &next) {
	for (Node &node : dir.children) {
		if (node.isDir)
			node.clusterCount = std::max<uint32_t>(1, (node.dirEntryCount(false) * 32 + clusterBytes - 1) / clusterBytes);
		else
			node.clusterCount = (uint32_t)((node.size + clusterBytes - 1) / clusterBytes);
		node.firstCluster = node.clusterCount ? next : 0;
		next += node.clusterCount;
		if (node.isDir)
			allocateClusters(node, clusterBytes, next);
	}
}

void putEntry(std::vector<uint8_t> &out, const uint8_t name[11], uint8_t attr, uint32_t cluster, uint32_t size, time_t mtime) {
	uint8_t e[32] = {};
	memcpy(e, name, 11);
	e[11] = attr;
	uint16_t date, time;
	fatDateTime(mtime, date, time);
	put16(e + 14, time);
	put16(e + 16, date);
	put16(e + 18, date);
	put16(e + 22, time);
	put16(e + 24, date);
	put16(e + 26, cluster);
	put32(e + 28, size);
	out.insert(out.end(), e, e + 32);
}

void putLongName(std::vector<uint8_t> &out, const std::u16string &name, const uint8_t shortName[11]) {
	static const int charOffsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
	uint8_t checksum = shortNameChecksum(shortName);
	int count = (int)(name.size() + 12) / 13;
	for (int i = count; i >= 1; i--) {
		uint8_t e[32] = {};
		e[0] = i | (i == count ? 0x40 : 0);
		e[11] = 0x0F;
		e[13] = checksum;
		for (int k = 0; k < 13; k++) {
			size_t index = (i - 1) * 13 + k;
			uint16_t c = index < name.size() ? name[index] : (index == name.size() ? 0 : 0xFFFF);
			put16(e + charOffsets[k], c);
		}
		out.insert(out.end(), e, e + 32);
	}
}

std::vector<uint8_t> directoryData(const Node &dir, bool isRoot, uint32_t parentCluster) {
	std::vector<uint8_t> out;
	if (isRoot) {
		putEntry(out, (const uint8_t *)VolumeLabel, 0x08, 0, 0, time(nullptr));
	} else {
		uint8_t dotName[11], dotDotName[11];
		memset(dotName, ' ', 11);
		memset(dotDotName, ' ', 11);
		dotName[0] = dotDotName[0] = dotDotName[1] = '.';
		putEntry(out, dotName, 0x10, dir.firstCluster, 0, dir.mtime);
		putEntry(out, dotDotName, 0x10, parentCluster, 0, dir.mtime);
	}
	for (const Node &node : dir.children) {
		if (node.needsLongName)
			putLongName(out, node.utf16Name, node.shortName);
		putEntry(out, node.shortName, node.isDir ? 0x10 : 0x20, node.firstCluster, node.isDir ? 0 : (uint32_t)node.size, node.mtime);
	}
	return out;
}

struct Layout {
	uint32_t sectorsPerCluster, fatSectors, clusters, volumeSectors;
	uint64_t fatStart, rootStart, dataStart; // byte offsets in the image

	uint64_t clusterOffset(uint32_t cluster) const {
		return dataStart + (uint64_t)(cluster - 2) * sectorsPerCluster * SectorSize;
	}
};

} // namespace


bool FatFolder::build(const std::string &folderPath, const std::string &imagePath) {
	folder = folderPath;
	files.clear();
	dirs.clear();

	Node root;
	root.hostPath = folderPath;
	if (!scanFolder(root))
		return false;
	assignShortNames(root);
	if (root.dirEntryCount(true) > RootEntries) {
		fprintf(stderr, "CF folder: too many files at the top of %s; put some in a folder\n", folderPath.c_str());
		return false;
	}

	// the smallest clusters that keep the volume within FAT16's limits
	Layout layout = {};
	for (uint32_t spc = 1; spc <= 64 && !layout.sectorsPerCluster; spc *= 2) {
		uint32_t clusterBytes = spc * SectorSize;
		uint64_t used = 0;
		countClusters(root, clusterBytes, used);
		uint64_t clusters = std::max(used + FreeSpace / clusterBytes, MinVolumeSize / clusterBytes);
		if (clusters < MinClusters || clusters > MaxClusters)
			continue;
		layout.sectorsPerCluster = spc;
		layout.clusters = (uint32_t)clusters;
	}
	if (!layout.sectorsPerCluster) {
		fprintf(stderr, "CF folder: %s holds too much for a FAT16 card\n", folderPath.c_str());
		return false;
	}
	layout.fatSectors = ((layout.clusters + 2) * 2 + SectorSize - 1) / SectorSize;
	layout.volumeSectors = 1 + 2 * layout.fatSectors + RootSectors + layout.clusters * layout.sectorsPerCluster;
	layout.fatStart = (uint64_t)(PartitionStart + 1) * SectorSize;
	layout.rootStart = layout.fatStart + 2ull * layout.fatSectors * SectorSize;
	layout.dataStart = layout.rootStart + RootSectors * SectorSize;
	uint32_t totalSectors = PartitionStart + layout.volumeSectors;

	uint32_t next = 2;
	allocateClusters(root, layout.sectorsPerCluster * SectorSize, next);

	FILE *image = fopen(imagePath.c_str(), "w+b");
	if (!image) {
		fprintf(stderr, "CF folder: can't create %s\n", imagePath.c_str());
		return false;
	}
	std::error_code ec;
	fs::resize_file(imagePath, (uint64_t)totalSectors * SectorSize, ec);
	bool ok = !ec;

	uint16_t cylinders, heads, sectorsPerTrack;
	CFCard::geometryFor(totalSectors, cylinders, heads, sectorsPerTrack);

	// partition table
	uint8_t sector[SectorSize] = {};
	auto putChs = [&](uint8_t *p, uint32_t lba) {
		uint32_t cylinder = lba / (heads * sectorsPerTrack);
		if (cylinder > 1023) {
			p[0] = 0xFE; p[1] = 0xFF; p[2] = 0xFF;
			return;
		}
		p[0] = (lba / sectorsPerTrack) % heads;
		p[1] = (lba % sectorsPerTrack + 1) | ((cylinder >> 2) & 0xC0);
		p[2] = cylinder & 0xFF;
	};
	uint8_t *partition = sector + 0x1BE;
	putChs(partition + 1, PartitionStart);
	partition[4] = 0x06; // FAT16
	putChs(partition + 5, totalSectors - 1);
	put32(partition + 8, PartitionStart);
	put32(partition + 12, layout.volumeSectors);
	sector[510] = 0x55;
	sector[511] = 0xAA;
	ok = ok && writeAt(image, 0, sector, SectorSize);

	// boot sector
	memset(sector, 0, sizeof(sector));
	memcpy(sector, "\xEB\x3C\x90" "WINDEMU ", 11);
	put16(sector + 11, SectorSize);
	sector[13] = layout.sectorsPerCluster;
	put16(sector + 14, 1); // reserved sectors
	sector[16] = 2; // FATs
	put16(sector + 17, RootEntries);
	if (layout.volumeSectors < 0x10000)
		put16(sector + 19, layout.volumeSectors);
	else
		put32(sector + 32, layout.volumeSectors);
	sector[21] = 0xF8;
	put16(sector + 22, layout.fatSectors);
	put16(sector + 24, sectorsPerTrack);
	put16(sector + 26, heads);
	put32(sector + 28, PartitionStart);
	sector[36] = 0x80;
	sector[38] = 0x29;
	put32(sector + 39, (uint32_t)time(nullptr));
	memcpy(sector + 43, VolumeLabel, 11);
	memcpy(sector + 54, "FAT16   ", 8);
	sector[510] = 0x55;
	sector[511] = 0xAA;
	ok = ok && writeAt(image, (uint64_t)PartitionStart * SectorSize, sector, SectorSize);

	// FATs: everything lies in one contiguous run of clusters
	std::vector<uint8_t> fat(layout.fatSectors * SectorSize, 0);
	put16(&fat[0], 0xFFF8);
	put16(&fat[2], 0xFFFF);
	std::function<void(const Node &)> chainClusters = [&](const Node &dir) {
		for (const Node &node : dir.children) {
			for (uint32_t i = 0; i < node.clusterCount; i++) {
				uint32_t cluster = node.firstCluster + i;
				put16(&fat[cluster * 2], (i + 1 == node.clusterCount) ? 0xFFFF : cluster + 1);
			}
			if (node.isDir)
				chainClusters(node);
		}
	};
	chainClusters(root);
	for (int i = 0; i < 2; i++)
		ok = ok && writeAt(image, layout.fatStart + (uint64_t)i * layout.fatSectors * SectorSize, fat.data(), fat.size());

	// directories and file contents
	std::vector<uint8_t> buffer(64 * 1024);
	std::function<bool(const Node &, bool, uint32_t, const std::string &)> writeTree =
		[&](const Node &dir, bool isRoot, uint32_t parentCluster, const std::string &relPath) {
		std::vector<uint8_t> entries = directoryData(dir, isRoot, parentCluster);
		if (!writeAt(image, isRoot ? layout.rootStart : layout.clusterOffset(dir.firstCluster), entries.data(), entries.size()))
			return false;
		for (const Node &node : dir.children) {
			std::string rel = relPath.empty() ? node.name : relPath + "/" + node.name;
			if (node.isDir) {
				dirs.insert(rel);
				if (!writeTree(node, false, isRoot ? 0 : dir.firstCluster, rel))
					return false;
				continue;
			}
			FILE *in = fopen(node.hostPath.string().c_str(), "rb");
			if (!in) {
				fprintf(stderr, "CF folder: can't read %s\n", node.hostPath.string().c_str());
				return false;
			}
			uint64_t hash = HashStart, done = 0;
			bool fileOk = node.size == 0 || fseek(image, (long)layout.clusterOffset(node.firstCluster), SEEK_SET) == 0;
			while (fileOk && done < node.size) {
				size_t len = (size_t)std::min<uint64_t>(buffer.size(), node.size - done);
				size_t got = fread(buffer.data(), 1, len, in);
				memset(buffer.data() + got, 0, len - got); // the file shrank meanwhile
				hash = hashBytes(hash, buffer.data(), len);
				fileOk = fwrite(buffer.data(), 1, len, image) == len;
				done += len;
			}
			fclose(in);
			if (!fileOk)
				return false;
			files[rel] = {node.size, hash};
		}
		return true;
	};
	ok = ok && writeTree(root, true, 0, "");

	if (fclose(image) != 0)
		ok = false;
	if (!ok)
		fprintf(stderr, "CF folder: couldn't write %s\n", imagePath.c_str());
	return ok;
}


namespace {

struct Volume {
	FILE *image;
	Layout layout;
	std::vector<uint16_t> fat;

	// Calls back with each cluster of a chain in turn, until it returns false
	bool forEachCluster(uint32_t first, const std::function<bool(uint32_t)> &callback) const {
		uint32_t cluster = first, steps = 0;
		while (cluster >= 2 && cluster < layout.clusters + 2) {
			if (++steps > layout.clusters || !callback(cluster))
				return false;
			cluster = fat[cluster];
		}
		return cluster >= 0xFFF8 || cluster == 0;
	}

	bool readCluster(uint32_t cluster, std::vector<uint8_t> &out) const {
		out.resize(layout.sectorsPerCluster * SectorSize);
		return readAt(image, layout.clusterOffset(cluster), out.data(), out.size());
	}
};

struct VolumeEntry {
	std::string path;
	bool isDir;
	uint32_t cluster, size;
	uint16_t date, time;
};

bool readDirectory(const Volume &vol, const std::vector<uint8_t> &data, const std::string &relPath,
                   std::vector<VolumeEntry> &out, int depth);

bool parseDirectory(const Volume &vol, const std::vector<uint8_t> &data, const std::string &relPath,
                    std::vector<VolumeEntry> &out, int depth) {
	static const int charOffsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
	std::vector<std::u16string> longParts;
	uint8_t longChecksum = 0;

	for (size_t pos = 0; pos + 32 <= data.size(); pos += 32) {
		const uint8_t *e = &data[pos];
		if (e[0] == 0)
			break;
		if (e[0] == 0xE5) {
			longParts.clear();
			continue;
		}
		uint8_t attr = e[11];
		if ((attr & 0x3F) == 0x0F) {
			int seq = e[0] & 0x1F;
			if (e[0] & 0x40) {
				longParts.assign(seq, std::u16string());
				longChecksum = e[13];
			}
			if (seq >= 1 && seq <= (int)longParts.size()) {
				std::u16string part;
				for (int k = 0; k < 13; k++)
					part += (char16_t)get16(e + charOffsets[k]);
				longParts[seq - 1] = part;
			}
			continue;
		}
		if (attr & 0x08) { // volume label
			longParts.clear();
			continue;
		}

		std::string name;
		bool haveLong = !longParts.empty() && longChecksum == shortNameChecksum(e);
		for (const std::u16string &part : longParts)
			if (part.empty())
				haveLong = false;
		if (haveLong) {
			std::u16string longName;
			for (const std::u16string &part : longParts)
				longName += part;
			size_t end = longName.find(u'\0');
			if (end != std::u16string::npos)
				longName.resize(end);
			name = utf16ToUtf8(longName);
		} else {
			std::string base((const char *)e, 8), ext((const char *)e + 8, 3);
			if ((uint8_t)base[0] == 0x05)
				base[0] = (char)0xE5;
			base.erase(base.find_last_not_of(' ') + 1);
			ext.erase(ext.find_last_not_of(' ') + 1);
			// Windows NT's flags for an all lowercase base or extension
			if (e[12] & 0x08)
				for (char &c : base) c = tolower((unsigned char)c);
			if (e[12] & 0x10)
				for (char &c : ext) c = tolower((unsigned char)c);
			name = ext.empty() ? base : base + "." + ext;
		}
		longParts.clear();

		if (name == "." || name == "..")
			continue;
		if (name.empty() || name.find('/') != std::string::npos || name.find('\0') != std::string::npos) {
			fprintf(stderr, "CF folder: ignoring an entry with an unusable name in /%s\n", relPath.c_str());
			continue;
		}

		VolumeEntry entry;
		entry.path = relPath.empty() ? name : relPath + "/" + name;
		entry.isDir = attr & 0x10;
		entry.cluster = get16(e + 26);
		entry.size = get32(e + 28);
		entry.time = get16(e + 22);
		entry.date = get16(e + 24);
		out.push_back(entry);

		if (entry.isDir) {
			std::vector<uint8_t> subdir, cluster;
			bool ok = vol.forEachCluster(entry.cluster, [&](uint32_t c) {
				if (!vol.readCluster(c, cluster))
					return false;
				subdir.insert(subdir.end(), cluster.begin(), cluster.end());
				return true;
			});
			if (!ok || !readDirectory(vol, subdir, entry.path, out, depth + 1))
				return false;
		}
	}
	return true;
}

bool readDirectory(const Volume &vol, const std::vector<uint8_t> &data, const std::string &relPath,
                   std::vector<VolumeEntry> &out, int depth) {
	if (depth > 64) {
		fprintf(stderr, "CF folder: folders on the card nest too deeply\n");
		return false;
	}
	return parseDirectory(vol, data, relPath, out, depth);
}

bool hashHostFile(const fs::path &path, uint64_t &size, uint64_t &hash) {
	FILE *f = fopen(path.string().c_str(), "rb");
	if (!f)
		return false;
	std::vector<uint8_t> buffer(64 * 1024);
	size = 0;
	hash = HashStart;
	size_t got;
	while ((got = fread(buffer.data(), 1, buffer.size(), f)) > 0) {
		hash = hashBytes(hash, buffer.data(), got);
		size += got;
	}
	fclose(f);
	return true;
}

} // namespace


bool FatFolder::syncBack(FILE *image) {
	fflush(image);

	// find the volume: EPOC may have reformatted the card, with or without a partition table
	uint8_t sector[SectorSize];
	if (!readAt(image, 0, sector, SectorSize))
		return false;
	uint32_t start = 0;
	uint8_t type = sector[0x1BE + 4];
	if (sector[510] == 0x55 && sector[511] == 0xAA && (type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0E))
		start = get32(sector + 0x1BE + 8);
	if (!readAt(image, (uint64_t)start * SectorSize, sector, SectorSize))
		return false;

	Volume vol;
	vol.image = image;
	uint32_t spc = sector[13], reserved = get16(sector + 14), fats = sector[16], rootEntries = get16(sector + 17);
	uint32_t total = get16(sector + 19) ? get16(sector + 19) : get32(sector + 32);
	uint32_t fatSectors = get16(sector + 22);
	uint32_t rootSectors = (rootEntries * 32 + SectorSize - 1) / SectorSize;
	uint32_t dataSector = reserved + fats * fatSectors + rootSectors;
	if (get16(sector + 11) != SectorSize || !spc || (spc & (spc - 1)) || !fats || !fatSectors || total <= dataSector) {
		fprintf(stderr, "CF folder: the card no longer holds a FAT16 volume\n");
		return false;
	}
	vol.layout.sectorsPerCluster = spc;
	vol.layout.clusters = (total - dataSector) / spc;
	if (vol.layout.clusters < 4085 || vol.layout.clusters >= 65525) {
		fprintf(stderr, "CF folder: the card was reformatted as something other than FAT16\n");
		return false;
	}
	vol.layout.fatStart = ((uint64_t)start + reserved) * SectorSize;
	vol.layout.rootStart = vol.layout.fatStart + (uint64_t)fats * fatSectors * SectorSize;
	vol.layout.dataStart = vol.layout.rootStart + (uint64_t)rootSectors * SectorSize;

	std::vector<uint8_t> fatBytes((size_t)fatSectors * SectorSize);
	if (!readAt(image, vol.layout.fatStart, fatBytes.data(), fatBytes.size()))
		return false;
	vol.fat.resize(vol.layout.clusters + 2);
	for (size_t i = 0; i < vol.fat.size() && i * 2 + 1 < fatBytes.size(); i++)
		vol.fat[i] = get16(&fatBytes[i * 2]);

	std::vector<uint8_t> rootData((size_t)rootSectors * SectorSize);
	std::vector<VolumeEntry> entries;
	if (!readAt(image, vol.layout.rootStart, rootData.data(), rootData.size()) ||
		!readDirectory(vol, rootData, "", entries, 0)) {
		fprintf(stderr, "CF folder: couldn't read the card's folders\n");
		return false;
	}

	bool ok = true;
	fs::path base(folder);
	std::set<std::string> present;
	std::vector<uint8_t> cluster;
	for (const VolumeEntry &entry : entries) {
		present.insert(entry.path);
		fs::path hostPath = base / fs::u8path(entry.path);
		std::error_code ec;
		if (entry.isDir) {
			fs::create_directories(hostPath, ec);
			if (ec) {
				fprintf(stderr, "CF folder: can't create %s\n", hostPath.string().c_str());
				ok = false;
			}
			continue;
		}

		// read it off the card once to see if it changed, then again to copy it
		auto readFile = [&](const std::function<bool(const uint8_t *, size_t)> &consume) {
			uint32_t left = entry.size;
			bool chainOk = vol.forEachCluster(entry.cluster, [&](uint32_t c) {
				if (!left)
					return false;
				if (!vol.readCluster(c, cluster))
					return false;
				size_t len = std::min<size_t>(left, cluster.size());
				left -= len;
				return consume(cluster.data(), len);
			});
			return left == 0 && (chainOk || entry.size > 0);
		};
		uint64_t hash = HashStart;
		if (!readFile([&](const uint8_t *data, size_t len) { hash = hashBytes(hash, data, len); return true; })) {
			fprintf(stderr, "CF folder: %s is damaged on the card, leaving it be\n", entry.path.c_str());
			ok = false;
			continue;
		}
		auto old = files.find(entry.path);
		if (old != files.end() && old->second.size == entry.size && old->second.hash == hash)
			continue; // unchanged

		uint64_t hostSize, hostHash;
		if (old != files.end() && hashHostFile(hostPath, hostSize, hostHash) &&
			(hostSize != old->second.size || hostHash != old->second.hash))
			fprintf(stderr, "CF folder: %s was changed both here and on the Psion; keeping the Psion's version\n", hostPath.string().c_str());

		fs::path tempPath = hostPath;
		tempPath += ".windemu-new";
		FILE *out = fopen(tempPath.string().c_str(), "wb");
		bool written = out && readFile([&](const uint8_t *data, size_t len) { return fwrite(data, 1, len, out) == len; });
		if (out && fclose(out) != 0)
			written = false;
		if (written)
			fs::rename(tempPath, hostPath, ec);
		if (!written || ec) {
			fprintf(stderr, "CF folder: can't write %s\n", hostPath.string().c_str());
			fs::remove(tempPath, ec);
			ok = false;
			continue;
		}
		setModificationTime(hostPath, hostTime(entry.date, entry.time));
	}

	// remove what EPOC deleted, unless it was changed here in the meantime
	for (const auto &file : files) {
		if (present.count(file.first))
			continue;
		fs::path hostPath = base / fs::u8path(file.first);
		uint64_t hostSize, hostHash;
		if (!hashHostFile(hostPath, hostSize, hostHash))
			continue;
		std::error_code ec;
		if (hostSize == file.second.size && hostHash == file.second.hash)
			fs::remove(hostPath, ec);
		else
			fprintf(stderr, "CF folder: %s was deleted on the Psion but changed here; keeping it\n", hostPath.string().c_str());
	}
	// deepest folders first, so that their parents can go too
	for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
		if (present.count(*it))
			continue;
		std::error_code ec;
		fs::path hostPath = base / fs::u8path(*it);
		if (fs::is_directory(hostPath, ec) && fs::is_empty(hostPath, ec))
			fs::remove(hostPath, ec);
	}
	return ok;
}
