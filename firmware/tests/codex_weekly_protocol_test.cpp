#include <apps/common/codex_weekly/protocol.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace codex_weekly::protocol;

namespace {

constexpr std::uint32_t now = 1700000000u;

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::string base64(std::string_view data)
{
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const auto a = static_cast<unsigned char>(data[i]);
        const auto b = i + 1 < data.size() ? static_cast<unsigned char>(data[i + 1]) : 0u;
        const auto c = i + 2 < data.size() ? static_cast<unsigned char>(data[i + 2]) : 0u;
        result += alphabet[a >> 2u];
        result += alphabet[((a & 3u) << 4u) | (b >> 4u)];
        if (i + 1 < data.size()) result += alphabet[((b & 15u) << 2u) | (c >> 6u)];
        if (i + 2 < data.size()) result += alphabet[c & 63u];
    }
    return result;
}

std::string jwt(std::string_view payload)
{
    return "eyJhbGciOiJSUzI1NiJ9." + base64(payload) + ".c2ln";
}

std::string identity(std::string_view account = "fixture-account")
{
    return jwt("{\"exp\":1699999999,\"https://api.openai.com/auth\":{\"chatgpt_account_id\":\"" +
               std::string(account) + "\"}}");
}

std::string token_response(std::string_view access = "opaque-access", std::string_view refresh = "fixture-refresh")
{
    return "{\"access_token\":\"" + std::string(access) + "\",\"refresh_token\":\"" +
           std::string(refresh) + "\",\"id_token\":\"" + identity() + "\"}";
}

std::string window(std::string_view used = "36", std::string_view seconds = "604800", std::string_view reset = "1700604800")
{
    return "{\"used_percent\":" + std::string(used) + ",\"limit_window_seconds\":" +
           std::string(seconds) + ",\"reset_at\":" + std::string(reset) + "}";
}

std::string usage(std::string_view primary, std::string_view secondary = "null")
{
    return "{\"rate_limit\":{\"primary_window\":" + std::string(primary) +
           ",\"secondary_window\":" + std::string(secondary) + "}}";
}

void rejected_device(std::string_view input)
{
    DeviceCode output{"keep-device", "keep-code", 17};
    check(!parse_device_code(input, output) && output.device_auth_id == "keep-device" &&
              output.user_code == "keep-code" && output.interval_seconds == 17,
          "invalid device response accepted or partially replaced output");
}

void rejected_tokens(std::string_view input, std::string_view old_refresh = "old-refresh", std::string_view old_account = "old-account")
{
    Tokens output{"keep-access", "keep-refresh", "keep-account", 42};
    check(!parse_tokens(input, now, old_refresh, old_account, output) && output.access_token == "keep-access" &&
              output.refresh_token == "keep-refresh" && output.account_id == "keep-account" && output.expires_at == 42,
          "invalid token response accepted or partially replaced output");
}

void rejected_usage(std::string_view input)
{
    Usage output{17, 42};
    check(!parse_usage(input, now, output) && output.remaining_percent == 17 && output.reset_at == 42,
          "invalid usage fabricated quota or partially replaced output");
}

void official_device_string_interval_and_alias()
{
    DeviceCode output;
    check(parse_device_code(R"({"device_auth_id":"fixture-id","user_code":"ABCD-EFGH","interval":"5","new_field":{"ok":true}})", output) &&
              output.device_auth_id == "fixture-id" && output.user_code == "ABCD-EFGH" && output.interval_seconds == 5,
          "official string interval device fixture was rejected");
    check(parse_device_code(R"({"device_auth_id":"fixture-id","usercode":"ABCD-EFGH"})", output) && output.interval_seconds == 5,
          "legacy alias or missing-interval local default failed");
    check(parse_device_code(R"({"device_auth_id":"fixture-id","user_code":"ABCD-EFGH","usercode":"ABCD-EFGH","interval":60})", output) &&
              output.interval_seconds == 60, "matching aliases or bounded integer interval failed");
    check(parse_device_code(R"({"device_auth_id":"fixture-id","user_code":"ABCD-EFGH","interval":" 5 "})", output),
          "official trimmed interval form was rejected");
    check(parse_device_code(R"({"device_auth_id":"fixture-id","user_code":"ABCD-EFGH","interval":"0005"})", output) &&
              output.interval_seconds == 5, "integer-string leading zeros were rejected despite a bounded numeric value");
}

