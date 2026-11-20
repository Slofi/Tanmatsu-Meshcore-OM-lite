// SPDX-License-Identifier: MIT
// Host tests for the MeshCore @[Name] mentions (S516/EST-481). Real exit code: 0 = pass.
//
// What these pin, and why each can fail silently on the glass:
//   · the reply prefix must be the MeshCore apps' OWN form "@[Name] " (1,044 of OverMesh's 4,811 stored messages
//     carry it), cleaned exactly as OverMesh cleans it and writing nothing when the name cleans to nothing;
//   · Filip's position rule: "a@b" is never a mention, whatever follows it — so the finder's boundary test is
//     what makes an email address safe;
//   · the drawn form must hide the brackets WITHOUT changing the stored text, and its spans are what the drawer
//     colours: a wrong span paints the wrong word, an off-by-one cuts a UTF-8 letter in half;
//   · "mentions you" is exact: a longer name that starts with ours ("TNMT (SI)x") is NOT us, and "TNMT (SI)" as
//     a name with spaces and parentheses must still match the plain L2 form;
//   · the composer rules: brackets only at the position rule, Enter never sends while picking, one Backspace on
//     an empty "@[]" leaves a plain '@', and a picked name that does not fit is refused rather than cut;
//   · the picker's order (favourites, then radio, then the rest; most recently heard inside a group) and its
//     five-row bound.
#include <stdio.h>
#include <string.h>

#include "chat_mention_edit.h"
#include "chat_mentions.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                       \
      return;                                                             \
    }                                                                     \
  } while (0)

#define CHECK_EQ(got, want)                                                          \
  do {                                                                               \
    ++g_checks;                                                                      \
    const long g_ = static_cast<long>(got);                                          \
    const long w_ = static_cast<long>(want);                                         \
    if (g_ != w_) {                                                                  \
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,        \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

#define CHECK_STR(got, want)                                                        \
  do {                                                                              \
    ++g_checks;                                                                     \
    if (strcmp((got), (want)) != 0) {                                               \
      printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got), (want));   \
      ++g_failures;                                                                 \
      return;                                                                       \
    }                                                                               \
  } while (0)

// ---- the reply prefix --------------------------------------------------------------------------------------
void test_reply_prefix_is_the_meshcore_form() {
    char out[48];
    CHECK_EQ(om::ui::mentionReplyPrefix(out, sizeof(out), "Alpha"), 9);
    CHECK_STR(out, "@[Alpha] ");

    // OverMesh cleaning: [ ] CR LF removed, spaces trimmed from both ends.
    CHECK(om::ui::mentionReplyPrefix(out, sizeof(out), " [Al]pha\r\n ") > 0);
    CHECK_STR(out, "@[Alpha] ");

    // Nothing left after cleaning: NOTHING is written (never a bare "@[] ").
    CHECK_EQ(om::ui::mentionReplyPrefix(out, sizeof(out), "[]\r\n  "), 0);
    CHECK_STR(out, "");
    CHECK_EQ(om::ui::mentionReplyPrefix(out, sizeof(out), ""), 0);
    CHECK_EQ(om::ui::mentionReplyPrefix(out, sizeof(out), nullptr), 0);

    // A UTF-8 name keeps its exact bytes.
    CHECK(om::ui::mentionReplyPrefix(out, sizeof(out), "Čurek") > 0);
    CHECK_STR(out, "@[Čurek] ");

    // A buffer that cannot hold the whole prefix writes nothing at all (never a cut name).
    char small[8];
    CHECK_EQ(om::ui::mentionReplyPrefix(small, sizeof(small), "Alpha"), 0);
    CHECK_STR(small, "");
}

