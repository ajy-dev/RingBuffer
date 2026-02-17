#include "Logger.hpp"

#if defined(_WIN32) || defined(_WIN64)
# define NOMINMAX
# include <Windows.h>
# include <io.h>
# include <fcntl.h>
# define STDIN_FILENO	0
# define STDOUT_FILENO	1
# define STDERR_FILENO	2
# define LOGGER_SYSCALL_OPEN	_open
# define LOGGER_SYSCALL_CLOSE	_close
# define LOGGER_SYSCALL_WRITE	_write
# define LOGGER_OPEN_FLAG	(_O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY)
# define LOGGER_OPEN_MODE	(_S_IREAD | _S_IWRITE)
#else
# include <unistd.h>
# include <fcntl.h>
# include <sys/stat.h>
# define LOGGER_SYSCALL_OPEN	open
# define LOGGER_SYSCALL_CLOSE	close
# define LOGGER_SYSCALL_WRITE	write
# define LOGGER_OPEN_FLAG	(O_WRONLY | O_CREAT | O_APPEND)
# define LOGGER_OPEN_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#endif

#include <new>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <algorithm>
#include <cstdarg>
#include <cassert>
#include <cstdio>
#include <utility>

static const char *loglevel_to_cstr(Logger::LogLevel level) noexcept;

Logger &Logger::get_instance() noexcept
{
	static Logger instance;

	return instance;
}

bool Logger::log(LogLevel level, std::string_view str) noexcept
{
	char buffer[MAX_LINE_LENGTH];
	const char *level_prefix;
	int snprintf_ret;
	std::size_t written;

	if (!is_enabled(level))
		return true;

	if (!str.data())
	{
		if (str.size())
			return false;
		str = std::string_view("");
	}

	level_prefix = loglevel_to_cstr(level);
	snprintf_ret = std::snprintf(
		buffer,
		sizeof(buffer),
		"[%s] %.*s",
		level_prefix,
		static_cast<int>(std::min(str.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))),
		str.data()
	);
	if (snprintf_ret < 0)
		return false;
	written = std::min(sizeof(buffer) - 1, static_cast<std::size_t>(snprintf_ret));
	written == sizeof(buffer) - 1 ? buffer[sizeof(buffer) - 2] = '\n' : buffer[written++] = '\n';

	return this->buffer.enqueue(buffer, written);
}

bool Logger::log(LogLevel level, const char *fmtstr, ...) noexcept
{
	char buffer[MAX_LINE_LENGTH];
	const char *level_prefix;
	int sprintf_ret;
	va_list args;
	int vsnprintf_ret;
	std::size_t written;

	if (!is_enabled(level))
		return true;

	if (!fmtstr)
		return false;

	level_prefix = loglevel_to_cstr(level);
	assert(std::strlen(level_prefix) + 3 < sizeof(buffer) && "Logger: Log prefix exceeded max line length");
	sprintf_ret = std::sprintf(buffer, "[%s] ", level_prefix);
	if (sprintf_ret < 0)
		return false;

	va_start(args, fmtstr);
	vsnprintf_ret = std::vsnprintf(buffer + sprintf_ret, sizeof(buffer) - static_cast<std::size_t>(sprintf_ret), fmtstr, args);
	va_end(args);
	if (vsnprintf_ret < 0)
		return false;

	written = std::min(sizeof(buffer) - 1, static_cast<std::size_t>(sprintf_ret) + static_cast<std::size_t>(vsnprintf_ret));
	written == sizeof(buffer) - 1 ? buffer[sizeof(buffer) - 2] = '\n' : buffer[written++] = '\n';

	return this->buffer.enqueue(buffer, written);
}

bool Logger::flush(void) noexcept
{
	while (this->buffer.get_used_size())
	{
		std::size_t count;
		const char *ptr;
		std::size_t bytes_written;

		count = this->buffer.get_direct_dequeue_size();
		if (!count)
			break;
		ptr = static_cast<const char *>(this->buffer.get_direct_dequeue_ptr());
		bytes_written = this->sink.write(ptr, count);
		if (!bytes_written)
			return false;
		this->buffer.advance_read_index(bytes_written);
	}
	if (this->sink.backend == SinkBackend::STDIO && this->sink.handle.fp)
	{
		if (std::fflush(this->sink.handle.fp))
			return false;
	}
	return true;
}

Logger::LogLevel Logger::get_threshold(void) const noexcept
{
	return this->threshold;
}

void Logger::set_threshold(LogLevel level) noexcept
{
	this->threshold = level;
}

