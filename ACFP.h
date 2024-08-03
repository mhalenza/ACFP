// Copyright (c) 2024 Matt M Halenza
// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace ACFP {

template <typename T>
struct Parser
{
    /* static T parse(std::string_view); */
};

template <>
struct Parser<bool>
{
    static bool parse(std::string_view sv)
    {
        if (sv.size() > 0) {
            switch (sv[0]) {
                case '0':
                case 'f':
                case 'n':
                case 'F':
                case 'N':
                    return false;
                case '1':
                case 't':
                case 'y':
                case 'T':
                case 'Y':
                    return true;
            }
        }
        throw std::domain_error(std::format("Could not parse '{}' as bool", sv));
    }
};

template <typename T>
    requires std::integral<T> || std::floating_point<T>
struct Parser<T>
{
    static T parse(std::string_view sv)
    {
        T v;
        auto const [ptr, ec] = std::from_chars(sv.begin(), sv.end(), v);
        if (ec == std::errc::result_out_of_range)
            throw std::range_error(std::format("String '{}' not representible in type {}", sv, typeid(T).name()));
        if (ec == std::errc::invalid_argument)
            throw std::invalid_argument(std::format("String '{}' is not a valid {}", sv, typeid(T).name()));
        if (ec != std::errc{})
            throw std::runtime_error(std::format("Unknown error while parsing '{}' as a {}", sv, typeid(T).name()));
        return v;
    }
};

template <typename T>
inline
T parse(std::string_view sv)
{
    return Parser<T>::parse(sv);
}

template <typename T>
inline
std::optional<T> parse(std::optional<std::string_view> osv)
{
    if (osv.has_value()) {
        return parse<T>(osv.value());
    }
    return std::nullopt;
}

class Section final
{
public:
    bool hasField(std::string_view key) const
    {
        return this->fields.count(std::string{ key }) != 0;
    }
    std::optional<std::string_view> getField(std::string_view key) const
    {
        auto it = this->fields.find(std::string{ key });
        if (it == this->fields.end())
            return std::nullopt;
        else
            return std::string_view{ it->second };
    }
    template <typename T>
    std::optional<T> getFieldAs(std::string_view key) const
    {
        return parse<T>(this->getField(key));
    }
    void setField(std::string_view key, std::string_view value)
    {
        this->fields[std::string{ key }] = std::string{ value };
    }
    std::optional<std::string_view> operator[](std::string_view key) const
    {
        return this->getField(key);
    }
    void iterate(std::function<void(std::string_view, std::string_view)> cb) const
    {
        for (auto const& kv : this->fields) {
            cb(kv.first, kv.second);
        }
    }
private:
    std::unordered_map<std::string, std::string> fields;
};

class SectionGroup final
{
public:
    bool hasSubsection(std::string_view subkey) const
    {
        return this->sections.count(std::string{ subkey }) != 0;
    }
    Section const& getSubsection(std::string_view subkey) const
    {
        return this->operator[](subkey);
    }
    Section& getSubsection(std::string_view subkey)
    {
        return this->sections[std::string{ subkey }];
    }
    Section const& operator[](std::string_view subkey) const
    {
        static const Section empty_section;
        auto it = this->sections.find(std::string{ subkey });
        if (it == this->sections.end())
            return empty_section;
        else
            return it->second;
    }
private:
    std::unordered_map<std::string, Section> sections;
};

class ConfigTable final
{
public:
    bool hasSection(std::string_view key) const
    {
        return this->groups.count(std::string{ key }) != 0;
    }
    SectionGroup const& getSection(std::string_view key) const
    {
        return this->operator[](key);
    }
    SectionGroup& getSection(std::string_view key)
    {
        return this->groups[std::string{ key }];
    }
    SectionGroup const& operator[](std::string_view key) const
    {
        static const SectionGroup empty_section_group;
        auto it = this->groups.find(std::string{ key });
        if (it == this->groups.end())
            return empty_section_group;
        else
            return it->second;
    }
private:
    std::unordered_map<std::string, SectionGroup> groups;
};

