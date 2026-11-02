// report.h - field report forms (METHANE, ATMIST, SALUTE, SITREP) and their one-message summaries.
// Ported from MESHDECK 1.x.
#pragma once
#include <string>
#include <vector>

namespace meshdeck {

// Report forms and their one-message summaries for the mesh.
//
// Each form is a standard UK or common field mnemonic (METHANE from JESIP, ATMIST for casualty
// handover, SALUTE for observations, plus a plain SITREP). A summary keeps the mnemonic letters
// so the receiver can read it against the same card: "METHANE 21:12 M:YES E:SE 0108 5432 ...".
// Empty fields are left out. A summary longer than one mesh message is split at word boundaries
// into numbered parts, each within the limit.
struct ReportField {
    const char* key;     // letter(s) used in the summary
    const char* label;   // shown on the form
    const char* hint;    // placeholder text
    enum Auto { None, Location, Time } autofill;
};

struct ReportForm {
    const char* code;    // "METHANE"
    const char* title;   // "METHANE - MAJOR INCIDENT (JESIP)"
    std::vector<ReportField> fields;
};

const std::vector<ReportForm>& reportForms();

// "CODE HH:MM KEY:VALUE ..." (time omitted when empty). Values are upper-cased and trimmed,
// and newlines become spaces.
std::string reportSummary(const ReportForm& form, const std::vector<std::string>& values, const std::string& time);

// Splits text into parts of at most maxLen characters, breaking at spaces where possible and
// adding " (1/2)" style suffixes when there is more than one part.
std::vector<std::string> splitMessage(const std::string& text, size_t maxLen);

} // namespace meshdeck
