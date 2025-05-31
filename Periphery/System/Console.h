#pragma once
#include <Utilities/Print.h>
#include <functional>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>

class System;


class HexManipulator {
private:
    uint8* data;
    size_t size;
public:
    HexManipulator(uint8* data, size_t size) : data(data), size(size) {}
    
    uint8* getData() const { return data; }
    size_t getSize() const { return size; }
};



template<typename T>
class NumberManipulator {
private:
    T number;
    Print::Format format;
public:
    NumberManipulator(T number, Print::Format format) : number(number), format(format) {}
    
    T getNumber() const { return number; }
    Print::Format getFormat() const { return format; }
};



class TimestampManipulator {
public:
    enum class Format {
        HMS,        // HH:MM:SS
        HMSM,       // HH:MM:SS:MS
        Auto        // Automatically add days/months/years if needed
    };
    
private:
    Format format;
    
public:
    TimestampManipulator(Format fmt = Format::Auto) : format(fmt) {}
    
    Format getFormat() const { return format; }
};



class SeparatorManipulator {
private:
    const char* separator;
public:
    SeparatorManipulator(const char* sep = "----------------------------------------") : separator(sep) {}
    
    const char* getSeparator() const { return separator; }
};



class IndentManipulator {
private:
    size_t spaces;
public:
    IndentManipulator(size_t spaces) : spaces(spaces) {}
    
    size_t getSpaces() const { return spaces; }
};



class Console {
private:
    class ConsolePrint : public Print {
    public:
        std::function<void(const char* string, size_t size)> writeHandle;
    protected:
        virtual void WriteRaw(const char* string, size_t size) override {
            if (writeHandle) {
                writeHandle(string, size);
            }
        }
    };

    ConsolePrint print;
    std::function<int()> readHandle;
    Print::Format consoleFormat = Print::Format::Dec;


public:
    inline void SetWriteHandler(std::function<void(const char* string, size_t size)> handler) {
        print.writeHandle = handler;
    }


    inline void SetReadHandler(std::function<int()> handler) {
        readHandle = handler;
    }


    template <typename T>
    std::enable_if_t<std::is_integral_v<T>, size_t>
    inline Write(T number, Print::Format format = Print::Format::Dec) {
        return print.Write(number, format);
    }


    template <typename T>
    std::enable_if_t<std::is_integral_v<T>, size_t>
    inline WriteLine(T number, Print::Format format = Print::Format::Dec) {
        return print.WriteLine(number, format);
    }


    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, size_t>
    inline Write(T number, uint8 precision = 2) {
        return print.Write(number, precision);
    }


    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, size_t>
    inline WriteLine(T number, uint8 precision = 2) {
        return print.WriteLine(number, precision);
    }


    inline size_t Write(const std::string_view str) {
        return print.Write(str);
    }


    inline size_t WriteLine(const std::string_view str) {
        return print.Write(str);
    }


    inline size_t Write(const char* value) {
    	return Write(std::string_view(value));
    }


    inline size_t WriteLine(const char* value) {
        return WriteLine(std::string_view(value));
    }


    inline size_t Write(const char* value, size_t size) {
    	return print.Write(value, size);
    }


    inline size_t Write(bool value) {
        return print.Write(value);
    }


    inline size_t WriteLine(bool value) {
        return print.Write(value);
    }


    inline size_t Write(uint8* buffer, size_t size) {
        return print.Write(buffer, size);
    }


    inline size_t WriteLine(uint8* buffer, size_t size) {
        return print.WriteLine(buffer, size);
    }


    inline size_t WriteBuffer(uint8* buffer, size_t size) {
        return print.WriteBuffer(buffer, size);
    }


    inline size_t WriteBufferLine(uint8* buffer, size_t size) {
        return print.WriteBufferLine(buffer, size);
    }


    inline size_t Line() {
    	return print.Line();
    }


    void Log(const char* level, const char* message) {
        WriteWithPrefixAndTick(level, message);
    }


    void Info(const char* message = nullptr) {
        Log("[INFO]", message);
    }


    void Debug(const char* message = nullptr) {
        Log("[DEBUG]", message);
    }


    void Error(const char* message = nullptr) {
        Log("[ERROR]", message);
    }


    size_t Read(char* buffer, size_t bufferSize) {
        size_t count = 0;
        int ch;
        while (count < bufferSize - 1 && (ch = ReadChar()) != EOF) {
            buffer[count++] = static_cast<char>(ch);
        }
        buffer[count] = '\0';
        return count;
    }


	template <typename T, size_t bufferSize = 32>
	T Read() {
		char buffer[bufferSize] = {0};
		size_t count = Read(buffer, sizeof(buffer));
		size_t pos = 0;

		if constexpr (std::is_integral_v<T>) {
			T result;
			Print::StringToNumber(buffer, result);
			return result;
		} else if constexpr (std::is_floating_point_v<T>) {
			T result;
			Print::StringToFloat(buffer, result);
			return result;
		}

		return T{};
	}


    size_t ReadLine(char* buffer, size_t bufferSize) {
        size_t count = 0;
        int ch;
        while (count < bufferSize - 1 && (ch = ReadChar()) != EOF) {
            if (ch == '\n') {
                break;
            }
            buffer[count++] = static_cast<char>(ch);
        }
        buffer[count] = '\0';
        return count;
    }


    template <typename T>
    std::enable_if_t<std::is_integral_v<T>, Console&>
    operator<<(T number) {
    	print.Write(number, consoleFormat);
        return *this;
    }


    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, Console&>
    operator<<(T number) {
    	print.Write(number);
        return *this;
    }


