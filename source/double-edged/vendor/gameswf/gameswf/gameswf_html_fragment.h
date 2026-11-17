// Text for unwrapped Flash htmlText fragments. Rich paragraphs keep their parser.
#ifndef GAMESWF_HTML_FRAGMENT_H
#define GAMESWF_HTML_FRAGMENT_H

#include <string>

namespace gameswf {
inline std::string html_fragment_text(const char* input)
{
    const std::string text(input);
    std::string visible;
    visible.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '<') {
            std::size_t end = i + 1;
            char quote = 0;
            for (; end < text.size(); ++end) {
                const char c = text[end];
                if (quote) { if (c == quote) quote = 0; }
                else if (c == '\'' || c == '"') quote = c;
                else if (c == '>') break;
            }
            // Typewriter animation often ends with '<', '<b' or '<br'.
            // Those are unfinished markup, not characters to display.
            if (end == text.size()) break;
            std::size_t name = i + 1;
            while (name < end && (text[name] == ' ' || text[name] == '\t')) ++name;
            const std::size_t start = name;
            while (name < end && ((text[name] >= 'a' && text[name] <= 'z') ||
                                  (text[name] >= 'A' && text[name] <= 'Z'))) ++name;
            if (name - start == 2 && (text[start] == 'b' || text[start] == 'B') &&
                (text[start + 1] == 'r' || text[start + 1] == 'R') &&
                (name == end || text[name] == '/' || text[name] == ' ' ||
                 text[name] == '\t' || text[name] == '\r' || text[name] == '\n'))
                visible += '\n';
            i = end + 1;
            continue;
        }
        if (text[i] == '&') {
            const std::size_t end = text.find(';', i + 1);
            if (end != std::string::npos && end - i <= 12) {
                const std::string entity = text.substr(i + 1, end - i - 1);
                unsigned code = 0;
                if (entity == "amp") code = '&';
                else if (entity == "lt") code = '<';
                else if (entity == "gt") code = '>';
                else if (entity == "quot") code = '"';
                else if (entity == "apos") code = '\'';
                else if (entity == "nbsp") code = ' ';
                else if (!entity.empty() && entity[0] == '#') {
                    std::size_t digit = 1;
                    unsigned base = 10;
                    if (digit < entity.size() && (entity[digit] == 'x' || entity[digit] == 'X')) {
                        base = 16; ++digit;
                    }
                    bool valid = digit < entity.size();
                    for (; digit < entity.size() && valid; ++digit) {
                        const char c = entity[digit];
                        unsigned value = c >= '0' && c <= '9' ? unsigned(c - '0') :
                            c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) :
                            c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10) : 16;
                        if (value >= base || code > (0x10ffffu - value) / base) valid = false;
                        else code = code * base + value;
                    }
                    if (!valid || code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu)) code = 0;
                }
                if (code) {
                    if (code < 0x80) visible += char(code);
                    else if (code < 0x800) {
                        visible += char(0xc0 | (code >> 6)); visible += char(0x80 | (code & 63));
                    } else if (code < 0x10000) {
                        visible += char(0xe0 | (code >> 12)); visible += char(0x80 | ((code >> 6) & 63));
                        visible += char(0x80 | (code & 63));
                    } else {
                        visible += char(0xf0 | (code >> 18)); visible += char(0x80 | ((code >> 12) & 63));
                        visible += char(0x80 | ((code >> 6) & 63)); visible += char(0x80 | (code & 63));
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        visible += text[i++];
    }
    return visible;
}
} // namespace gameswf
#endif