inline
void trimStringViewEnds(std::string_view& sv, std::string_view const trim_chars = std::string_view{ " \t" })
{
    auto fnos = sv.find_first_not_of(trim_chars);
    if (fnos != std::string_view::npos)
        sv.remove_prefix(fnos);
    auto lnos = sv.find_last_not_of(trim_chars);
    if (lnos != std::string_view::npos)
        sv.remove_suffix(sv.size() - (lnos + 1));
}
inline
std::size_t findFirstNotQuoted(std::string_view sv, char ch)
{
    bool quoted = false;
    bool escaped = false;

    for (std::size_t p = 0 ; p < sv.size() ; p++) {
        if (sv[p] == '\\') {
            escaped = !escaped;
        }
        else if (sv[p] == '"') {
            if (quoted) {
                if (!escaped)
                    quoted = false;
            }
            else {
                if (!escaped)
                    quoted = true;
            }
            escaped = false;
        }
        else if (sv[p] == ch) {
            if (!escaped && !quoted)
                return p;
        }
    }
    return std::string_view::npos;
}

inline
void trimStringComment(std::string_view& sv)
{
    auto p = sv.find_first_of("#/");
    if (p != std::string_view::npos) {
        if (sv[p] == '#')
            sv.remove_suffix(sv.size() - p);
        else if (sv[p] == '/' && sv.size() > p + 1 && sv[p+1] == '/') {
            sv.remove_suffix(sv.size() - p);
        }
    }
}
inline
void trimStringQuotes(std::string_view& sv, uint16_t line_num, char front = '"', char back = '"')
{
    if (sv.front() == front) {
        sv.remove_prefix(1);
        if (sv.size() == 0 || sv.back() != back) {
            throw std::runtime_error(std::format("Unfished quoted string on line {}: '{}'", line_num, sv));
        }
        sv.remove_suffix(1);
    }
}
inline
std::size_t findEqPos(std::string_view line)
{
    return findFirstNotQuoted(line, '=');
}

inline
ConfigTable parseConfigFile(std::istream& is)
{
    ConfigTable ct;

    auto* cur_section = &ct.getSection("").getSubsection("");
    std::string line_string;
    for (uint32_t line_num = 1 ; std::getline(is, line_string) ; line_num++) {
        std::string_view line = line_string;
        // Trim Spaces from ends
        trimStringViewEnds(line);
        // Remove comments
        trimStringComment(line);
        // Skip empty lines
        if (line.size() == 0)
            continue;
        // Figure out what kind of line this is
        if (line.front() == '[') {
            // Section start
            trimStringQuotes(line, line_num, '[', ']');
            auto const sep = findFirstNotQuoted(line, ' ');
            if (sep == std::string_view::npos) {
                // Singleton Section
                auto const section_name = line;
                cur_section = &ct.getSection(section_name).getSubsection("");
            }
            else {
                auto section_name = line.substr(0, sep);
                trimStringViewEnds(section_name);
                trimStringQuotes(section_name, line_num);
                auto section_subname = line.substr(sep + 1, std::string_view::npos);
                trimStringViewEnds(section_subname);
                trimStringQuotes(section_subname, line_num);
                cur_section = &ct.getSection(section_name).getSubsection(section_subname);
            }
        }
        else {
            // Key/Value
            auto const eq_pos = findEqPos(line);
            if (eq_pos == std::string_view::npos)
                throw std::runtime_error(std::format("Malformed line on line {}: '{}'", line_num, line));
            auto key = line.substr(0, eq_pos);
            trimStringViewEnds(key);
            trimStringQuotes(key, line_num);
            auto value = line.substr(eq_pos + 1, std::string_view::npos);
            trimStringViewEnds(value);
            trimStringQuotes(value, line_num);

            cur_section->setField(std::string{ key }, std::string{ value });
        }
    }

    return ct;
}
inline
ConfigTable  parseConfigFile(std::filesystem::path filename)
{
    std::ifstream ifs;
    ifs.exceptions(std::ios_base::badbit);
    ifs.open(filename);
    return parseConfigFile(ifs);
}

}
