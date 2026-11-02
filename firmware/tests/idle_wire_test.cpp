#include <apps/common/idle_program/wire.h>
#include <array>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace idle_program;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
const std::string weekly = "SC_IDLE {\"v\":1,\"type\":\"weekly\",\"available\":true,\"sequence\":1700000000,"
    "\"remaining_percent\":37,\"reset_at\":1700003600,\"observed_at\":1700000000,\"ttl_ms\":900000}";

std::string replace(std::string value, const std::string& from, const std::string& to)
{
    const auto at = value.find(from);
    check(at != std::string::npos, "invalid test replacement");
    value.replace(at, from.size(), to);
    return value;
}

bool parse(const std::string& text, wire::Message& message, const Config& config = {})
{
    return wire::parse(text.data(), text.size(), config, message);
}

void complete_weekly_and_unavailable_shapes()
{
    wire::Message message;
    check(parse(weekly, message) && message.kind == wire::Kind::Weekly && message.weekly.available &&
              message.weekly.sequence == 1700000000 && message.weekly.remaining_percent == 37 &&
              message.weekly.ttl_ms == 900000, "valid weekly frame did not parse");
    check(parse("SC_IDLE {\"type\":\"weekly\",\"v\":1,\"sequence\":2,\"available\":false}", message) &&
              !message.weekly.available, "explicit unavailable shape rejected");
    check(!parse("SC_IDLE {\"v\":1,\"type\":\"weekly\",\"available\":false}", message), "missing sequence accepted");
    check(!parse("SC_IDLE {\"v\":1,\"type\":\"weekly\",\"available\":false,\"sequence\":2,\"remaining_percent\":0}", message),
          "unavailable frame carried stale percentage fields");
    for (const auto value : {"0", "100"}) check(parse(replace(weekly, "\"remaining_percent\":37", std::string("\"remaining_percent\":") + value), message), "percent boundary rejected");
}

void exact_types_and_integer_ranges()
{
    wire::Message message;
    const std::array bad{
        replace(weekly, "\"v\":1", "\"v\":true"), replace(weekly, "\"v\":1", "\"v\":2"),
        replace(weekly, "\"available\":true", "\"available\":1"),
        replace(weekly, "\"sequence\":1700000000", "\"sequence\":false"),
        replace(weekly, "\"sequence\":1700000000", "\"sequence\":4294967296"),
        replace(weekly, "\"sequence\":1700000000", "\"sequence\":01"),
        replace(weekly, "\"remaining_percent\":37", "\"remaining_percent\":NaN"),
        replace(weekly, "\"remaining_percent\":37", "\"remaining_percent\":Infinity"),
        replace(weekly, "\"remaining_percent\":37", "\"remaining_percent\":37.0"),
        replace(weekly, "\"remaining_percent\":37", "\"remaining_percent\":1e1"),
        replace(weekly, "\"remaining_percent\":37", "\"remaining_percent\":-1"),
        replace(weekly, "\"remaining_percent\":37", "\"remaining_percent\":101"),
        replace(weekly, "\"ttl_ms\":900000", "\"ttl_ms\":\"900000\"")};
    for (const auto& input : bad) check(!parse(input, message), "bad JSON field type or numeric range accepted");
}

void ttl_reset_and_required_fields()
{
    wire::Message message;
    for (const auto value : {"60000", "3600000"}) check(parse(replace(weekly, "\"ttl_ms\":900000", std::string("\"ttl_ms\":") + value), message), "TTL boundary rejected");
    for (const auto& input : {
        replace(weekly, "\"ttl_ms\":900000", "\"ttl_ms\":59999"),
        replace(weekly, "\"ttl_ms\":900000", "\"ttl_ms\":3600001"),
        replace(weekly, "\"reset_at\":1700003600", "\"reset_at\":1700000000"),
        replace(weekly, "\"reset_at\":1700003600", "\"reset_at\":1700691201"),
        replace(weekly, "\"observed_at\":1700000000", "\"observed_at\":0"),
        replace(weekly, ",\"ttl_ms\":900000", "")}) {
        check(!parse(input, message), "invalid TTL/reset or missing required field accepted");
    }
}

