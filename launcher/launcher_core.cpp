#include "launcher_core.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace hotas::launcher {
namespace {

constexpr std::string_view kRepositoryPrefix =
    "https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/";
constexpr std::string_view kReleaseNotesPrefix =
    "https://github.com/Kgray44/HOTAS_BF6_simple/releases/tag/";

void setError(std::string *error, std::string_view message)
{
    if (error) *error = message;
}

bool parseVersionComponent(std::string_view text, std::uint32_t &value)
{
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
    for (const char character : text) {
        if (character < '0' || character > '9') return false;
    }
    const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value);
    return status == std::errc{} && end == text.data() + text.size();
}

struct JsonScalar {
    enum class Type { String, Number };
    Type type = Type::String;
    std::string value;
};

class FlatJsonReader final {
public:
    explicit FlatJsonReader(std::string_view input) : m_input(input) {}

    bool parse(std::map<std::string, JsonScalar> &fields, std::string *error)
    {
        skipWhitespace();
        if (!consume('{')) return fail(error, "manifest must be a JSON object");
        skipWhitespace();
        if (consume('}')) {
            skipWhitespace();
            return m_position == m_input.size() || fail(error, "trailing manifest data");
        }
        for (;;) {
            std::string key;
            if (!parseString(key, error)) return false;
            skipWhitespace();
            if (!consume(':')) return fail(error, "manifest key is missing a colon");
            skipWhitespace();
            JsonScalar value;
            if (peek() == '"') {
                value.type = JsonScalar::Type::String;
                if (!parseString(value.value, error)) return false;
            } else {
                value.type = JsonScalar::Type::Number;
                if (!parseNumber(value.value, error)) return false;
            }
            if (!fields.emplace(std::move(key), std::move(value)).second) {
                return fail(error, "manifest contains a duplicate field");
            }
            skipWhitespace();
            if (consume('}')) {
                skipWhitespace();
                return m_position == m_input.size() || fail(error, "trailing manifest data");
            }
            if (!consume(',')) return fail(error, "manifest object is malformed");
            skipWhitespace();
        }
    }

private:
    bool fail(std::string *error, std::string_view message)
    {
        setError(error, message);
        return false;
    }

    char peek() const
    {
        return m_position < m_input.size() ? m_input[m_position] : '\0';
    }

    bool consume(char expected)
    {
        if (peek() != expected) return false;
        ++m_position;
        return true;
    }

    void skipWhitespace()
    {
        while (m_position < m_input.size()
               && std::isspace(static_cast<unsigned char>(m_input[m_position]))) {
            ++m_position;
        }
    }

