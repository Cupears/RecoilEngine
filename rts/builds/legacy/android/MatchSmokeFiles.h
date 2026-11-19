#pragma once

#include <cstdio>
#include <cstdint>

// Each side owns one file. Rename publishes a complete update inside the
// Android app's private directory, shared only by its two processes.
namespace AndroidMatchSmoke {
	struct Position {
		float x, groundOffset, z;
		float forwardX, forwardZ;
	};

	enum Action : uint32_t { MoveTo, PreviewSolar, BuildSolar };

	struct Order {
		uint32_t sequence;
		float x, z;
		Action action;
	};

	struct Placement {
		uint32_t sequence;
		float x, z, halfWidth, halfDepth;
		uint32_t valid;
	};

	struct SolarCollector {
		float x, z, progress;
	};

	struct Economy {
		float energyIncome;
		uint32_t collectorCount;
		SolarCollector collectors[32];
	};

	template<typename T>
	bool Read(const char* path, T& value)
	{
		auto* file = std::fopen(path, "rb");
		if (!file)
			return false;
		const bool done = std::fread(&value, sizeof(T), 1, file) == 1 && std::fgetc(file) == EOF;
		std::fclose(file);
		return done;
	}

	template<typename T>
	bool Write(const char* path, const char* pending, const T& value)
	{
		auto* file = std::fopen(pending, "wb");
		if (!file)
			return false;
		const bool wrote = std::fwrite(&value, sizeof(T), 1, file) == 1;
		const bool closed = std::fclose(file) == 0;
		return wrote && closed && std::rename(pending, path) == 0;
	}
}