void device_fields_and_numeric_types_are_strict()
{
    for (const auto interval : {"0", "61", "-1", "5.0", "true", "null", "\"5.5\"", "\"+5\"", "\"5s\"", "\"\"", "\"999999999999999999999\"", "4294967296"})
        rejected_device("{\"device_auth_id\":\"fixture-id\",\"user_code\":\"ABCD-EFGH\",\"interval\":" + std::string(interval) + "}");
    for (const auto input : {
             R"({"device_auth_id":true,"user_code":"ABCD-EFGH"})",
             R"({"device_auth_id":"","user_code":"ABCD-EFGH"})",
             R"({"device_auth_id":"fixture-id","user_code":null,"usercode":"ABCD-EFGH"})",
             R"({"device_auth_id":"fixture-id","user_code":"ABCD-EFGH","usercode":"DIFFERENT"})",
             R"({"device_auth_id":"fixture-id","user_code":"ABCD\r\nInjected"})",
             R"({"device_auth_id":"fixture-id","user_code":"ABCD\u0000EFGH"})"}) rejected_device(input);
    rejected_device("{\"device_auth_id\":\"fixture-id\",\"user_code\":\"" + std::string(32, 'x') + "\"}");
    rejected_device("{\"device_auth_id\":\"" + std::string(513, 'x') + "\",\"user_code\":\"ABCD-EFGH\"}");
}

void authorization_is_only_committed_when_complete()
{
    Authorization output{"keep-code", "keep-verifier"};
    check(parse_authorization(R"({"authorization_code":"fixture-code","code_verifier":"fixture-verifier","code_challenge":"fixture-challenge"})", output) &&
              output.code == "fixture-code" && output.verifier == "fixture-verifier", "official poll fixture rejected");
    for (const auto input : {R"({"authorization_code":"fixture-code"})", R"({"authorization_code":1,"code_verifier":"v"})",
                             R"({"authorization_code":"new-code","code_verifier":""})",
                             R"({"authorization_code":"new-code","code_verifier":"bad\nheader"})"}) {
        check(!parse_authorization(input, output) && output.code == "fixture-code" && output.verifier == "fixture-verifier",
              "authorization parse accepted invalid data or partially updated output");
    }
}

void malformed_json_duplicates_and_trailing_data_fail_closed()
{
    for (const auto input : {"", "null", "[]", "{} trailing", "{}{}", "{", "{\"device_auth_id\":'x'}",
                             "{device_auth_id:\"x\",\"user_code\":\"y\"}",
                             "{\"device_auth_id\":\"x\",\"user_code\":\"y\",\"n\":01}",
                             "{\"device_auth_id\":\"x\",\"user_code\":\"y\",\"n\":NaN}",
                             "{\"device_auth_id\":\"x\",\"user_code\":\"y\",\"n\":\"\\ud800\"}",
                             "{\"device_auth_id\":\"x\",\"device_auth_id\":\"z\",\"user_code\":\"y\"}",
                             "{\"device_auth_id\":\"x\",\"device_auth_\\u0069d\":\"z\",\"user_code\":\"y\"}"})
        rejected_device(input);
    rejected_device(std::string("{\"device_auth_id\":\"x\",\"user_code\":\"y\"}") + '\0');
    rejected_device("{\"device_auth_id\":\"x\",\"user_code\":\"y\",\"extra\":\"" + std::string(1, static_cast<char>(0xff)) + "\"}");
}