void duplicate_unknown_trailing_and_embedded_values()
{
    wire::Message message;
    for (const auto& input : {
        replace(weekly, "{\"v\":1", "{\"v\":1,\"v\":1"),
        replace(weekly, "{\"v\":1", "{\"v\":1,\"extra\":0"),
        replace(weekly, "\"available\":true", "\"available\":truefalse"),
        replace(weekly, "\"type\":\"weekly\"", "\"type\":\"config\""),
        weekly + "garbage", weekly + "{}", replace(weekly, "SC_IDLE ", "SC_IDLE"),
        replace(weekly, "\"sequence\":1700000000", "\"sequence\":null"),
        replace(weekly, "\"sequence\":1700000000", "\"sequence\":[1]"),
        replace(weekly, "\"sequence\":1700000000", "\"sequence\":{\"x\":1}")}) {
        check(!parse(input, message), "duplicate/unknown/trailing or nested JSON accepted");
    }
    auto nul = weekly;
    nul.insert(nul.size() - 1, 1, '\0');
    check(!parse(nul, message), "embedded NUL accepted");
}

void partial_configuration_and_explicit_save()
{
    Config initial;
    initial.pet_gap_ms = 45000;
    wire::Message message;
    check(parse("SC_IDLE {\"v\":1,\"type\":\"config\",\"eye_interval_ms\":20000,\"rest_duration_ms\":20000,\"save\":false}", message, initial),
          "debug partial configuration rejected");
    check(message.kind == wire::Kind::Config && !message.save && message.config.eye_interval_ms == 20000 &&
              message.config.rest_duration_ms == 20000 && message.config.pet_gap_ms == 45000,
          "partial config overwrote untouched fields or requested implicit save");
    check(parse("SC_IDLE {\"v\":1,\"type\":\"config\",\"enabled\":false}", message) && !message.save && !message.config.enabled,
          "toggle-only configuration or default no-save rejected");
    check(parse("SC_IDLE {\"v\":1,\"type\":\"config\",\"save\":true}", message) && message.save,
          "explicit save rejected");
    for (const auto text : {
        "SC_IDLE {\"v\":1,\"type\":\"config\",\"save\":1}",
        "SC_IDLE {\"v\":1,\"type\":\"config\",\"enabled\":0}",
        "SC_IDLE {\"v\":1,\"type\":\"config\",\"eye_interval_ms\":999}",
        "SC_IDLE {\"v\":1,\"type\":\"config\",\"available\":false}",
        "SC_IDLE {\"v\":1,\"type\":\"config\",\"unknown\":0}"}) check(!parse(text, message), "bad configuration accepted");
}

void parser_does_not_mutate_output_on_rejection()
{
    wire::Message message;
    message.kind = wire::Kind::Config;
    message.save = true;
    message.config.eye_interval_ms = 12345;
    check(!parse(weekly + "x", message), "invalid test input accepted");
    check(message.kind == wire::Kind::Config && message.save && message.config.eye_interval_ms == 12345,
          "rejected frame partially mutated caller output");
    check(!wire::parse(nullptr, 512, {}, message), "null input accepted");
}

void bounded_lines_and_overflow_resynchronization()
{
    wire::Lines lines;
    auto padded = weekly;
    padded.resize(wire::kMaxFrameBytes, ' ');
    for (const auto value : padded) check(lines.push(value) == wire::LineResult::None, "incomplete frame emitted early");
    check(lines.push('\n') == wire::LineResult::Ready && lines.size() == 512, "512-byte boundary frame was not emitted");
    wire::Message message;
    check(wire::parse(lines.data(), lines.size(), {}, message), "exact maximum frame could not parse");
    for (std::size_t i = 0; i < 513; ++i) check(lines.push('x') == wire::LineResult::None, "overlong frame emitted partial content");
    for (const auto value : weekly) check(lines.push(value) == wire::LineResult::None, "overflow discarded prefix incorrectly");
    check(lines.push('\n') == wire::LineResult::TooLong, "overflow did not discard through the next newline");
    for (const auto value : weekly) lines.push(value);
    check(lines.push('\r') == wire::LineResult::None && lines.push('\n') == wire::LineResult::Ready,
          "CRLF frame did not recover after overflow");
    check(wire::parse(lines.data(), lines.size(), {}, message), "recovered CRLF frame rejected");
    check(lines.push('\n') == wire::LineResult::None, "empty line emitted a command");
}

} // namespace
int main()
{
    const std::array tests{complete_weekly_and_unavailable_shapes, exact_types_and_integer_ranges,
        ttl_reset_and_required_fields, duplicate_unknown_trailing_and_embedded_values,
        partial_configuration_and_explicit_save, parser_does_not_mutate_output_on_rejection,
        bounded_lines_and_overflow_resynchronization};
    try { for (const auto test : tests) test(); }
    catch (const std::exception& error) { std::cerr << "idle_wire_test: " << error.what() << '\n'; return 1; }
    std::cout << "idle_wire_test: " << tests.size() << " bounded protocol groups passed\n";
    return 0;
}