    bool parseHex4(std::uint32_t &value)
    {
        if (m_position + 4 > m_input.size()) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const char character = m_input[m_position++];
            value <<= 4;
            if (character >= '0' && character <= '9') value |= character - '0';
            else if (character >= 'a' && character <= 'f') value |= character - 'a' + 10;
            else if (character >= 'A' && character <= 'F') value |= character - 'A' + 10;
            else return false;
        }
        return true;
    }

    static void appendUtf8(std::string &target, std::uint32_t codePoint)
    {
        if (codePoint <= 0x7f) target.push_back(static_cast<char>(codePoint));
        else if (codePoint <= 0x7ff) {
            target.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            target.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else {
            target.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            target.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            target.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }

    bool parseString(std::string &target, std::string *error)
    {
        if (!consume('"')) return fail(error, "manifest string is malformed");
        target.clear();
        while (m_position < m_input.size()) {
            const unsigned char character = static_cast<unsigned char>(m_input[m_position++]);
            if (character == '"') return true;
            if (character < 0x20) return fail(error, "manifest string contains a control character");
            if (character != '\\') {
                target.push_back(static_cast<char>(character));
                continue;
            }
            if (m_position == m_input.size()) return fail(error, "manifest string escape is incomplete");
            const char escaped = m_input[m_position++];
            switch (escaped) {
            case '"': target.push_back('"'); break;
            case '\\': target.push_back('\\'); break;
            case '/': target.push_back('/'); break;
            case 'b': target.push_back('\b'); break;
            case 'f': target.push_back('\f'); break;
            case 'n': target.push_back('\n'); break;
            case 'r': target.push_back('\r'); break;
            case 't': target.push_back('\t'); break;
            case 'u': {
                std::uint32_t codePoint = 0;
                if (!parseHex4(codePoint)) return fail(error, "manifest unicode escape is malformed");
                appendUtf8(target, codePoint);
                break;
            }
            default: return fail(error, "manifest contains an unsupported escape");
            }
        }
        return fail(error, "manifest string is unterminated");
    }

    bool parseNumber(std::string &target, std::string *error)
    {
        const size_t start = m_position;
        consume('-');
        if (peek() == '0') ++m_position;
        else {
            if (peek() < '1' || peek() > '9') return fail(error, "manifest value must be a string or number");
            while (peek() >= '0' && peek() <= '9') ++m_position;
        }
        if (consume('.')) {
            if (peek() < '0' || peek() > '9') return fail(error, "manifest number is malformed");
            while (peek() >= '0' && peek() <= '9') ++m_position;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++m_position;
            if (peek() == '+' || peek() == '-') ++m_position;
            if (peek() < '0' || peek() > '9') return fail(error, "manifest number is malformed");
            while (peek() >= '0' && peek() <= '9') ++m_position;
        }
        target.assign(m_input.substr(start, m_position - start));
        return true;
    }

    std::string_view m_input;
    size_t m_position = 0;
};

bool requiredString(const std::map<std::string, JsonScalar> &fields, std::string_view name,
                    std::string &value, std::string *error)
{
    const auto found = fields.find(std::string(name));
    if (found == fields.end() || found->second.type != JsonScalar::Type::String || found->second.value.empty()) {
        setError(error, "manifest is missing a required string field");
        return false;
    }
    value = found->second.value;
    return true;
}

bool isSha256(std::string_view hash)
{
    if (hash.size() != 64) return false;
    for (const unsigned char character : hash) {
        if (!std::isxdigit(character)) return false;
    }
    return true;
}

std::string lowercase(std::string text)
{
    for (char &character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text;
}

std::string versionText(const SemanticVersion &version)
{
    return std::to_string(version.major) + "." + std::to_string(version.minor)
        + "." + std::to_string(version.patch);
}

} // namespace

bool parseSemanticVersion(std::string_view text, SemanticVersion &version, std::string *error)
{
    std::array<std::uint32_t, 3> values{};
    size_t componentStart = 0;
    for (size_t component = 0; component < values.size(); ++component) {
        const size_t separator = text.find('.', componentStart);
        if ((component < 2 && separator == std::string_view::npos)
            || (component == 2 && separator != std::string_view::npos)
            || !parseVersionComponent(text.substr(componentStart,
                (separator == std::string_view::npos ? text.size() : separator) - componentStart),
                values[component])) {
            setError(error, "version must be a numeric major.minor.patch value");
            return false;
        }
        componentStart = separator == std::string_view::npos ? text.size() : separator + 1;
    }
    version = {values[0], values[1], values[2]};
    return true;
}

int compareSemanticVersions(const SemanticVersion &left, const SemanticVersion &right)
{
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    return 0;
}

bool parseAndValidateManifest(std::string_view json, UpdateManifest &manifest, std::string *error)
{
    std::map<std::string, JsonScalar> fields;
    FlatJsonReader reader(json);
    if (!reader.parse(fields, error)) return false;

    const auto schema = fields.find("schema");
    if (schema == fields.end() || schema->second.type != JsonScalar::Type::Number || schema->second.value != "1") {
        setError(error, "manifest schema is unsupported");
        return false;
    }
    std::string channel;
    if (!requiredString(fields, "channel", channel, error) || channel != "stable") {
        setError(error, "manifest channel is not stable");
        return false;
    }
    UpdateManifest candidate;
    std::string minimumLauncherVersion;
    if (!requiredString(fields, "version", candidate.versionText, error)
        || !parseSemanticVersion(candidate.versionText, candidate.version, error)
        || !requiredString(fields, "minimum_launcher_version", minimumLauncherVersion, error)
        || !parseSemanticVersion(minimumLauncherVersion, candidate.minimumLauncherVersion, error)) {
        return false;
    }
    if (!requiredString(fields, "tag", candidate.tag, error)
        || candidate.tag != "v" + candidate.versionText
        || !requiredString(fields, "installer", candidate.installer, error)
        || candidate.installer != "HOTAS-BF6-Setup-v" + candidate.versionText + ".exe") {
        setError(error, "manifest tag or installer name is unsafe");
        return false;
    }
    if (!requiredString(fields, "installer_url", candidate.installerUrl, error)
        || candidate.installerUrl != std::string(kRepositoryPrefix) + candidate.tag + "/" + candidate.installer) {
        setError(error, "manifest installer URL is not the official HTTPS release asset");
        return false;
    }
    if (!requiredString(fields, "sha256", candidate.sha256, error) || !isSha256(candidate.sha256)) {
        setError(error, "manifest SHA-256 is invalid");
        return false;
    }
    candidate.sha256 = lowercase(candidate.sha256);
    if (!requiredString(fields, "published_utc", candidate.publishedUtc, error)
        || !requiredString(fields, "release_notes_url", candidate.releaseNotesUrl, error)
        || candidate.releaseNotesUrl != std::string(kReleaseNotesPrefix) + candidate.tag) {
        setError(error, "manifest release metadata is invalid");
        return false;
    }
    manifest = std::move(candidate);
    return true;
}

UpdateAction decideUpdate(bool fetchSucceeded, std::string_view manifestJson,
                          const SemanticVersion &localVersion, UpdateManifest *manifest,
                          std::string *reason)
{
    if (!fetchSucceeded) {
        setError(reason, "manifest fetch failed");
        return UpdateAction::LaunchCurrent;
    }
    UpdateManifest parsed;
    if (!parseAndValidateManifest(manifestJson, parsed, reason)) return UpdateAction::LaunchCurrent;
    if (compareSemanticVersions(localVersion, parsed.minimumLauncherVersion) < 0) {
        setError(reason, "installed launcher is below the manifest minimum");
        return UpdateAction::LaunchCurrent;
    }
    if (compareSemanticVersions(parsed.version, localVersion) <= 0) {
        setError(reason, "installed version is current or newer");
        return UpdateAction::LaunchCurrent;
    }
    if (manifest) *manifest = std::move(parsed);
    setError(reason, "newer stable release available");
    return UpdateAction::InstallUpdate;
}

bool verifyFileSha256(const std::wstring &path, std::string_view expectedHash, std::string *error)
{
    if (!isSha256(expectedHash)) {
        setError(error, "expected SHA-256 is invalid");
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::byte> hashObject;
    bool result = false;
    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            setError(error, "could not initialize SHA-256");
            break;
        }
        DWORD hashObjectLength = 0;
        DWORD hashLength = 0;
        DWORD returned = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&hashObjectLength), sizeof(hashObjectLength), &returned, 0))
            || !BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &returned, 0))
            || hashLength != 32) {
            setError(error, "SHA-256 provider returned an unexpected size");
            break;
        }
        hashObject.resize(hashObjectLength);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, reinterpret_cast<PUCHAR>(hashObject.data()),
                hashObjectLength, nullptr, 0, 0))) {
            setError(error, "could not create SHA-256 state");
            break;
        }
        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        if (!input) {
            setError(error, "downloaded installer could not be opened");
            break;
        }
        std::array<char, 32 * 1024> buffer{};
        while (input.good()) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = input.gcount();
            if (read > 0 && !BCRYPT_SUCCESS(BCryptHashData(hash,
                    reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(read), 0))) {
                setError(error, "could not hash downloaded installer");
                break;
            }
            if (read == 0) break;
        }
        if (!input.eof() && input.fail()) {
            setError(error, "could not read downloaded installer");
            break;
        }
        std::array<unsigned char, 32> digest{};
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) {
            setError(error, "could not finish SHA-256");
            break;
        }
        constexpr char hex[] = "0123456789abcdef";
        std::string calculated;
        calculated.reserve(digest.size() * 2);
        for (const unsigned char byte : digest) {
            calculated.push_back(hex[(byte >> 4) & 0x0f]);
            calculated.push_back(hex[byte & 0x0f]);
        }
        if (calculated != lowercase(std::string(expectedHash))) {
            setError(error, "downloaded installer SHA-256 does not match");
            break;
        }
        result = true;
    } while (false);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

} // namespace hotas::launcher
