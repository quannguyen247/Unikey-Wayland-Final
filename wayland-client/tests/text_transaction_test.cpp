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

    return 0;
}