void response_size_and_nesting_limits_are_enforced()
{
    const std::string prefix = R"({"authorization_code":"fixture-code","code_verifier":"fixture-verifier","extra":")";
    const std::string suffix = "\"}";
    Authorization output;
    const auto exact = prefix + std::string(kMaxResponseBytes - prefix.size() - suffix.size(), 'x') + suffix;
    check(parse_authorization(exact, output), "exact 32 KiB valid response was rejected");
    check(!parse_authorization(exact + ' ', output), "over-32 KiB response was accepted");
    const auto nested = prefix.substr(0, prefix.size() - 1) + std::string(20, '[') + "0" + std::string(20, ']') + "}";
    check(!parse_authorization(nested, output), "deep JSON bypassed the nesting limit");
    check(parse_authorization(R"({"authorization_code":"c","code_verifier":"v","extra":"\ud83d\ude00"})", output),
          "valid unrelated Unicode surrogate pair was rejected");
}

void opaque_access_and_id_identity_do_not_invent_access_expiry()
{
    Tokens output;
    check(parse_tokens(token_response(), now, {}, {}, output) && output.access_token == "opaque-access" &&
              output.refresh_token == "fixture-refresh" && output.account_id == "fixture-account" && output.expires_at == 0,
          "opaque official-style access failed or id-token exp was mistaken for access expiry");
    check(parse_tokens(R"({"access_token":"opaque-new"})", now, "old-refresh", "old-account", output) &&
              output.refresh_token == "old-refresh" && output.account_id == "old-account" && output.expires_at == 0,
          "missing optional refresh fields did not retain the old identity and refresh token");
}

void access_expiry_is_checked_without_float_or_boolean_coercion()
{
    Tokens output;
    const auto access = jwt(R"({"exp":1700003600})");
    check(parse_tokens(token_response(access), now, {}, {}, output) && output.expires_at == 1700003600u,
          "valid future access JWT expiry was not preserved");
    check(parse_tokens(token_response(jwt(R"({"exp":4294967295})")), now, {}, {}, output) &&
              output.expires_at == UINT32_MAX, "valid uint32 max access expiry was truncated");
    for (const auto exp : {"1700000000", "1699999999", "-1", "0", "true", "null", "1700003600.0", "\"1700003600\"", "4294967296"})
        rejected_tokens(token_response(jwt("{\"exp\":" + std::string(exp) + "}")));
    check(parse_tokens(token_response(jwt("{}")), now, {}, {}, output) && output.expires_at == 0,
          "JWT without exp invented an expiry or failed opaque-compatible metadata handling");
}

void explicit_empty_or_wrong_type_token_fields_do_not_inherit()
{
    for (const auto field : {"refresh_token", "id_token", "access_token"}) {
        for (const auto value : {"null", "true", "7", "\"\""}) {
            std::string input = "{\"access_token\":\"opaque-new\"";
            if (std::string_view(field) == "access_token") input = "{";
            else input += ',';
            input += "\"" + std::string(field) + "\":" + value + "}";
            rejected_tokens(input);
        }
    }
    rejected_tokens(R"({"access_token":"opaque-new"})", {}, "old-account");
    rejected_tokens(R"({"access_token":"opaque-new"})", "old-refresh", {});
    rejected_tokens(R"({"access_token":"bad\r\nAuthorization"})");
    rejected_tokens(R"({"access_token":"opaque-new"})", "bad\r\nrefresh", "old-account");
    rejected_tokens(R"({"access_token":"opaque-new"})", "old-refresh", "bad account");
}

void refresh_rotation_and_new_identity_replace_only_present_fields()
{
    Tokens output;
    const auto input = "{\"access_token\":\"new-access\",\"refresh_token\":\"rotated-refresh\",\"id_token\":\"" + identity("new-account") + "\"}";
    check(parse_tokens(input, now, "old-refresh", "old-account", output) && output.refresh_token == "rotated-refresh" &&
              output.account_id == "new-account", "refresh rotation or explicit new identity was discarded");
    check(parse_tokens(input, now, "bad old refresh", "bad old account", output),
          "unused old fields prevented a complete valid new token response");
    rejected_tokens("{\"access_token\":\"new-access\",\"refresh_token\":\"rotated-refresh\",\"id_token\":\"e30.e30.c2ln\"}");
}