bool Logger::set_backend(SinkBackend backend) noexcept
{
	Sink new_sink;

	new_sink.backend = backend;
	new_sink.target = this->sink.target;
	try
	{
		new_sink.filepath = this->sink.filepath;
	}
	catch (const std::bad_alloc &exception)
	{
		this->sink.close();
		this->sink.target = SinkTarget::STDERR;
		this->sink.backend = SinkBackend::STDIO;
		this->sink.handle.fp = stderr;
		this->flush();
		this->log(LogLevel::Error, "Logger: Critical Internal Exception");
		this->log(LogLevel::Error, exception.what());
		this->flush();
		std::abort();
	}
	catch (...)
	{
		return false;
	}
	if (!new_sink.open())
		return false;

	this->flush();
	this->sink = std::move(new_sink);

	return true;
}

bool Logger::set_target_stdout(void) noexcept
{
	Sink new_sink;

	new_sink.backend = this->sink.backend;
	new_sink.target = SinkTarget::STDOUT;
	if (!new_sink.open())
		return false;

	this->flush();
	this->sink = std::move(new_sink);

	return true;
}

bool Logger::set_target_stderr(void) noexcept
{
	Sink new_sink;

	new_sink.backend = this->sink.backend;
	new_sink.target = SinkTarget::STDERR;
	if (!new_sink.open())
		return false;

	this->flush();
	this->sink = std::move(new_sink);

	return true;
}

bool Logger::set_target_file(const std::filesystem::path &filepath) noexcept
{
	Sink new_sink;

	if (filepath.empty())
		return false;

	new_sink.backend = this->sink.backend;
	new_sink.target = SinkTarget::FILE;
	try
	{
		new_sink.filepath = filepath;
	}
	catch (const std::bad_alloc &exception)
	{
		this->sink.close();
		this->sink.target = SinkTarget::STDERR;
		this->sink.backend = SinkBackend::STDIO;
		this->sink.handle.fp = stderr;
		this->flush();
		this->log(LogLevel::Error, "Logger: Critical Internal Exception");
		this->log(LogLevel::Error, exception.what());
		this->flush();
		std::abort();
	}
	catch (...)
	{
		return false;
	}

	if (!new_sink.open())
		return false;

	this->flush();
	this->sink = std::move(new_sink);

	return true;
}

Logger::Logger(void) noexcept
	: buffer(RingBuffer(BUFFER_SIZE))
	, threshold(LogLevel::Info)
{
	if (!this->buffer.get_capacity())
		std::abort();
}

Logger::~Logger(void) noexcept
{
	this->flush();
	this->sink.close();
}

bool Logger::is_enabled(LogLevel level) const noexcept
{
	return level >= this->threshold;
}

Logger::Sink::Sink(void) noexcept
	: backend(SinkBackend::STDIO)
	, target(SinkTarget::STDERR)
{
	handle.fp = stderr;
}

Logger::Sink::~Sink(void) noexcept
{
	this->close();
}

Logger::Sink::Sink(Sink &&other) noexcept
	: backend(other.backend)
	, target(other.target)
	, filepath(std::move(other.filepath))
	, handle(other.handle)
{
	other.backend = SinkBackend::STDIO;
	other.target = SinkTarget::STDERR;
	other.handle.fp = stderr;
}

Logger::Sink &Logger::Sink::operator=(Sink &&other) noexcept
{
	if (this != &other)
	{
		this->close();
		this->backend = other.backend;
		this->target = other.target;
		this->filepath = std::move(other.filepath);
		this->handle = other.handle;

		other.backend = SinkBackend::STDIO;
		other.target = SinkTarget::STDERR;
		other.handle.fp = stderr;
	}
	return *this;
}

void Logger::Sink::close(void) noexcept
{
	if (this->target != Logger::SinkTarget::FILE)
		return;

	switch (this->backend)
	{
	case Logger::SinkBackend::STDIO:
		if (this->handle.fp)
			std::fclose(this->handle.fp);
		this->handle.fp = nullptr;
		break;
	case Logger::SinkBackend::POSIX:
		if (this->handle.fd >= 0)
			::LOGGER_SYSCALL_CLOSE(this->handle.fd);
		this->handle.fd = -1;
		break;
	case Logger::SinkBackend::WINDOWS:
	{
#if defined(_WIN32) || defined(_WIN64)
		HANDLE handle;

		handle = static_cast<HANDLE>(this->handle.windows_handle);
		if (handle && handle != INVALID_HANDLE_VALUE)
			::CloseHandle(handle);
		this->handle.windows_handle = INVALID_HANDLE_VALUE;
#endif
		break;
	}
	}
}