// ---- the token finder --------------------------------------------------------------------------------------
void test_token_position_rule() {
    om::ui::MentionToken t;

    CHECK(om::ui::mentionFind("@[Name] hello", 0, &t));
    CHECK_EQ(t.start, 0);
    CHECK_EQ(t.content, 2);
    CHECK_EQ(t.content_len, 4);
    CHECK_EQ(t.end, 7);

    CHECK(om::ui::mentionFind("hi @[Name]", 0, &t));
    CHECK_EQ(t.start, 3);

    // Filip's rule: an '@' inside a word is never a mention.
    CHECK(!om::ui::mentionFind("a@[b]", 0, &t));
    CHECK(!om::ui::mentionFind("hi@[Name]", 0, &t));
    CHECK(!om::ui::mentionFind("(@[x])", 0, &t));
    // …but a space immediately before the '@' makes the same token valid (the previous byte is what counts).
    CHECK(om::ui::mentionFind(" @[x]", 0, &t));
    CHECK_EQ(t.content_len, 1);
}

void test_token_odd_brackets() {
    om::ui::MentionToken t;
    CHECK(!om::ui::mentionFind("@[abc", 0, &t));        // unclosed: not a token
    CHECK(!om::ui::mentionFind("@[]", 0, &t));          // empty: names nobody
    CHECK(!om::ui::mentionFind("@[a[b]c]", 0, &t));     // nested '[': not a name
    CHECK(!om::ui::mentionFind("@[a]]", 0, &t) || t.content_len == 1);   // the FIRST ']' closes it
    if (om::ui::mentionFind("@[a]]", 0, &t)) {
        CHECK_EQ(t.end, 4);   // "@[a]" then a stray ']'
    }

    // An unclosed one does not hide a later well-formed token.
    CHECK(om::ui::mentionFind("@[abc and @[Ok]", 0, &t));
    CHECK_EQ(t.start, 10);
    CHECK_EQ(t.content_len, 2);

    // Two tokens: the second search starts at the first one's end.
    CHECK(om::ui::mentionFind("@[A] @[B]", 0, &t));
    CHECK_EQ(t.start, 0);
    CHECK(om::ui::mentionFind("@[A] @[B]", t.end, &t));
    CHECK_EQ(t.start, 5);
    CHECK_EQ(t.content_len, 1);
}

void test_token_utf8_name() {
    om::ui::MentionToken t;
    const char* text = "@[Čurek] zdravo";
    CHECK(om::ui::mentionFind(text, 0, &t));
    CHECK_EQ(t.content_len, 6);   // Č is two bytes
    CHECK(strncmp(text + t.content, "Čurek", 6) == 0);
}

// ---- mentionsUs --------------------------------------------------------------------------------------------
void test_mentions_us_bracket_form() {
    const char* self = "TNMT (SI)";
    CHECK(om::ui::mentionsUs("@[TNMT (SI)] hello", self));
    CHECK(om::ui::mentionsUs("hi @[tnmt (si)]", self));       // case-insensitive
    CHECK(!om::ui::mentionsUs("@[TNMT (SI)x] hello", self));  // exact name only
    CHECK(!om::ui::mentionsUs("@[TNMT (SI)]", "TNMT (SI) ")); // the other way round: spacing matters
    CHECK(!om::ui::mentionsUs("a@[TNMT (SI)]", self));        // position rule
    CHECK(!om::ui::mentionsUs("@[Other] @[TNMT (SI)X]", self));   // a longer name in the bracket is not us
}

void test_mentions_us_plain_form() {
    const char* self = "TNMT (SI)";
    CHECK(om::ui::mentionsUs("@TNMT (SI) hi", self));
    CHECK(om::ui::mentionsUs("@tnmt (si), hi", self));   // the L2's separator list
    CHECK(om::ui::mentionsUs("@TNMT (SI)", self));       // end of text
    CHECK(om::ui::mentionsUs("@TNMT (SI)!", self));
    CHECK(!om::ui::mentionsUs("@TNMT (SI)x", self));     // a longer name is not us
    CHECK(!om::ui::mentionsUs("a@TNMT (SI)", self));     // position rule
    CHECK(!om::ui::mentionsUs("mail: x@TNMT (SI)", self));

    // A later plain mention still counts after a bracket mention that is not us.
    CHECK(om::ui::mentionsUs("@[Other] @TNMT (SI) hi", self));

    CHECK(!om::ui::mentionsUs("nothing here", self));
    CHECK(!om::ui::mentionsUs("@TNMT (SI) hi", ""));     // no name: nobody
    CHECK(!om::ui::mentionsUs(nullptr, self));
}

