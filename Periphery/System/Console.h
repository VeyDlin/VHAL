#pragma once
#include <Utilities/Print.h>
#include <functional>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>


class System;

class Console {
private:
    class ConsolePrint : public Print {
    public:
        std::function<void(const std::string_view str)> writeHandle;
    protected:
        virtual void WriteRaw(const char* string, size_t size) override {
            if (writeHandle != nullptr) {
                writeHandle(std::string_view(string, size));
            }
        }
    };

    ConsolePrint print;
    std::function<void(const std::string_view str)> writeHandle;
    std::function<int()> readHandle;

public:



public:
    inline void SetWriteHandler(std::function<void(const std::string_view str)> handler) {
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


    inline size_t Write(bool value) {
        return print.Write(value);
    }


    inline size_t WriteLine(bool value) {
        return print.Write(value);
    }


    inline size_t Write(uint8_t* buffer, size_t size) {
        return print.Write(buffer, size);
    }


    inline size_t WriteLine(uint8_t* buffer, size_t size) {
        return print.WriteLine(buffer, size);
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
			return static_cast<T>(StrToInt(buffer, pos));
		} else if constexpr (std::is_floating_point_v<T>) {
			return static_cast<T>(StrToFloat(buffer, pos));
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
    Console& operator<<(const T& value) {
        print.Write(value);
        return *this;
    }


    Console& operator<<(const char* value) {
        print.Write(value);
        return *this;
    }


    Console& operator<<(const std::string_view& value) {
        print.Write(value);
        return *this;
    }


    Console& operator<<(Console& (*manip)(Console&)) {
        return manip(*this);
    }


    static Console& endl(Console& console) {
        console.print.Line();
        return console;
    }


    static Console& tick(Console& console) {
        console.WriteTick();
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
        if (readHandle != nullptr) {
            return readHandle();
        }
        return EOF;
    }


    void WriteWithPrefixAndTick(const char* prefix, const char* message = nullptr) {
        auto tick = System::GetTick();
        print.Write(prefix);
        WriteTick();
        if(message != nullptr) {
            print.WriteLine(message);
        }
    }


    void WriteTick() {
        auto tick = System::GetTick();
        print.Write(" [");
        print.Write(tick);
        print.Write("] ");
    }


    static int StrToInt(const char* str, size_t& pos) {
        int result = 0;
        bool isNegative = false;
        pos = 0;

        while (str[pos] == ' ' || str[pos] == '\t') {
            ++pos;
        }

        if (str[pos] == '-') {
            isNegative = true;
            ++pos;
        } else if (str[pos] == '+') {
            ++pos;
        }

        while (str[pos] >= '0' && str[pos] <= '9') {
            int digit = str[pos] - '0';
            if (result > (std::numeric_limits<int>::max() - digit) / 10) {
                result = isNegative ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
                break;
            }
            result = result * 10 + digit;
            ++pos;
        }

        return isNegative ? -result : result;
    }


    static double StrToFloat(const char* str, size_t& pos) {
        double result = 0.0;
        double fraction = 0.1;
        bool isNegative = false;
        bool isFractional = false;
        pos = 0;

        while (str[pos] == ' ' || str[pos] == '\t') {
            ++pos;
        }

        if (str[pos] == '-') {
            isNegative = true;
            ++pos;
        } else if (str[pos] == '+') {
            ++pos;
        }

        while ((str[pos] >= '0' && str[pos] <= '9') || str[pos] == '.') {
            if (str[pos] == '.') {
                isFractional = true;
                ++pos;
                continue;
            }

            int digit = str[pos] - '0';

            if (!isFractional) {
                result = result * 10.0 + digit;
            } else {
                result += digit * fraction;
                fraction *= 0.1;
            }

            ++pos;
        }

        return isNegative ? -result : result;
    }
};