bool Logger::Sink::open(void) noexcept
{
	bool use_stderr;

	use_stderr = false;

	switch (this->target)
	{

	case Logger::SinkTarget::STDERR:
		use_stderr = true;
		[[fallthrough]];
	case Logger::SinkTarget::STDOUT:
		if (this->backend == Logger::SinkBackend::STDIO)
			this->handle.fp = use_stderr ? stderr : stdout;
		else if (this->backend == Logger::SinkBackend::POSIX)
			this->handle.fd = use_stderr ? STDERR_FILENO : STDOUT_FILENO;
		else if (this->backend == Logger::SinkBackend::WINDOWS)
		{
#if defined(_WIN32) || defined(_WIN64)
			this->handle.windows_handle = ::GetStdHandle(use_stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
			return this->handle.windows_handle != INVALID_HANDLE_VALUE;
#endif
		}
		return true;
	case Logger::SinkTarget::FILE:
		if (this->filepath.empty())
			return false;

		if (this->backend == Logger::SinkBackend::STDIO)
		{
			this->handle.fp = std::fopen(this->filepath.string().c_str(), "ab");
			return this->handle.fp;
		}
		else if (this->backend == Logger::SinkBackend::POSIX)
		{
			this->handle.fd = ::LOGGER_SYSCALL_OPEN(this->filepath.string().c_str(), LOGGER_OPEN_FLAG, LOGGER_OPEN_MODE);
			return this->handle.fd >= 0;
		}
		else if (this->backend == Logger::SinkBackend::WINDOWS)
		{
#if defined(_WIN32) || defined(_WIN64)
			this->handle.windows_handle = ::CreateFileW(
				this->filepath.wstring().c_str(),
				FILE_APPEND_DATA,
				FILE_SHARE_READ,
				NULL,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				NULL
			);
			return this->handle.windows_handle != INVALID_HANDLE_VALUE;
#endif
		}
		break;
	}
	return false;
}

std::size_t Logger::Sink::write(const char *buffer, std::size_t count) const noexcept
{
	if (!buffer || !count)
		return 0;

	switch (this->backend)
	{
	case Logger::SinkBackend::STDIO:
		if (this->handle.fp)
			return std::fwrite(buffer, 1, count, this->handle.fp);
		return 0;
	case Logger::SinkBackend::POSIX:
		if (this->handle.fd >= 0)
		{
			std::size_t chunk;
			std::size_t bytes_done;
			int bytes_written;

			bytes_done = 0;
			while (count)
			{
				chunk = std::min<std::size_t>(count, std::numeric_limits<int>::max());
				bytes_written = LOGGER_SYSCALL_WRITE(this->handle.fd, buffer + bytes_done, static_cast<unsigned int>(chunk));
				if (bytes_written <= 0)
					break;
				count -= static_cast<std::size_t>(bytes_written);
				bytes_done += static_cast<std::size_t>(bytes_written);;
			}
			return bytes_done;
		}
		return 0;
	case Logger::SinkBackend::WINDOWS:
	{
#if defined(_WIN32) || defined(_WIN64)
		HANDLE handle;

		handle = static_cast<HANDLE>(this->handle.windows_handle);
		if (handle && handle != INVALID_HANDLE_VALUE)
		{
			std::size_t chunk;
			std::size_t bytes_done;
			DWORD bytes_written;

			bytes_done = 0;
			while (count)
			{
				chunk = std::min<std::size_t>(count, std::numeric_limits<DWORD>::max());
				if (!WriteFile(
					handle,
					buffer + bytes_done,
					static_cast<DWORD>(chunk),
					&bytes_written,
					NULL
				))
					break;
				if (!bytes_written)
					break;
				count -= static_cast<std::size_t>(bytes_written);;
				bytes_done += static_cast<std::size_t>(bytes_written);;
			}
			return bytes_done;
		}
#endif
		return 0;
	}
	}
	return 0;
}

static const char *loglevel_to_cstr(Logger::LogLevel level) noexcept
{
	switch (level)
	{
	case Logger::LogLevel::Debug:
		return "DEBUG";
	case Logger::LogLevel::Info:
		return "INFO";
	case Logger::LogLevel::Warning:
		return "WARNING";
	case Logger::LogLevel::Error:
		return "ERROR";
	case Logger::LogLevel::Fatal:
		return "FATAL";
	default:
		assert(false && "Logger: Invalid LogLevel");
		return "UNKNOWN";
	}
}