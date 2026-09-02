#include "text_transaction.h"

#include <cassert>
#include <string>

int main() {
    const std::string word = "thê";
    assert(composition_matches_surrounding(word, word.size(), word.size(), word));
    assert(!composition_matches_surrounding("other", 5, 5, word));

    // Regression: "thê" -> "thể" must not split a UTF-8 codepoint.
    const std::string toned = "thể";
    const size_t common = utf8_common_prefix_bytes(word, toned);
    assert(common == 2);
    assert(word.size() - common == 2);
    assert(toned.substr(common) == "ể");

    const auto plain = make_surrounding_replacement(2, word, 4, 4);
    assert(plain.uses_surrounding);
    assert(plain.index == -2);
    assert(plain.length == 2);

    const std::string autocomplete = "thê suggestion";
    const auto forward = make_surrounding_replacement(2, autocomplete, 4, autocomplete.size());
    assert(forward.uses_surrounding);
    assert(forward.index == -2);
    assert(forward.length == autocomplete.size() - 2);

    const auto reverse = make_surrounding_replacement(2, autocomplete, autocomplete.size(), 4);
    assert(reverse.uses_surrounding);
    assert(reverse.index == 2 - static_cast<int32_t>(autocomplete.size()));
    assert(reverse.length == autocomplete.size() - 2);

    // A Direct Commit replacement is two compositor transactions. If the
    // delete is dropped, "noi" -> "nói" becomes "noiói".
    const std::string plain_word = "noi";
    const std::string vietnamese_word = "nói";
    const size_t word_common = utf8_common_prefix_bytes(plain_word, vietnamese_word);
    const auto replace_tone = make_surrounding_replacement(
        plain_word.size() - word_common, plain_word, plain_word.size(), plain_word.size());
    const auto expected = apply_surrounding_replacement(
        plain_word, plain_word.size(), plain_word.size(),
        replace_tone, vietnamese_word.substr(word_common));
    const auto insert_only = make_surrounding_replacement(
        0, plain_word, plain_word.size(), plain_word.size());
    const auto dropped_delete = apply_surrounding_replacement(
        plain_word, plain_word.size(), plain_word.size(),
        insert_only, vietnamese_word.substr(word_common));
    assert(expected.valid && expected.text == vietnamese_word);
    assert(dropped_delete.valid && dropped_delete.text == "noiói");
    assert(surrounding_matches(expected, vietnamese_word,
                               vietnamese_word.size(), vietnamese_word.size()));
    assert(!surrounding_matches(expected, dropped_delete.text,
                                dropped_delete.cursor, dropped_delete.anchor));

    // The recovery replaces the complete failed tail from a fresh snapshot.
    const auto repair = make_surrounding_replacement(
        dropped_delete.text.size(), dropped_delete.text,
        dropped_delete.cursor, dropped_delete.anchor);
    const auto repaired = apply_surrounding_replacement(
        dropped_delete.text, dropped_delete.cursor, dropped_delete.anchor,
        repair, vietnamese_word);
    assert(repaired.valid && repaired.text == vietnamese_word);

    // Deletion-only edits are also distinguishable from a dropped delete.
    const std::string shortened = "nó";
    const size_t backspace_common = utf8_common_prefix_bytes(vietnamese_word, shortened);
    const auto backspace = make_surrounding_replacement(
        vietnamese_word.size() - backspace_common,
        vietnamese_word, vietnamese_word.size(), vietnamese_word.size());
    const auto backspace_expected = apply_surrounding_replacement(
        vietnamese_word, vietnamese_word.size(), vietnamese_word.size(), backspace, "");
    assert(backspace_expected.valid && backspace_expected.text == shortened);

    return 0;
}
