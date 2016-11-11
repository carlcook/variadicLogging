#include <iostream>
#include <sstream>
#include <string>
#include <cstring>

struct BaseLogFormatter
{
  virtual ~BaseLogFormatter() = default;
  virtual std::ostringstream& Evaluate(const char* data, std::ostringstream&) const = 0;
};

// Knows how to format stored arguments, writing to an output stream
// Each instance of this class maps to a unique sequence of log arguments
template <typename ...T>
struct LogFormatter : BaseLogFormatter
{
  LogFormatter(const char* formatString) : mFormatString(formatString) {}
  const char* mFormatString;

  template<typename Arg>
  const char* FormatArg(std::ostringstream& outputStream, const char* argsData) const
  {
        const Arg* arg = reinterpret_cast<const Arg*>(argsData);
        outputStream << *arg;
        return argsData + sizeof(Arg);
  }

  template<typename ... Args>
  typename std::enable_if<sizeof...(Args) == 0>::type
  Format(std::ostringstream& outputStream, const char* formatString, const char*) const
  {
          outputStream << formatString;
  }

  template<typename Arg, typename ... Args>
  void Format(auto& outputStream, const char* formatString, const char* argsData) const
  {
    const char* firstPlaceholder = std::strstr(formatString, "%");
    // write from format to first placeholder
    outputStream.write(formatString, firstPlaceholder - formatString);

    // write corresponding argument
    argsData = FormatArg<Arg>(outputStream, argsData);

    // Move to the next data item
    Format<Args...>(outputStream, firstPlaceholder + 1, argsData);
  }

  // given input data, write to stream
  std::ostringstream& Evaluate(const char* argsData, std::ostringstream& outputStream) const override
  {
    Format<T...>(outputStream, mFormatString, argsData);
    return outputStream;
  }
};

// Knows how to write log data to a memory stream, given the static data
struct LogWriter
{
  struct Header
  {
    // the log data (i.e. args)
    char mBuffer[128];

    // the object which knows how to format this data
    const BaseLogFormatter* mLogFormatter;
  };

  static LogWriter& GetLogWriter()
  {
    static LogWriter logWriter;
    return logWriter;
  }

  // return a new object which knows how to store data
  template<typename T, typename ...Args>
  T* CreateLogFormatter(Args&&... args)
  {
    return new T(std::forward<Args>(args)...);
  }

  // front end to write all arguments to a buffer
  template<typename ... Args>
  void Write(const BaseLogFormatter& logFormatter, const Args... args)
  {
    size_t argSize = GetArgsSize(args...);
    char* buffer = GetLogBuffer(logFormatter, argSize);
    CopyArgs(buffer, args...);
  }

  Header& GetNextHeader()
  {
    // incomplete
    static Header sHeader;
    return sHeader;
  }

  char* GetLogBuffer(const BaseLogFormatter& logFormatter, size_t sizeRequired)
  {
    // incomplete
    Header& header = GetNextHeader();
    header.mLogFormatter = &logFormatter;
    return header.mBuffer;
  }

  // Note: we could have an option for non-trivially copyable args (SFINAE)
  template <typename T>
  char* CopyArg(char* buffer, T arg)
  {
    memcpy(buffer, &arg, sizeof(arg));
    return buffer + sizeof(arg);
  }

  // base case for the format string
  inline char* CopyArgs(char* buffer)
  {
    return buffer; // nothing to copy here
  }

  // write a single arg to the buffer and continue with the tail
  template<typename Arg, typename ... Args>
  char* CopyArgs(char* buffer, const Arg& arg, const Args&... args)
  {
    buffer = CopyArg(buffer, arg);
    return CopyArgs(buffer, args...);
  }

  inline size_t GetArgsSize()
  {
    return 0;
  }

  template<typename Arg>
  size_t GetArgSize(const Arg& arg)
  {
    return sizeof(arg);
  }

  template<typename Arg, typename... Args>
  size_t GetArgsSize(const Arg& arg, const Args... args)
  {
    return GetArgSize(arg) + GetArgsSize(args...);
  }
};

// front-end method to write a log entry
template <typename... Args>
void WriteLog(BaseLogFormatter** logFormatter, const char* formatString, const Args&... args)
{
  LogWriter logWriter = LogWriter::GetLogWriter();

  // find the object that knows how to write the args to the log buffer
  if (*logFormatter == nullptr)
    *logFormatter = logWriter.CreateLogFormatter<LogFormatter<Args...>>(formatString);

  // write the args to the log buffer
  logWriter.Write(**logFormatter, args...);
}

// Knows how to consume a record from the producer, given the static data
struct LogConsumer
{
  static LogConsumer& GetLogConsumer()
  {
    static LogConsumer logConsumer;
    return logConsumer;
  }

  void Consume(const LogWriter::Header& header, std::ostringstream& outputStream)
  {
    header.mLogFormatter->Evaluate(header.mBuffer, outputStream);
  }
};

// util
template <typename... Types>
constexpr unsigned sizeof_args(Types&&...)
{
  return sizeof...(Types);
}

// util
constexpr size_t CountPlaceholders(const char* formatString)
{
  if (formatString[0] == '\0')
    return 0;
  return (formatString[0] == '%' ? 1u : 0u) + CountPlaceholders(formatString + 1);
}

// API
#define LOG(formatString, ...) \
  static_assert(CountPlaceholders(formatString) == sizeof_args(__VA_ARGS__), "Number of arguments mismatch"); \
  static BaseLogFormatter* sLogFormatter = nullptr; \
  WriteLog(&sLogFormatter, formatString, ##__VA_ARGS__);

int main()
{
  LOG("Hello int=% char=% float=%", 1, 'a', 42.3);

  std::ostringstream outputStream;
  LogConsumer::GetLogConsumer().Consume(LogWriter::GetLogWriter().GetNextHeader(), outputStream);
  std::cout << outputStream.str() << std::endl;
}
