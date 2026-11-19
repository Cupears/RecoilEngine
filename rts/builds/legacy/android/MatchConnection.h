#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// Talks to the opt-in Lua widget in the separately staged local match.
class AndroidMatchConnection {
public:
	struct Unit {
		float x, y, z;
	};

	AndroidMatchConnection(): worker([this](std::stop_token stop) { Poll(stop); }) {}

	std::optional<Unit> GetCommander()
	{
		const std::lock_guard lock(mutex);
		return commander;
	}

	void Move(float x, float z)
	{
		const std::lock_guard lock(mutex);
		destination = Unit{x, 0, z};
	}

private:
	static std::string Request(const char* request)
	{
		const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd < 0)
			return {};
		const timeval timeout = {0, 250000};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(8454);

		std::string result;
		const size_t length = std::char_traits<char>::length(request);
		if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
		    send(fd, request, length, MSG_NOSIGNAL) == static_cast<ssize_t>(length)) {
			char buffer[96];
			while (result.size() < sizeof(buffer)) {
				const ssize_t received = recv(fd, buffer, sizeof(buffer) - result.size(), 0);
				if (received <= 0)
					break;
				result.append(buffer, received);
				if (result.find('\n') != std::string::npos)
					break;
			}
		}
		close(fd);
		return result;
	}

	void Poll(std::stop_token stop)
	{
		while (!stop.stop_requested()) {
			std::optional<Unit> target;
			{
				const std::lock_guard lock(mutex);
				target.swap(destination);
			}
			if (target) {
				char command[64];
				std::snprintf(command, sizeof(command), "MOVE %.2f %.2f\n", target->x, target->z);
				Request(command);
			}
			const std::string state = Request("STATE\n");
			Unit unit = {};
			int frame, id;
			const bool valid = std::sscanf(state.c_str(), "STATE %d %d %f %f %f",
				&frame, &id, &unit.x, &unit.y, &unit.z) == 5;
			{
				const std::lock_guard lock(mutex);
				commander = valid ? std::optional(unit) : std::nullopt;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(160));
		}
	}

	std::mutex mutex;
	std::optional<Unit> commander;
	std::optional<Unit> destination;
	std::jthread worker;
};