// ---- the drawn form ----------------------------------------------------------------------------------------
void test_drawn_form() {
    char             out[200];
    om::ui::MentionSpan spans[om::ui::kMentionMaxSpans];

    om::ui::MentionDrawn r = om::ui::mentionDrawn("hi @[Alpha] there", out, sizeof(out), spans,
                                                   om::ui::kMentionMaxSpans);
    CHECK_STR(out, "hi @Alpha there");
    CHECK_EQ(r.len, strlen("hi @Alpha there"));
    CHECK_EQ(r.spans, 1);
    CHECK_EQ(spans[0].start, 3);
    CHECK_EQ(spans[0].len, 6);   // "@Alpha"

    // Position rule: a mid-word token is NOT transformed.
    r = om::ui::mentionDrawn("a@[b] c", out, sizeof(out), spans, om::ui::kMentionMaxSpans);
    CHECK_STR(out, "a@[b] c");
    CHECK_EQ(r.spans, 0);

    // Unclosed and empty stay literal.
    r = om::ui::mentionDrawn("@[abc and @[]", out, sizeof(out), spans, om::ui::kMentionMaxSpans);
    CHECK_STR(out, "@[abc and @[]");
    CHECK_EQ(r.spans, 0);

    // Two tokens, two spans.
    r = om::ui::mentionDrawn("@[A] and @[Bee]", out, sizeof(out), spans, om::ui::kMentionMaxSpans);
    CHECK_STR(out, "@A and @Bee");
    CHECK_EQ(r.spans, 2);
    CHECK_EQ(spans[0].start, 0);
    CHECK_EQ(spans[0].len, 2);
    CHECK_EQ(spans[1].start, 7);
    CHECK_EQ(spans[1].len, 4);

    // UTF-8: the span is in DRAWN bytes and covers the whole name.
    r = om::ui::mentionDrawn("@[Čurek] jo", out, sizeof(out), spans, om::ui::kMentionMaxSpans);
    CHECK_STR(out, "@Čurek jo");
    CHECK_EQ(spans[0].len, 7);   // '@' + 6 bytes

    // No span buffer: the string is still the drawn one.
    r = om::ui::mentionDrawn("@[A] x", out, sizeof(out), nullptr, 0);
    CHECK_STR(out, "@A x");
    CHECK_EQ(r.spans, 0);

    // A short cap stops on a UTF-8 boundary and says so.
    char tiny[5];
    r = om::ui::mentionDrawn("ččč", tiny, sizeof(tiny), nullptr, 0);
    CHECK(r.truncated);
    CHECK_STR(tiny, "čč");   // 4 bytes + NUL fits exactly; the third whole letter does not
}

// ---- the composer: '@' -------------------------------------------------------------------------------------
void test_at_opens_only_at_the_position_rule() {
    char   buf[16];
    size_t len = 0;
    bool   active = false;
    buf[0] = '\0';

    CHECK(om::ui::mentionTypeAt(buf, &len, sizeof(buf), &active) == om::ui::MentionAt::Opened);
    CHECK(active);
    CHECK_EQ(len, 3);
    CHECK_STR(buf, "@[]");
    CHECK_EQ(om::ui::mentionOpenIndex(buf, len), 1);

    // Mid-word: a plain '@', no brackets, no mode.
    strcpy(buf, "ab");
    len    = 2;
    active = false;
    CHECK(om::ui::mentionTypeAt(buf, &len, sizeof(buf), &active) == om::ui::MentionAt::Plain);
    CHECK(!active);
    CHECK_STR(buf, "ab@");

    // After a space: brackets.
    strcpy(buf, "hi ");
    len    = 3;
    active = false;
    CHECK(om::ui::mentionTypeAt(buf, &len, sizeof(buf), &active) == om::ui::MentionAt::Opened);
    CHECK_STR(buf, "hi @[]");
    CHECK_EQ(len, 6);

    // No room for "@[]": the '@' is plain (never half a mention).
    char   tight[4] = "hi";
    len             = 2;
    active          = false;
    CHECK(om::ui::mentionTypeAt(tight, &len, sizeof(tight), &active) == om::ui::MentionAt::Plain);
    CHECK_STR(tight, "hi@");
    CHECK(!active);
}

