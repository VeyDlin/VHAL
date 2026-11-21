# CommandStatusText

`Common/Utilities/CommandLine/CommandStatusText.h`: the name of each `ResultStatus`, for the standard handler's `CommandFailed` text.

```cpp
inline constexpr std::array<std::string_view, static_cast<std::size_t>(ResultStatus::high) + 1> CommandStatusNames = {{ "ok", "error", ... }};
```

The array is indexed by the status value, so it has one name for each status from `ok` to `high`: 161 entries, in order of `Utilities/Result/ResultStatus.h`. A failed command prints `<command>: <name>`, for example `key: invalidParameter`.

The table is `inline constexpr`, and only `CommandMessages.h` refers to it. A console whose set has its own `Default()` never calls that code, so the table is not linked, as the link check showed: a host build with a `Default()` set and no `Help` had no status name and no `unknown command` text.

A status added to `ResultStatus` makes the array one entry longer. The initializer then has fewer names than the array, and the missing ones are empty strings. Add the new name in the same change.
