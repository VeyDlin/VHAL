#pragma once
#include "Print.h"
#include "Manipulator/HexManipulator.h"
#include "Manipulator/IndentManipulator.h"
#include "Manipulator/NumberManipulator.h"
#include "Manipulator/SeparatorManipulator.h"
#include "Manipulator/TimestampManipulator.h"
#include <functional>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>

class System;


class Console : public Print {
private:
    std::function<void(const char* string, size_t size)> writeHandle;
    std::function<int()> readHandle;
    Format consoleFormat = Format::Dec;


protected:
    virtual void WriteRaw(const char* string, size_t size) override {
        if (writeHandle) {
            writeHandle(string, size);
        }
    }


public:
    inline void SetWriteHandler(std::function<void(const char* string, size_t size)> handler) {
        writeHandle = handler;
    }


    inline void SetReadHandler(std::function<int()> handler) {
        readHandle = handler;
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
    	Write(number, consoleFormat);
        return *this;
    }


    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, Console&>
    operator<<(T number) {
    	Write(number);
        return *this;
    }


    Console& operator<<(const char* value) {
        Write(std::string_view(value));
        return *this;
    }


    Console& operator<<(const std::string_view& value) {
        Write(value);
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
        WriteBuffer(hex.getData(), hex.getSize());
        return *this;
    }


    template<typename T>
    Console& operator<<(const NumberManipulator<T>& num) {
        Write(num.getNumber(), num.getFormat());
        return *this;
    }


    Console& operator<<(const TimestampManipulator& ts) {
        WriteTimestamp(ts.getFormat());
        return *this;
    }


    Console& operator<<(const SeparatorManipulator& sep) {
        WriteLine(sep.getSeparator());
        return *this;
    }


    Console& operator<<(const IndentManipulator& indent) {
        for (size_t i = 0; i < indent.getSpaces(); ++i) {
            Write(" ");
        }
        return *this;
    }


    static Console& endl(Console& console) {
    	console.Line();
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


    static TimestampManipulator timestamp(TimestampManipulator::Format format) {
        return TimestampManipulator(format);
    }


    static Console& timestamp(Console& console) {
    	console.WriteTimestamp();
    	return console;
    }


    static SeparatorManipulator separator(const char* sep = nullptr) {
        return SeparatorManipulator(sep);
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
        Write(prefix);
        Write(" ");
        WriteTimestamp();
        if(message) {
            WriteLine(message);
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

        Write("[");

        // For Auto format, add days/months/years if needed
        if (format == TimestampManipulator::Format::Auto && totalDays > 0) {
            if (totalDays >= 365) {
                uint64 years = totalDays / 365;
                uint64 remainingDays = totalDays % 365;
                Write(years);
                Write("y ");
                if (remainingDays >= 30) {
                    uint64 months = remainingDays / 30;
                    remainingDays = remainingDays % 30;
                    Write(months);
                    Write("m ");
                }
                if (remainingDays > 0) {
                    Write(remainingDays);
                    Write("d ");
                }
            } else if (totalDays >= 30) {
                uint64 months = totalDays / 30;
                uint64 remainingDays = totalDays % 30;
                Write(months);
                Write("m ");
                if (remainingDays > 0) {
                    Write(remainingDays);
                    Write("d ");
                }
            } else {
                Write(totalDays);
                Write("d ");
            }
        }

        // Write time in HH:MM:SS or HH:MM:SS:MS format
        char timeBuffer[20];
        size_t len = 0;

        // Hours
        if (hours < 10) timeBuffer[len++] = '0';
        len += NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, hours, Format::Dec);
        timeBuffer[len++] = ':';

        // Minutes
        if (minutes < 10) timeBuffer[len++] = '0';
        len += NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, minutes, Format::Dec);
        timeBuffer[len++] = ':';

        // Seconds
        if (seconds < 10) timeBuffer[len++] = '0';
        len += NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, seconds, Format::Dec);

        // Milliseconds (if requested)
        if (format == TimestampManipulator::Format::HMSM || format == TimestampManipulator::Format::Auto) {
            timeBuffer[len++] = ':';
            if (ms < 100) timeBuffer[len++] = '0';
            if (ms < 10) timeBuffer[len++] = '0';
            len += NumberToString(&timeBuffer[len], sizeof(timeBuffer) - len, ms, Format::Dec);
        }

        timeBuffer[len] = '\0';
        Write(timeBuffer);
        Write("] ");
    }
};