void optional_refresh_preserves_rotation_even_with_old_expired_or_missing_access()
{
    const Tokens old{jwt(R"({"exp":1699999999})"), "old-refresh", "old-account", 1699999999u};
    Tokens output;
    check(parse_refresh_tokens(R"({"refresh_token":"rotated-refresh"})", now, old, output) &&
              output.refresh_token == "rotated-refresh" && output.access_token == old.access_token &&
              output.account_id == old.account_id && output.expires_at == old.expires_at,
          "refresh-only rotation was lost because inherited access was expired");
    const auto id_only = "{\"id_token\":\"" + identity("new-account") + "\"}";
    check(parse_refresh_tokens(id_only, now, old, output) && output.account_id == "new-account" &&
              output.access_token == old.access_token && output.refresh_token == old.refresh_token &&
              output.expires_at == old.expires_at, "id-only refresh did not preserve missing access and refresh values");
    check(parse_refresh_tokens("{}", now, old, output) && output.access_token == old.access_token &&
              output.refresh_token == old.refresh_token && output.account_id == old.account_id &&
              output.expires_at == old.expires_at, "empty refresh response did not inherit the complete previous token state");

    const Tokens boot{{}, "boot-refresh", "boot-account", 0};
    check(parse_refresh_tokens(R"({"refresh_token":"boot-rotated"})", now, boot, output) &&
              output.access_token.empty() && output.expires_at == 0 && output.refresh_token == "boot-rotated" &&
              output.account_id == "boot-account", "boot without stored access discarded a rotated refresh credential");
    check(parse_refresh_tokens("{}", now, boot, output) && output.access_token.empty() &&
              output.refresh_token == boot.refresh_token && output.account_id == boot.account_id,
          "boot refresh response fabricated an access token or discarded valid stored credentials");

    rejected_tokens(R"({"refresh_token":"rotated-refresh"})");
    rejected_tokens(id_only);
    rejected_tokens("{}");
    for (const auto field : {"access_token", "refresh_token", "id_token"}) {
        for (const auto value : {"null", "true", "7", "\"\""}) {
            output = {"keep-access", "keep-refresh", "keep-account", 42};
            const auto response = "{\"" + std::string(field) + "\":" + value + "}";
            check(!parse_refresh_tokens(response, now, old, output) && output.access_token == "keep-access" &&
                      output.refresh_token == "keep-refresh" && output.account_id == "keep-account" && output.expires_at == 42,
                  "explicit invalid optional token field inherited or partially changed refresh output");
        }
    }
    output = {"keep-access", "keep-refresh", "keep-account", 42};
    check(!parse_refresh_tokens(token_response(old.access_token, "new-rotation"), now, old, output) &&
              output.access_token == "keep-access" && output.refresh_token == "keep-refresh" && output.expires_at == 42,
          "explicit newly supplied expired access was accepted as an inherited token");
    check(parse_refresh_tokens(R"({"access_token":"new-opaque-access"})", now, old, output) &&
              output.access_token == "new-opaque-access" && output.expires_at == 0 &&
              output.refresh_token == old.refresh_token && output.account_id == old.account_id,
          "explicit new opaque access inherited the stale old expiry");
    auto aliased = old;
    check(parse_refresh_tokens(R"({"refresh_token":"aliased-rotation"})", now, aliased, aliased) &&
              aliased.access_token == old.access_token && aliased.expires_at == old.expires_at &&
              aliased.refresh_token == "aliased-rotation", "refresh output aliasing invalidated inherited values");
}

