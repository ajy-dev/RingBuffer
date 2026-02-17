#ifndef LOGGER_HPP
# define LOGGER_HPP

# include "RingBuffer.hpp"
# include <string_view>
# include <cstdio>
# include <filesystem>

class Logger
{
public:
	// Types
	enum class LogLevel
	{
		Debug,
		Info,
		Warning,
		Error,
		Fatal
	};

	enum class SinkBackend
	{
		STDIO,
		POSIX,
		WINDOWS
	};

	enum class SinkTarget
	{
		STDERR,
		STDOUT,
		FILE
	};

	// Copy / Move
	Logger(const Logger &other) = delete;
	Logger &operator=(const Logger &other) = delete;
	Logger(Logger &&other) = delete;
	Logger &operator=(Logger &&other) = delete;

	// Core APIs
	static Logger &get_instance() noexcept;

	bool log(LogLevel level, std::string_view str) noexcept;
	bool log(LogLevel level, const char *fmtstr, ...) noexcept;
	bool flush(void) noexcept;

	// Observer / Getters
	LogLevel get_threshold(void) const noexcept;

	// Mutators / Setters
	void set_threshold(LogLevel level) noexcept;
	bool set_backend(SinkBackend backend) noexcept;
	bool set_target_stdout(void) noexcept;
	bool set_target_stderr(void) noexcept;
	bool set_target_file(const std::filesystem::path &filepath) noexcept;
private:
	// Types
	struct Sink
	{
		SinkBackend backend;
		SinkTarget target;
		std::filesystem::path filepath;
		union
		{
			std::FILE *fp;
			int fd;
			void *windows_handle;
		} handle;

		Sink(void) noexcept;
		~Sink(void) noexcept;
		Sink(const Sink &other) = delete;
		Sink &operator=(const Sink &other) = delete;
		Sink(Sink &&other) noexcept;
		Sink &operator=(Sink &&other) noexcept;

		void close(void) noexcept;
		bool open(void) noexcept;
		std::size_t write(const char *buffer, std::size_t count) const noexcept;
	};

	// Constants
	static constexpr std::size_t BUFFER_SIZE = 1ULL << 20;
	static constexpr std::size_t MAX_LINE_LENGTH = 1ULL << 8;

	// Constructors / Destructors
	Logger(void) noexcept;
	~Logger(void) noexcept;

	// Helpers
	bool is_enabled(LogLevel level) const noexcept;

	// Members
	RingBuffer buffer;
	Sink sink;
	LogLevel threshold;
};

#endif