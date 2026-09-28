#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <type_traits>

// Binary state (de)serialiser: each component lists its fields once, in one
// function that both saves and loads, so the two can't drift apart.
class StateIO {
	FILE *file;
	bool loading;
	bool ok = true;

	void raw(void *data, size_t size) {
		if (!ok) return;
		if (loading)
			ok = fread(data, 1, size, file) == size;
		else
			ok = fwrite(data, 1, size, file) == size;
	}

public:
	StateIO(FILE *file, bool loading) : file(file), loading(loading) { }

	bool isLoading() const { return loading; }
	bool good() const { return ok; }
	void fail() { ok = false; }

	template<typename T> void pod(T &value) {
		static_assert(std::is_trivially_copyable<T>::value, "only plain data can be saved directly");
		raw(&value, sizeof(value));
	}

	// RAM is mostly empty: store it as 4 KiB pages, skipping all-zero ones
	void memory(uint8_t *data, size_t size) {
		static const size_t PageSize = 0x1000;
		static const uint8_t zeroPage[PageSize] = {};
		for (size_t offset = 0; offset < size && ok; offset += PageSize) {
			size_t len = (size - offset < PageSize) ? size - offset : PageSize;
			uint8_t present = memcmp(data + offset, zeroPage, len) != 0;
			pod(present);
			if (present)
				raw(data + offset, len);
			else if (loading)
				memset(data + offset, 0, len);
		}
	}
};
