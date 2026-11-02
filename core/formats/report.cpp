// report.cpp - see report.h.
#include "formats/report.h"
#include <ctype.h>
#include <stdio.h>

namespace meshdeck {

const std::vector<ReportForm>& reportForms() {
    using F = ReportField;
    static const std::vector<ReportForm> forms = {
        {"SITREP", "SITREP - SITUATION REPORT", {
            {"WHO", "WHO (CALLSIGN / NAMES)", "E.G. TD01, 3 ADULTS", F::None},
            {"AT", "WHERE (GRID REF)", "NO POSITION SET: TYPE A GRID REF", F::Location},
            {"STATUS", "STATUS", "E.G. OK / DELAYED / NEED HELP", F::None},
            {"NEEDS", "NEEDS", "E.G. WATER, MEDS, NONE", F::None},
            {"NEXT", "NEXT CHECK-IN", "E.G. 22:00", F::None},
            {"NOTE", "NOTES", "ANYTHING ELSE", F::None},
        }},
        {"METHANE", "METHANE - MAJOR INCIDENT (JESIP)", {
            {"M", "M  MAJOR INCIDENT DECLARED?", "YES / NO", F::None},
            {"E", "E  EXACT LOCATION", "NO POSITION SET: TYPE A GRID REF", F::Location},
            {"T", "T  TYPE OF INCIDENT", "E.G. FLOOD, FIRE, COLLISION", F::None},
            {"H", "H  HAZARDS", "PRESENT AND POTENTIAL", F::None},
            {"A", "A  ACCESS", "ROUTES IN AND OUT", F::None},
            {"N", "N  NUMBER OF CASUALTIES", "NUMBER, TYPE, SEVERITY", F::None},
            {"E", "E  EMERGENCY SERVICES", "PRESENT AND REQUIRED", F::None},
        }},
        {"ATMIST", "ATMIST - CASUALTY HANDOVER", {
            {"A", "A  AGE / SEX", "E.G. ADULT M ~40", F::None},
            {"T", "T  TIME OF INCIDENT", "HH:MM", F::Time},
            {"M", "M  MECHANISM", "WHAT HAPPENED", F::None},
            {"I", "I  INJURIES", "FOUND OR SUSPECTED", F::None},
            {"S", "S  SIGNS", "BREATHING, PULSE, RESPONSE", F::None},
            {"T", "T  TREATMENT GIVEN", "SO FAR", F::None},
        }},
        {"SALUTE", "SALUTE - OBSERVATION", {
            {"S", "S  SIZE", "HOW MANY", F::None},
            {"A", "A  ACTIVITY", "WHAT THEY ARE DOING", F::None},
            {"L", "L  LOCATION", "NO POSITION SET: TYPE A GRID REF", F::Location},
            {"U", "U  UNIT / IDENTITY", "WHO, CLOTHING, MARKINGS", F::None},
            {"T", "T  TIME SEEN", "HH:MM", F::Time},
            {"E", "E  EQUIPMENT", "VEHICLES, KIT", F::None},
        }},
    };
    return forms;
}

static std::string clean(const std::string& in) {
    std::string out;
    for (char c : in) {
        char ch = (c == '\n' || c == '\r' || c == '\t') ? ' ' : (char)toupper((unsigned char)c);
        if (ch == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += ch;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string reportSummary(const ReportForm& form, const std::vector<std::string>& values, const std::string& time) {
    std::string s = form.code;
    if (!time.empty()) s += " " + time;
    for (size_t i = 0; i < form.fields.size() && i < values.size(); i++) {
        std::string v = clean(values[i]);
        if (!v.empty()) s += std::string(" ") + form.fields[i].key + ":" + v;
    }
    return s;
}

std::vector<std::string> splitMessage(const std::string& text, size_t maxLen) {
    if (text.size() <= maxLen) return {text};
    // Reserve room for " (nn/nn)" and cut at the last space that fits.
    const size_t room = maxLen - 8;
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = pos + room;
        if (end >= text.size()) end = text.size();
        else {
            size_t sp = text.rfind(' ', end);
            if (sp != std::string::npos && sp > pos + room / 2) end = sp;
        }
        std::string part = text.substr(pos, end - pos);
        while (!part.empty() && part.back() == ' ') part.pop_back();
        parts.push_back(part);
        pos = end;
        while (pos < text.size() && text[pos] == ' ') pos++;
    }
    for (size_t i = 0; i < parts.size(); i++) {
        char suffix[16];
        snprintf(suffix, sizeof suffix, " (%u/%u)", (unsigned)(i + 1), (unsigned)parts.size());
        parts[i] += suffix;
    }
    return parts;
}

} // namespace meshdeck
