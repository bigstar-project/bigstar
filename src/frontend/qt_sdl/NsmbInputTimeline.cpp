#include "NsmbInputTimeline.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace NsmbNetplayPoC::InputTimeline
{

namespace
{

std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string Upper(std::string value)
{
    for (char& ch : value)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}

bool ParseU32(const std::string& text, melonDS::u32& out)
{
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 0);
    if (!end || *end != '\0')
        return false;
    out = static_cast<melonDS::u32>(value);
    return true;
}

int ButtonBit(const std::string& name)
{
    const std::string key = Upper(name);
    if (key == "A") return 0;
    if (key == "B") return 1;
    if (key == "SELECT") return 2;
    if (key == "START") return 3;
    if (key == "RIGHT") return 4;
    if (key == "LEFT") return 5;
    if (key == "UP") return 6;
    if (key == "DOWN") return 7;
    if (key == "R") return 8;
    if (key == "L") return 9;
    if (key == "X") return 10;
    if (key == "Y") return 11;
    return -1;
}

}

bool ParseInputSpec(const std::string& spec, InputState& input)
{
    input = {};

    if (spec.empty() || Upper(spec) == "NONE" || Upper(spec) == "NEUTRAL")
        return true;

    if (spec.rfind("mask=", 0) == 0 || spec.rfind("MASK=", 0) == 0)
    {
        melonDS::u32 mask = 0;
        if (!ParseU32(spec.substr(5), mask))
            return false;
        input.KeyMask = mask & 0xFFF;
        return true;
    }

    std::stringstream stream(spec);
    std::string button;
    while (std::getline(stream, button, '+'))
    {
        const int bit = ButtonBit(Trim(button));
        if (bit < 0)
            return false;
        input.KeyMask &= ~(1u << bit);
    }
    return true;
}

bool ParseInputScript(std::istream& input, std::vector<InputSpan>& spans, ParseError& error)
{
    spans.clear();
    error = {};

    std::string line;
    int lineNo = 0;
    while (std::getline(input, line))
    {
        lineNo++;
        const auto comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);
        line = Trim(line);
        if (line.empty())
            continue;

        std::stringstream stream(line);
        std::string target;
        std::string range;
        std::string buttons;
        std::string touch;
        stream >> target >> range >> buttons >> touch;

        InputSpan span;
        if (target.find('-') != std::string::npos)
        {
            touch = buttons;
            buttons = range;
            range = target;
        }
        else
        {
            const std::string upperTarget = Upper(target);
            if (upperTarget != "ALL")
            {
                constexpr const char* prefix = "INST";
                melonDS::u32 targetInstance = 0;
                if (upperTarget.rfind(prefix, 0) != 0 ||
                    !ParseU32(upperTarget.substr(4), targetInstance) ||
                    targetInstance >= 16)
                {
                    error = { ParseErrorKind::Target, lineNo };
                    return false;
                }
                span.Instance = static_cast<int>(targetInstance);
            }
        }

        const auto dash = range.find('-');
        if (dash == std::string::npos)
        {
            error = { ParseErrorKind::Range, lineNo };
            return false;
        }
        if (!ParseU32(range.substr(0, dash), span.Start) ||
            !ParseU32(range.substr(dash + 1), span.End) ||
            span.End < span.Start ||
            !ParseInputSpec(buttons, span.Input))
        {
            error = { ParseErrorKind::Input, lineNo };
            return false;
        }

        if (!touch.empty())
        {
            const auto comma = touch.find(',');
            melonDS::u32 x = 0;
            melonDS::u32 y = 0;
            if (comma == std::string::npos ||
                !ParseU32(touch.substr(0, comma), x) ||
                !ParseU32(touch.substr(comma + 1), y))
            {
                error = { ParseErrorKind::Touch, lineNo };
                return false;
            }
            span.Input.Touching = true;
            span.Input.TouchX = static_cast<melonDS::u16>(std::min<melonDS::u32>(x, 255));
            span.Input.TouchY = static_cast<melonDS::u16>(std::min<melonDS::u32>(y, 191));
        }
        spans.push_back(span);
    }
    return true;
}

bool LoadInputScriptFile(
    const std::string& path,
    std::vector<InputSpan>& spans,
    ParseError& error)
{
    std::ifstream file(path);
    if (!file)
    {
        spans.clear();
        error = { ParseErrorKind::Open, 0 };
        return false;
    }
    return ParseInputScript(file, spans, error);
}

InputState Apply(
    const std::vector<InputSpan>& spans,
    int instanceID,
    melonDS::u32 frame,
    const InputState& fallback)
{
    for (const InputSpan& span : spans)
    {
        if ((span.Instance < 0 || span.Instance == instanceID) &&
            frame >= span.Start && frame <= span.End)
        {
            return span.Input;
        }
    }
    return fallback;
}

}
