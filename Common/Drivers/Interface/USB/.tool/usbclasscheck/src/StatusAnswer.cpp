#include "StatusAnswer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Text.h"

namespace {

// Reads a decimal number of one to ten digits. Anything else fails.
bool ParseDecimal(const std::string& text, std::uint32_t& value) {
	if (text.empty() || text.size() > 10) {
		return false;
	}

	std::uint64_t result = 0;
	for (char digit : text) {
		if (digit < '0' || digit > '9') {
			return false;
		}

		result = result * 10 + static_cast<std::uint64_t>(digit - '0');
	}

	if (result > 0xFFFFFFFFULL) {
		return false;
	}

	value = static_cast<std::uint32_t>(result);
	return true;
}


bool ParseBool(const std::string& text, bool& value) {
	if (text == "true") {
		value = true;
		return true;
	}

	if (text == "false") {
		value = false;
		return true;
	}

	return false;
}


// The key=value words of every body line. A later word with the same key replaces the earlier one.
std::map<std::string, std::string> CollectFields(const std::vector<std::string>& body) {
	std::map<std::string, std::string> fields;
	for (const std::string& line : body) {
		std::size_t start = 0;
		while (start < line.size()) {
			std::size_t end = line.find(' ', start);
			if (end == std::string::npos) {
				end = line.size();
			}

			std::string word = line.substr(start, end - start);
			std::size_t equals = word.find('=');
			if (equals != std::string::npos) {
				fields[word.substr(0, equals)] = word.substr(equals + 1);
			}

			start = end + 1;
		}
	}

	return fields;
}


// Looks up a field. A missing field is reported by name.
bool RequireField(const std::map<std::string, std::string>& fields, const std::string& key, std::string& value, std::string& error) {
	std::map<std::string, std::string>::const_iterator found = fields.find(key);
	if (found == fields.end()) {
		error = "the status answer has no " + key + " field";
		return false;
	}

	value = found->second;
	return true;
}


bool RequireNumber(const std::map<std::string, std::string>& fields, const std::string& key, std::uint32_t& number, std::string& error) {
	std::string text;
	if (!RequireField(fields, key, text, error)) {
		return false;
	}

	if (!ParseDecimal(text, number)) {
		error = "the " + key + " field is not a number: '" + text + "'";
		return false;
	}

	return true;
}


bool RequireBool(const std::map<std::string, std::string>& fields, const std::string& key, bool& flag, std::string& error) {
	std::string text;
	if (!RequireField(fields, key, text, error)) {
		return false;
	}

	if (!ParseBool(text, flag)) {
		error = "the " + key + " field is not true or false: '" + text + "'";
		return false;
	}

	return true;
}


// The one-letter name of a parity, as used in the shorthand 8N1.
char ParityLetter(const std::string& parity) {
	if (parity == "odd") {
		return 'O';
	}

	if (parity == "even") {
		return 'E';
	}

	if (parity == "mark") {
		return 'M';
	}

	if (parity == "space") {
		return 'S';
	}

	return 'N';
}

}


bool AnswerComplete(const std::string& answer) {
	std::string ending = std::string("\n") + PromptText;
	return answer.size() >= ending.size() && answer.compare(answer.size() - ending.size(), ending.size(), ending) == 0;
}


std::vector<std::string> SplitLines(const std::string& text) {
	std::vector<std::string> lines;
	std::string current;
	// The byte that ended the line just before this one, or 0 when the previous byte was text. A CR LF or LF CR pair is one line end:
	// the second byte of the pair follows a line end byte of the other kind, and it does not start a line of its own.
	char previousEnd = 0;
	for (char character : text) {
		if (character == '\r' || character == '\n') {
			if (previousEnd != 0 && previousEnd != character) {
				previousEnd = 0;
				continue;
			}

			lines.push_back(current);
			current.clear();
			previousEnd = character;
			continue;
		}

		previousEnd = 0;
		current.push_back(character);
	}

	if (!current.empty()) {
		lines.push_back(current);
	}

	return lines;
}


bool CommandBody(
	const std::string& answer,
	const std::string& command,
	std::vector<std::string>& body,
	std::string& error)
{
	if (!AnswerComplete(answer)) {
		error = "no prompt after the answer to '" + command + "'";
		return false;
	}

	std::vector<std::string> lines = SplitLines(answer);
	if (lines.size() < 2) {
		error = "the answer to '" + command + "' is too short";
		return false;
	}

	// The prompt has no line end, so the echo of the next command follows it on the same line: "> status". A greeting that the
	// board prints when the terminal opens comes before the first echo and is not part of any answer.
	std::string prefixed = std::string(PromptText) + command;
	std::size_t echo = lines.size();
	for (std::size_t index = 0; index + 1 < lines.size(); index++) {
		if (lines[index] == command || lines[index] == prefixed) {
			echo = index;
		}
	}

	if (echo == lines.size()) {
		error = "the command line did not echo '" + command + "'";
		return false;
	}

	body.assign(lines.begin() + static_cast<std::ptrdiff_t>(echo) + 1, lines.end() - 1);
	return true;
}


bool ParseStatus(const std::vector<std::string>& body, CompositeStatus& status, std::string& error) {
	std::map<std::string, std::string> fields = CollectFields(body);

	std::string state;
	if (!RequireField(fields, "state", state, error)) {
		return false;
	}

	std::string parity;
	std::string stopBits;
	if (!RequireField(fields, "parity", parity, error) || !RequireField(fields, "stop", stopBits, error)) {
		return false;
	}

	CompositeStatus parsed;
	parsed.state = state;
	parsed.coding.parity = parity;
	parsed.coding.stopBits = stopBits;
	if (!RequireNumber(fields, "addr", parsed.address, error)
		|| !RequireBool(fields, "numLock", parsed.numLock, error)
		|| !RequireBool(fields, "capsLock", parsed.capsLock, error)
		|| !RequireBool(fields, "scrollLock", parsed.scrollLock, error)
		|| !RequireNumber(fields, "baud", parsed.coding.baud, error)
		|| !RequireNumber(fields, "data", parsed.coding.dataBits, error)
		|| !RequireBool(fields, "dtr", parsed.dtr, error)
		|| !RequireNumber(fields, "looped", parsed.looped, error)
		|| !RequireNumber(fields, "cdcDropped", parsed.cdcDropped, error)
		|| !RequireNumber(fields, "usbErr", parsed.usbErrors, error))
	{
		return false;
	}

	status = parsed;
	return true;
}


bool SameLineCoding(const LineCoding& reported, const LineCoding& expected) {
	return reported.baud == expected.baud
		&& reported.dataBits == expected.dataBits
		&& reported.parity == expected.parity
		&& reported.stopBits == expected.stopBits;
}


std::string DescribeLineCoding(const LineCoding& coding) {
	std::string text;
	AppendFormat(
		text,
		"%u %u%c%s",
		static_cast<unsigned int>(coding.baud),
		static_cast<unsigned int>(coding.dataBits),
		ParityLetter(coding.parity),
		coding.stopBits.c_str());
	return text;
}