// ---- the composer: letters, ']', '[' ----------------------------------------------------------------------
void test_letters_and_the_closing_bracket() {
    char   buf[16] = "@[]";
    size_t len     = 3;
    bool   active  = true;

    CHECK(om::ui::mentionTypeChar(buf, &len, sizeof(buf), &active, 'a') == om::ui::MentionType::Inserted);
    CHECK(om::ui::mentionTypeChar(buf, &len, sizeof(buf), &active, 'l') == om::ui::MentionType::Inserted);
    CHECK_STR(buf, "@[al]");
    CHECK_EQ(len, 5);

    // A nested '[' is refused, so the tail stays one clean mention.
    CHECK(om::ui::mentionTypeChar(buf, &len, sizeof(buf), &active, '[') == om::ui::MentionType::Ignored);
    CHECK_STR(buf, "@[al]");

    // ']' steps OVER the auto bracket: no "]]".
    CHECK(om::ui::mentionTypeChar(buf, &len, sizeof(buf), &active, ']') == om::ui::MentionType::SteppedOver);
    CHECK(!active);
    CHECK_STR(buf, "@[al]");

    // The query is what was typed between the brackets.
    char q[16];
    strcpy(buf, "@[alp]");
    len    = 6;
    active = true;
    CHECK_EQ(om::ui::mentionQuery(buf, len, q, sizeof(q)), 3);
    CHECK_STR(q, "alp");

    // A full buffer ignores the letter (the composer's byte limit, unchanged).
    char   full[5] = "@[a]";
    len            = 4;
    active         = true;
    CHECK(om::ui::mentionTypeChar(full, &len, sizeof(full), &active, 'b') == om::ui::MentionType::Ignored);
    CHECK_STR(full, "@[a]");
}

// ---- the composer: Enter / Right / Backspace --------------------------------------------------------------
void test_enter_with_a_suggestion() {
    char   buf[32] = "hi @[al]";
    size_t len     = 8;
    bool   active  = true;

    CHECK(om::ui::mentionPick(buf, &len, sizeof(buf), &active, "[Alpha]\r\n") == om::ui::MentionPick::Picked);
    CHECK(!active);
    CHECK_STR(buf, "hi @[Alpha] ");
    CHECK_EQ(len, strlen("hi @[Alpha] "));

    // A name that does not fit is REFUSED, the draft and the mode stay exactly as they were.
    char   tight[12] = "hi @[al]";
    len              = 8;
    active           = true;
    CHECK(om::ui::mentionPick(tight, &len, sizeof(tight), &active, "A-Very-Long-Name") ==
          om::ui::MentionPick::TooLong);
    CHECK(active);
    CHECK_STR(tight, "hi @[al]");
}

void test_enter_without_a_suggestion_is_right_arrow() {
    char buf[16]  = "@[al]";
    bool active   = true;

    CHECK(om::ui::mentionLeave(&active));   // Enter with no suggestion / Right arrow
    CHECK(!active);
    CHECK_STR(buf, "@[al]");                 // everything typed stays, brackets included
    CHECK(!om::ui::mentionLeave(&active));   // already out: nothing to do
}

void test_backspace() {
    char   buf[16] = "@[]";
    size_t len     = 3;
    bool   active  = true;

    // An EMPTY "@[]": both brackets go, a plain '@' stays, the mode ends.
    CHECK(om::ui::mentionBackspace(buf, &len, &active) == om::ui::MentionBack::RemovedBrackets);
    CHECK(!active);
    CHECK_STR(buf, "@");
    CHECK_EQ(len, 1);

    // Inside a name: one character (whole UTF-8 sequence) goes.
    strcpy(buf, "@[č]");
    len    = 5;
    active = true;
    CHECK(om::ui::mentionBackspace(buf, &len, &active) == om::ui::MentionBack::RemovedChar);
    CHECK(active);
    CHECK_STR(buf, "@[]");
    CHECK_EQ(len, 3);

    strcpy(buf, "@[ab]");
    len    = 5;
    active = true;
    CHECK(om::ui::mentionBackspace(buf, &len, &active) == om::ui::MentionBack::RemovedChar);
    CHECK_STR(buf, "@[a]");
}

