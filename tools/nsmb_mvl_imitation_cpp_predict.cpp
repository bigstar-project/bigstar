#include "NsmbImitationAI.h"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

std::vector<std::string> ParseCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); i++)
    {
        const char ch = line[i];
        if (quoted)
        {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"')
            {
                field.push_back('"');
                i++;
            }
            else if (ch == '"')
            {
                quoted = false;
            }
            else
            {
                field.push_back(ch);
            }
        }
        else if (ch == '"')
        {
            quoted = true;
        }
        else if (ch == ',')
        {
            fields.push_back(field);
            field.clear();
        }
        else
        {
            field.push_back(ch);
        }
    }
    fields.push_back(field);
    return fields;
}

double ParseDouble(const std::string& value)
{
    if (value.empty())
        return 0.0;
    char* end = nullptr;
    return std::strtod(value.c_str(), &end);
}

int ParseInt(const std::string& value)
{
    if (value.empty())
        return 0;
    return static_cast<int>(ParseDouble(value));
}

std::string ButtonsText(std::uint16_t held, const std::vector<std::string>& buttons)
{
    std::string text;
    for (std::size_t i = 0; i < buttons.size() && i < 16; i++)
    {
        if ((held & (1u << i)) == 0)
            continue;
        if (!text.empty())
            text += "+";
        for (char ch : buttons[i])
            text.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return text.empty() ? "-" : text;
}

void PrintUsage()
{
    std::cerr
        << "usage: nsmb_mvl_imitation_cpp_predict <runtime-model.json> <dataset.csv> <output.csv>"
        << " [--threshold VALUE] [--limit N]\n";
}

}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        PrintUsage();
        return 2;
    }

    const std::string modelPath = argv[1];
    const std::string datasetPath = argv[2];
    const std::string outputPath = argv[3];
    double threshold = 0.5;
    int limit = 0;
    for (int i = 4; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--threshold" && i + 1 < argc)
            threshold = ParseDouble(argv[++i]);
        else if (arg == "--limit" && i + 1 < argc)
            limit = ParseInt(argv[++i]);
        else
        {
            PrintUsage();
            return 2;
        }
    }

    NsmbImitationAI::LinearPolicyModel model;
    std::string error;
    if (!NsmbImitationAI::LoadLinearPolicyModel(modelPath, model, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    std::ifstream dataset(datasetPath, std::ios::in);
    if (!dataset)
    {
        std::cerr << "failed to open dataset: " << datasetPath << "\n";
        return 1;
    }
    std::ofstream output(outputPath, std::ios::out);
    if (!output)
    {
        std::cerr << "failed to open output: " << outputPath << "\n";
        return 1;
    }

    std::string line;
    if (!std::getline(dataset, line))
    {
        std::cerr << "empty dataset: " << datasetPath << "\n";
        return 1;
    }
    const std::vector<std::string> header = ParseCsvLine(line);
    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < header.size(); i++)
        columns.emplace(header[i], i);

    std::vector<std::size_t> featureColumns;
    featureColumns.reserve(model.FeatureNames.size());
    for (const std::string& feature : model.FeatureNames)
    {
        auto it = columns.find(feature);
        if (it == columns.end())
        {
            std::cerr << "dataset is missing model feature: " << feature << "\n";
            return 1;
        }
        featureColumns.push_back(it->second);
    }

    output << "frame,player,label_held,label_text,pred_held,pred_text";
    for (const std::string& button : model.Buttons)
        output << ",prob_" << button;
    output << "\n";

    std::size_t rows = 0;
    std::size_t exactMatches = 0;
    std::size_t buttonMatches = 0;
    std::size_t buttonTotal = 0;
    const bool hasLabels = columns.find("label_held") != columns.end();

    while (std::getline(dataset, line))
    {
        if (line.empty())
            continue;
        const std::vector<std::string> fields = ParseCsvLine(line);
        std::vector<double> features;
        features.reserve(featureColumns.size());
        for (std::size_t index : featureColumns)
            features.push_back(index < fields.size() ? ParseDouble(fields[index]) : 0.0);

        const auto prediction = NsmbImitationAI::PredictLinearPolicy(model, features, threshold);
        const int labelHeld = hasLabels ? ParseInt(fields[columns["label_held"]]) : 0;

        if (hasLabels)
        {
            bool exact = true;
            for (std::size_t button = 0; button < model.Buttons.size() && button < 16; button++)
            {
                const bool predicted = (prediction.Held & (1u << button)) != 0;
                const bool expected = (labelHeld & (1u << button)) != 0;
                if (predicted == expected)
                    buttonMatches++;
                else
                    exact = false;
                buttonTotal++;
            }
            if (exact)
                exactMatches++;
        }

        const auto getField = [&](const char* name) -> std::string {
            auto it = columns.find(name);
            if (it == columns.end() || it->second >= fields.size())
                return "0";
            return fields[it->second];
        };

        output
            << getField("frame") << ","
            << getField("player") << ","
            << labelHeld << ","
            << ButtonsText(static_cast<std::uint16_t>(labelHeld), model.Buttons) << ","
            << prediction.Held << ","
            << ButtonsText(prediction.Held, model.Buttons);
        output << std::fixed << std::setprecision(6);
        for (double probability : prediction.Probabilities)
            output << "," << probability;
        output << "\n";

        rows++;
        if (limit > 0 && static_cast<int>(rows) >= limit)
            break;
    }

    std::cout << "rows=" << rows << " output=" << outputPath;
    if (hasLabels && rows > 0)
    {
        const double buttonAcc = static_cast<double>(buttonMatches) / static_cast<double>(buttonTotal);
        const double exact = static_cast<double>(exactMatches) / static_cast<double>(rows);
        std::cout << " button_acc=" << std::fixed << std::setprecision(3) << buttonAcc
            << " exact=" << exact;
    }
    std::cout << "\n";
    return 0;
}