    Console& operator<<(const char* value) {
        print.Write(std::string_view(value));
        return *this;
    }


    Console& operator<<(const std::string_view& value) {
        print.Write(value);
        return *this;
    }


    Console& operator<<(Print::Format format) {
        consoleFormat = format;
        return *this;
    }


    Console& operator<<(Console& (*manip)(Console&)) {
        return manip(*this);
    }


    Console& operator<<(const HexManipulator& hex) {
        print.WriteBuffer(hex.getData(), hex.getSize());
        return *this;
    }


    template<typename T>
    Console& operator<<(const NumberManipulator<T>& num) {
        print.Write(num.getNumber(), num.getFormat());
        return *this;
    }


    Console& operator<<(const TimestampManipulator& ts) {
        WriteTimestamp(ts.getFormat());
        return *this;
    }


    Console& operator<<(const SeparatorManipulator& sep) {
        print.WriteLine(sep.getSeparator());
        return *this;
    }


    Console& operator<<(const IndentManipulator& indent) {
        for (size_t i = 0; i < indent.getSpaces(); ++i) {
            print.Write(" ");
        }
        return *this;
    }


    static Console& endl(Console& console) {
        console.print.Line();
        return console;
    }


    static HexManipulator hex(uint8* data, size_t size) {
        return HexManipulator(data, size);
    }


    template<typename T>
    static NumberManipulator<T> hex(T number) {
        return NumberManipulator<T>(number, Print::Format::Hex);
    }


    template<typename T>
    static NumberManipulator<T> dec(T number) {
        return NumberManipulator<T>(number, Print::Format::Dec);
    }


    template<typename T>
    static NumberManipulator<T> bin(T number) {
        return NumberManipulator<T>(number, Print::Format::Bin);
    }


    template<typename T>
    static NumberManipulator<T> oct(T number) {
        return NumberManipulator<T>(number, Print::Format::Oct);
    }


    static TimestampManipulator timestamp(TimestampManipulator::Format format = TimestampManipulator::Format::HMSM) {
        return TimestampManipulator(format);
    }


    static SeparatorManipulator separator(const char* sep = nullptr) {
        return SeparatorManipulator(sep ? sep : "----------------------------------------");
    }


    static IndentManipulator indent(size_t spaces) {
        return IndentManipulator(spaces);
    }


    static Console& tick(Console& console) {
        console.Write(System::GetTick());
        return console;
    }


    static Console& info(Console& console) {
        console.Info();
        return console;
    }


    static Console& debug(Console& console) {
        console.Debug();
        return console;
    }


    static Console& error(Console& console) {
        console.Error();
        return console;
    }


private:
    int ReadChar() {
        if (readHandle) {
            return readHandle();
        }
        return EOF;
    }


    void WriteWithPrefixAndTick(const char* prefix, const char* message = nullptr) {
        print.Write(prefix);
        print.Write(" ");
        WriteTimestamp();
        if(message) {
            print.WriteLine(message);
        }
    }

    
    void WriteTimestamp(TimestampManipulator::Format format = TimestampManipulator::Format::Auto) {
        uint64 totalMs = System::GetMs();
        
        // Calculate time components
        uint64 ms = totalMs % 1000;
        uint64 totalSeconds = totalMs / 1000;
        uint64 seconds = totalSeconds % 60;
        uint64 totalMinutes = totalSeconds / 60;
        uint64 minutes = totalMinutes % 60;
        uint64 totalHours = totalMinutes / 60;
        uint64 hours = totalHours % 24;
        uint64 totalDays = totalHours / 24;
        
        print.Write("[");
        
        // For Auto format, add days/months/years if needed
        if (format == TimestampManipulator::Format::Auto && totalDays > 0) {
            if (totalDays >= 365) {
                uint64 years = totalDays / 365;
                uint64 remainingDays = totalDays % 365;
                print.Write(years);
                print.Write("y ");
                if (remainingDays >= 30) {
                    uint64 months = remainingDays / 30;
                    remainingDays = remainingDays % 30;
                    print.Write(months);
                    print.Write("m ");
                }
                if (remainingDays > 0) {
                    print.Write(remainingDays);
                    print.Write("d ");
                }
            } else if (totalDays >= 30) {
                uint64 months = totalDays / 30;
                uint64 remainingDays = totalDays % 30;
                print.Write(months);
                print.Write("m ");
                if (remainingDays > 0) {
                    print.Write(remainingDays);
                    print.Write("d ");
                }
            } else {
                print.Write(totalDays);
                print.Write("d ");
            }
        }
        
        // Write time in HH:MM:SS or HH:MM:SS:MS format
        char timeBuffer[20];
        size_t len = 0;
        
        // Hours
        if (hours < 10) timeBuffer[len++] = '0';
        len += Print::NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, hours, Print::Format::Dec);
        timeBuffer[len++] = ':';
        
        // Minutes
        if (minutes < 10) timeBuffer[len++] = '0';
        len += Print::NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, minutes, Print::Format::Dec);
        timeBuffer[len++] = ':';
        
        // Seconds
        if (seconds < 10) timeBuffer[len++] = '0';
        len += Print::NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, seconds, Print::Format::Dec);
        
        // Milliseconds (if requested)
        if (format == TimestampManipulator::Format::HMSM || format == TimestampManipulator::Format::Auto) {
            timeBuffer[len++] = ':';
            if (ms < 100) timeBuffer[len++] = '0';
            if (ms < 10) timeBuffer[len++] = '0';
            len += Print::NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, ms, Print::Format::Dec);
        }
        
        timeBuffer[len] = '\0';
        print.Write(timeBuffer);
        print.Write("] ");
    }
};