// ---- the picker's ranking ----------------------------------------------------------------------------------
om::ui::MentionRow row(const char* name, bool fav, bool radio, uint32_t heard) {
    om::ui::MentionRow r;
    snprintf(r.name, sizeof(r.name), "%s", name);
    r.favourite = fav;
    r.in_radio  = radio;
    r.heard_at  = heard;
    return r;
}

void test_ranking() {
    // Substring and case.
    CHECK(om::ui::mentionMatches("Alpha", "alp"));
    CHECK(om::ui::mentionMatches("Alpha", "ALP"));
    CHECK(om::ui::mentionMatches("Alpha", ""));       // nothing typed: everything is a suggestion
    CHECK(!om::ui::mentionMatches("Alpha", "alpz"));
    CHECK(om::ui::mentionMatches("Čurek", "Ču"));     // UTF-8 bytes match exactly
    CHECK(om::ui::mentionMatches("Alpha", nullptr));   // no query at all = no filter

    om::ui::MentionRow rows[om::ui::kMentionPickRows];
    int                n = 0;

    // Favourites first, then radio, then the rest; most recently heard inside each group.
    om::ui::mentionInsert(rows, &n, om::ui::kMentionPickRows, row("Old-fav", true, false, 10));
    om::ui::mentionInsert(rows, &n, om::ui::kMentionPickRows, row("Radio-new", false, true, 900));
    om::ui::mentionInsert(rows, &n, om::ui::kMentionPickRows, row("Plain-newest", false, false, 1000));
    om::ui::mentionInsert(rows, &n, om::ui::kMentionPickRows, row("Radio-old", false, true, 100));
    om::ui::mentionInsert(rows, &n, om::ui::kMentionPickRows, row("New-fav", true, true, 500));

    CHECK_EQ(n, 5);
    CHECK_STR(rows[0].name, "New-fav");       // favourite, newest
    CHECK_STR(rows[1].name, "Old-fav");       // favourite, older
    CHECK_STR(rows[2].name, "Radio-new");     // radio, newest
    CHECK_STR(rows[3].name, "Radio-old");     // radio, older
    CHECK_STR(rows[4].name, "Plain-newest");  // plain, even though it is the newest of all

    // The bound: a sixth candidate that belongs in the middle pushes the LAST row out.
    const int at = om::ui::mentionInsert(rows, &n, om::ui::kMentionPickRows, row("Newer-radio", false, true, 2000));
    CHECK_EQ(at, 2);
    CHECK_EQ(n, 5);
    CHECK_STR(rows[2].name, "Newer-radio");
    CHECK_STR(rows[4].name, "Radio-old");     // "Plain-newest" is gone: the list is a top 5

    // A worse candidate does not make the list.
    CHECK_EQ(om::ui::mentionInsert(rows, &n, om::ui::kMentionPickRows, row("Worst", false, false, 1)), -1);
    CHECK_EQ(n, 5);

    // The same name twice is one row, and the better-flagged copy keeps it.
    om::ui::MentionRow dup[om::ui::kMentionPickRows];
    int                dn = 0;
    om::ui::mentionInsert(dup, &dn, om::ui::kMentionPickRows, row("Alpha", false, false, 5));
    om::ui::mentionInsert(dup, &dn, om::ui::kMentionPickRows, row("alpha", true, false, 9));
    CHECK_EQ(dn, 1);
    CHECK(dup[0].favourite);
    CHECK_EQ(dup[0].heard_at, 9);
}

}  // namespace

int main() {
    test_reply_prefix_is_the_meshcore_form();
    test_token_position_rule();
    test_token_odd_brackets();
    test_token_utf8_name();
    test_mentions_us_bracket_form();
    test_mentions_us_plain_form();
    test_drawn_form();
    test_at_opens_only_at_the_position_rule();
    test_letters_and_the_closing_bracket();
    test_enter_with_a_suggestion();
    test_enter_without_a_suggestion_is_right_arrow();
    test_backspace();
    test_ranking();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