void identity_jwt_must_have_valid_payload_and_account_claim()
{
    for (const auto token : {"not-a-jwt", "e30.A.c2ln", "e30.____.c2ln", "e30.e30=.c2ln", "e30.e3$.c2ln", "e30.e31.c2ln", ".e30.c2ln", "e30.e30.", "e30.e30.c2ln.extra"})
        rejected_tokens("{\"access_token\":\"opaque-new\",\"id_token\":\"" + std::string(token) + "\"}");
    for (const auto payload : {"[]", "{}", R"({"https://api.openai.com/auth":null})",
                               R"({"https://api.openai.com/auth":{"chatgpt_account_id":""}})",
                               R"({"https://api.openai.com/auth":{"chatgpt_account_id":true}})",
                               R"({"https://api.openai.com/auth":{"chatgpt_account_id":"bad\r\nheader"}})",
                               R"({"exp":"1700000000","https://api.openai.com/auth":{"chatgpt_account_id":"fixture"}})"})
        rejected_tokens("{\"access_token\":\"opaque-new\",\"id_token\":\"" + jwt(payload) + "\"}");
    rejected_tokens("{\"access_token\":\"opaque-new\",\"id_token\":\"" + std::string(8193, 'x') + "\"}");
    rejected_tokens(token_response(std::string(8193, 'x')));
}

void malformed_access_jwt_is_opaque_and_never_supplies_identity()
{
    Tokens output;
    check(parse_tokens(token_response("e30.A.c2ln"), now, {}, {}, output) && output.expires_at == 0 &&
              output.account_id == "fixture-account", "malformed JWT-like opaque access was rejected despite valid id identity");
    check(parse_tokens(R"({"access_token":"e30.A.c2ln"})", now, "old-refresh", "old-account", output) &&
              output.account_id == "old-account" && output.expires_at == 0,
          "refresh opaque compatibility failed with valid old account identity");
    rejected_tokens("{\"access_token\":\"" + identity() + "\",\"refresh_token\":\"fixture-refresh\"}", {}, {});
}

void weekly_window_may_be_primary_or_secondary()
{
    Usage output;
    check(parse_usage(usage(window("12", "18000"), window()), now, output) &&
              output.remaining_percent == 64 && output.reset_at == 1700604800u,
          "weekly secondary fixture did not report 100 minus integer used percent");
    check(parse_usage(usage(window(), window("12", "18000")), now, output) && output.remaining_percent == 64,
          "weekly primary fixture was mistaken for the five-hour window");
    check(parse_usage(usage(window("0")), now, output) && output.remaining_percent == 100, "zero used quota failed");
    check(parse_usage(usage(window("100")), now, output) && output.remaining_percent == 0, "fully used quota failed");
    check(parse_usage(usage(window("1", "604800", "4294967295")), now, output) && output.reset_at == UINT32_MAX,
          "uint32 max reset deadline was truncated");
}

void quota_numbers_are_integers_with_future_deadlines()
{
    for (const auto used : {"-1", "101", "true", "null", "\"36\"", "36.0", "36.7", "4294967296"})
        rejected_usage(usage(window(used)));
    for (const auto seconds : {"true", "null", "\"604800\"", "604800.0", "604800.1", "-1", "4294967296"})
        rejected_usage(usage(window("36", seconds)));
    for (const auto reset : {"1700000000", "1699999999", "0", "-1", "true", "null", "\"1700604800\"", "1700604800.0", "4294967296"})
        rejected_usage(usage(window("36", "604800", reset)));
}

void quota_never_substitutes_another_bucket_or_ambiguous_week()
{
    for (const auto input : {"{}", R"({"rate_limit":null})", R"({"rate_limit":[]})",
                             R"({"rate_limit":{"primary_window":null,"secondary_window":null}})",
                             R"({"rate_limit":{"primary_window":false}})"}) rejected_usage(input);
    rejected_usage(usage(window("36", "18000")));
    rejected_usage(usage(window(), window()));
    rejected_usage("{\"rate_limit\":{\"primary_window\":" + window("36", "18000") +
                   "},\"additional_rate_limits\":[{\"rate_limit\":{\"secondary_window\":" + window() + "}}]}");
    rejected_usage("{\"rate_limit\":{\"primary_window\":" + window() + ",\"primary_window\":" + window("5") + "}}");
    Usage output;
    check(parse_usage("{\"rate_limit\":{\"primary_window\":" + window() + "},\"unrelated\":{\"x\":[1,true,null]}}", now, output) &&
              output.remaining_percent == 64, "unrelated new fields broke an unambiguous weekly response");
}

void refresh_permanent_errors_are_distinguished_from_network_and_rate_limits()
{
    check(refresh_rejected_permanently(401, "not-json"), "401 refresh rejection was not permanent");
    for (const auto code : {"invalid_grant", "refresh_token_expired", "refresh_token_reused", "refresh_token_invalidated"}) {
        const auto simple = "{\"error\":\"" + std::string(code) + "\"}";
        const auto nested = "{\"error\":{\"code\":\"" + std::string(code) + "\"}}";
        check(refresh_rejected_permanently(400, simple) && refresh_rejected_permanently(400, nested), "known refresh code was not permanent");
        const bool explicit_refresh_code = std::string_view(code) != "invalid_grant";
        check(refresh_rejected_permanently(429, nested) == explicit_refresh_code &&
                  refresh_rejected_permanently(503, nested) == explicit_refresh_code &&
                  refresh_rejected_permanently(200, nested) == explicit_refresh_code,
              "explicit official refresh rejection code did not follow its status-independent rule");
    }
    for (const auto input : {"", "not-json", R"({"error":"invalid_request"})", R"({"error":{"message":"invalid_grant"}})",
                             R"({"error":true})"})
        check(!refresh_rejected_permanently(400, input), "unknown or malformed 400 was treated as permanent");
    check(refresh_rejected_permanently(400, R"({"error":"INVALID_GRANT"})") &&
              refresh_rejected_permanently(403, R"({"code":"REFRESH_TOKEN_REUSED"})") &&
              refresh_rejected_permanently(403, R"({"error":{"code":true},"code":"refresh_token_expired"})"),
          "case-insensitive top-level fallback did not match official refresh rejection handling");
    check(!refresh_rejected_permanently(403, R"({"error":"invalid_grant","code":"refresh_token_expired"})") &&
              !refresh_rejected_permanently(429, R"({"error":{"code":"rate_limit_exceeded"},"code":"refresh_token_reused"})") &&
              !refresh_rejected_permanently(0, R"({"error":"refresh_token_reused"})"),
          "error-code priority or absence of an HTTP response incorrectly invalidated credentials");
}

void form_encoding_preserves_unreserved_bytes_and_escapes_everything_else()
{
    check(form_encode("").empty() && form_encode("AZaz09-._~") == "AZaz09-._~", "URL unreserved bytes changed");
    check(form_encode("a b+c/=?&%") == "a%20b%2Bc%2F%3D%3F%26%25", "URL reserved or space escaping was incorrect");
    check(form_encode(std::string("\0\xff", 2)) == "%00%FF", "URL binary data was truncated or sign-extended");
    check(form_encode("\xe4\xb8\xad") == "%E4%B8%AD", "URL UTF-8 bytes were not independently escaped");
}

} // namespace

int main()
{
    const std::array tests{
        official_device_string_interval_and_alias, device_fields_and_numeric_types_are_strict,
        authorization_is_only_committed_when_complete, malformed_json_duplicates_and_trailing_data_fail_closed,
        response_size_and_nesting_limits_are_enforced, opaque_access_and_id_identity_do_not_invent_access_expiry,
        access_expiry_is_checked_without_float_or_boolean_coercion, explicit_empty_or_wrong_type_token_fields_do_not_inherit,
        refresh_rotation_and_new_identity_replace_only_present_fields, identity_jwt_must_have_valid_payload_and_account_claim,
        optional_refresh_preserves_rotation_even_with_old_expired_or_missing_access,
        malformed_access_jwt_is_opaque_and_never_supplies_identity, weekly_window_may_be_primary_or_secondary,
        quota_numbers_are_integers_with_future_deadlines, quota_never_substitutes_another_bucket_or_ambiguous_week,
        refresh_permanent_errors_are_distinguished_from_network_and_rate_limits,
        form_encoding_preserves_unreserved_bytes_and_escapes_everything_else,
    };
    try { for (const auto test : tests) test(); }
    catch (const std::exception& error) { std::cerr << "codex weekly protocol: " << error.what() << '\n'; return 1; }
    std::cout << "codex weekly protocol: " << tests.size() << " synthetic protocol groups passed\n";
    return 0;
}
