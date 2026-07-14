#ifndef NSMBINPUTTIMELINE_H
#define NSMBINPUTTIMELINE_H

#include "NsmbNetplayPoC.h"

#include <istream>
#include <string>
#include <vector>

namespace NsmbNetplayPoC::InputTimeline
{

struct InputSpan
{
    int Instance = -1;
    melonDS::u32 Start = 0;
    melonDS::u32 End = 0;
    InputState Input;
};

enum class ParseErrorKind
{
    None,
    Open,
    Target,
    Range,
    Input,
    Touch,
};

struct ParseError
{
    ParseErrorKind Kind = ParseErrorKind::None;
    int Line = 0;
};

bool ParseInputSpec(const std::string& spec, InputState& input);
bool ParseInputScript(std::istream& input, std::vector<InputSpan>& spans, ParseError& error);
bool LoadInputScriptFile(
    const std::string& path,
    std::vector<InputSpan>& spans,
    ParseError& error);
InputState Apply(
    const std::vector<InputSpan>& spans,
    int instanceID,
    melonDS::u32 frame,
    const InputState& fallback);

}

#endif
