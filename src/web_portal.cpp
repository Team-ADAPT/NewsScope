#include "scoring_engine.h"
#include "thread_pool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace newsscope;

namespace {

enum class JobStatus { QUEUED, PROCESSING, DONE, FAILED };

struct JobRecord {
    JobStatus status = JobStatus::QUEUED;
    CredibilityResult result{};
    std::string label;
    std::string label_reason;
    std::string error;
    std::chrono::steady_clock::time_point updated_at = std::chrono::steady_clock::now();
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::atomic<bool> g_running{true};
std::atomic<int> g_server_fd{-1};
constexpr size_t MAX_IN_FLIGHT_JOBS = 2000;
constexpr size_t MAX_STORED_FINISHED_JOBS = 5000;
const auto FINISHED_JOB_TTL = std::chrono::minutes(10);
constexpr size_t MAX_FETCH_RESPONSE_BYTES = 600000;
constexpr size_t MAX_ANALYSIS_TEXT_BYTES = 20000;
constexpr size_t MAX_REQUEST_BODY_BYTES = 65536;
constexpr size_t MAX_SOURCE_BYTES = 160;
constexpr size_t MAX_PATH_BYTES = 2048;

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool has_invalid_url_characters(const std::string& url) {
    for (unsigned char c : url) {
        if (c < 32 || c == 127 || std::isspace(c)) {
            return true;
        }
    }
    return false;
}

std::string extract_host_from_url(const std::string& url) {
    const size_t scheme_sep = url.find("://");
    if (scheme_sep == std::string::npos) {
        return "";
    }
    const size_t host_start = scheme_sep + 3;
    if (host_start >= url.size()) {
        return "";
    }
    size_t host_end = host_start;
    while (host_end < url.size()) {
        const char c = url[host_end];
        if (c == '/' || c == '?' || c == '#' || c == ':') {
            break;
        }
        ++host_end;
    }
    if (host_end <= host_start) {
        return "";
    }
    return to_lower(url.substr(host_start, host_end - host_start));
}

std::string extract_domain_from_url(const std::string& url) {
    std::string host = extract_host_from_url(url);
    if (host.empty()) {
        return "";
    }
    
    // Remove www. prefix for consistency
    if (host.find("www.") == 0) {
        host = host.substr(4);
    }
    
    // For subdomains like m.bbc.co.uk or mobile.reuters.com, use the main domain
    // Keep it simple: just return the host as-is (the source validator will handle it)
    return host;
}

bool looks_like_ipv4(const std::string& host) {
    if (host.empty()) {
        return false;
    }
    for (char c : host) {
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.')) {
            return false;
        }
    }
    return host.find('.') != std::string::npos;
}

bool is_private_ipv4_host(const std::string& host) {
    if (!looks_like_ipv4(host)) {
        return false;
    }

    std::vector<int> octets;
    std::stringstream ss(host);
    std::string part;
    while (std::getline(ss, part, '.')) {
        if (part.empty() || part.size() > 3) {
            return false;
        }
        const int value = std::atoi(part.c_str());
        if (value < 0 || value > 255) {
            return false;
        }
        octets.push_back(value);
    }
    if (octets.size() != 4) {
        return false;
    }

    return octets[0] == 10 ||
           octets[0] == 127 ||
           octets[0] == 0 ||
           (octets[0] == 169 && octets[1] == 254) ||
           (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
           (octets[0] == 192 && octets[1] == 168) ||
           (octets[0] == 100 && octets[1] >= 64 && octets[1] <= 127);
}

bool has_disallowed_host_suffix(const std::string& host) {
    static const std::vector<std::string> suffixes = {
        ".local", ".internal", ".lan", ".home", ".home.arpa"
    };
    for (const auto& suffix : suffixes) {
        if (host.size() >= suffix.size() &&
            host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

bool has_userinfo_in_url(const std::string& url) {
    const size_t scheme_sep = url.find("://");
    if (scheme_sep == std::string::npos) {
        return false;
    }
    const size_t authority_start = scheme_sep + 3;
    const size_t authority_end = url.find_first_of("/?#", authority_start);
    const std::string authority = url.substr(authority_start, authority_end - authority_start);
    return authority.find('@') != std::string::npos;
}

bool is_reasonable_http_method(const std::string& method) {
    return method == "GET" || method == "POST";
}

bool is_reasonable_path(const std::string& path) {
    if (path.empty() || path.size() > MAX_PATH_BYTES || path[0] != '/') {
        return false;
    }
    for (unsigned char c : path) {
        if (c < 32 || c == 127) {
            return false;
        }
    }
    return true;
}

bool is_private_or_local_sockaddr(const sockaddr* addr) {
    if (!addr) {
        return true;
    }

    if (addr->sa_family == AF_INET) {
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
        const uint32_t host_order = ntohl(addr4->sin_addr.s_addr);
        const uint8_t first = static_cast<uint8_t>((host_order >> 24) & 0xFF);
        const uint8_t second = static_cast<uint8_t>((host_order >> 16) & 0xFF);

        return first == 10 ||
               first == 127 ||
               first == 0 ||
               (first == 169 && second == 254) ||
               (first == 172 && second >= 16 && second <= 31) ||
               (first == 192 && second == 168) ||
               (first == 100 && second >= 64 && second <= 127);
    }

    if (addr->sa_family == AF_INET6) {
        const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
        const auto& bytes = addr6->sin6_addr.s6_addr;
        const bool loopback = IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr);
        const bool link_local = IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr);
        const bool unique_local = (bytes[0] & 0xFE) == 0xFC;
        const bool unspecified = IN6_IS_ADDR_UNSPECIFIED(&addr6->sin6_addr);
        return loopback || link_local || unique_local || unspecified;
    }

    return true;
}

bool host_resolves_to_public_address(const std::string& host, std::string& out_ip) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    addrinfo* results = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &results) != 0) {
        return false;
    }

    bool found_public = false;
    for (addrinfo* current = results; current != nullptr; current = current->ai_next) {
        if (!is_private_or_local_sockaddr(current->ai_addr)) {
            found_public = true;
            char ipstr[INET6_ADDRSTRLEN];
            if (current->ai_family == AF_INET) {
                inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(current->ai_addr)->sin_addr, ipstr, sizeof(ipstr));
            } else {
                inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(current->ai_addr)->sin6_addr, ipstr, sizeof(ipstr));
            }
            out_ip = ipstr;
            break;
        }
    }

    freeaddrinfo(results);
    return found_public;
}

bool is_allowed_http_url(const std::string& raw_url) {
    const std::string url = trim(raw_url);
    if (url.empty() || url.size() > 2048 || has_invalid_url_characters(url)) {
        return false;
    }

    const std::string lower = to_lower(url);
    if (!(lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0)) {
        return false;
    }
    if (has_userinfo_in_url(url)) {
        return false;
    }

    const std::string host = extract_host_from_url(lower);
    if (host.empty()) {
        return false;
    }
    if (host == "localhost" || host == "::1" ||
        host.rfind("127.", 0) == 0 || host.rfind("0.", 0) == 0) {
        return false;
    }
    if (is_private_ipv4_host(host) || has_disallowed_host_suffix(host)) {
        return false;
    }

    return true;
}

bool starts_with_ci(const std::string& text, size_t pos, const std::string& token) {
    if (pos + token.size() > text.size()) {
        return false;
    }
    for (size_t i = 0; i < token.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[pos + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

size_t find_ci(const std::string& text, const std::string& token, size_t start_pos = 0) {
    if (token.empty() || start_pos >= text.size()) {
        return std::string::npos;
    }
    for (size_t i = start_pos; i + token.size() <= text.size(); ++i) {
        if (starts_with_ci(text, i, token)) {
            return i;
        }
    }
    return std::string::npos;
}

std::string decode_html_entities(std::string text) {
    const std::vector<std::pair<std::string, std::string>> entities = {
        {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}
    };
    for (const auto& [needle, replacement] : entities) {
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            text.replace(pos, needle.size(), replacement);
            pos += replacement.size();
        }
    }
    return text;
}

std::string collapse_whitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool prev_space = false;
    for (unsigned char c : text) {
        if (std::isspace(c)) {
            if (!prev_space) {
                out.push_back(' ');
                prev_space = true;
            }
        } else {
            out.push_back(static_cast<char>(c));
            prev_space = false;
        }
    }
    return trim(out);
}

bool is_tag_boundary_character(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isspace(uc) || c == '>' || c == '/' || c == '\0';
}

size_t find_tag_open_ci(const std::string& html, const std::string& tag, size_t start_pos = 0) {
    const std::string needle = "<" + tag;
    size_t pos = start_pos;
    while (true) {
        pos = find_ci(html, needle, pos);
        if (pos == std::string::npos) {
            return pos;
        }
        const size_t boundary_idx = pos + needle.size();
        const char boundary = (boundary_idx < html.size()) ? html[boundary_idx] : '\0';
        if (is_tag_boundary_character(boundary)) {
            return pos;
        }
        ++pos;
    }
}

size_t find_tag_close_ci(const std::string& html, const std::string& tag, size_t start_pos = 0) {
    const std::string needle = "</" + tag;
    size_t pos = start_pos;
    while (true) {
        pos = find_ci(html, needle, pos);
        if (pos == std::string::npos) {
            return pos;
        }
        const size_t boundary_idx = pos + needle.size();
        const char boundary = (boundary_idx < html.size()) ? html[boundary_idx] : '\0';
        if (is_tag_boundary_character(boundary)) {
            return pos;
        }
        ++pos;
    }
}

std::vector<std::string> collect_tag_inner_blocks(const std::string& html,
                                                  const std::string& tag,
                                                  size_t max_blocks) {
    std::vector<std::string> blocks;
    size_t search_pos = 0;

    while (blocks.size() < max_blocks) {
        const size_t open_start = find_tag_open_ci(html, tag, search_pos);
        if (open_start == std::string::npos) {
            break;
        }
        const size_t open_end = html.find('>', open_start);
        if (open_end == std::string::npos) {
            break;
        }
        if (open_end > open_start && html[open_end - 1] == '/') {
            search_pos = open_end + 1;
            continue;
        }

        size_t depth = 1;
        size_t cursor = open_end + 1;
        size_t close_start = std::string::npos;
        size_t close_end = std::string::npos;

        while (cursor < html.size()) {
            const size_t next_open = find_tag_open_ci(html, tag, cursor);
            const size_t next_close = find_tag_close_ci(html, tag, cursor);
            if (next_close == std::string::npos) {
                break;
            }

            if (next_open != std::string::npos && next_open < next_close) {
                const size_t nested_end = html.find('>', next_open);
                if (nested_end == std::string::npos) {
                    break;
                }
                if (!(nested_end > next_open && html[nested_end - 1] == '/')) {
                    ++depth;
                }
                cursor = nested_end + 1;
                continue;
            }

            const size_t candidate_close_end = html.find('>', next_close);
            if (candidate_close_end == std::string::npos) {
                break;
            }
            --depth;
            if (depth == 0) {
                close_start = next_close;
                close_end = candidate_close_end;
                break;
            }
            cursor = candidate_close_end + 1;
        }

        if (close_start == std::string::npos) {
            search_pos = open_end + 1;
            continue;
        }

        blocks.push_back(html.substr(open_end + 1, close_start - (open_end + 1)));
        search_pos = close_end + 1;
    }

    return blocks;
}

std::string strip_script_style_tags(const std::string& html) {
    std::string cleaned;
    cleaned.reserve(html.size());
    
    size_t i = 0;
    while (i < html.size()) {
        // Find opening tag
        const size_t tag_start = html.find('<', i);
        if (tag_start == std::string::npos) {
            cleaned.append(html.substr(i));
            break;
        }
        
        // Add text before tag
        cleaned.append(html.substr(i, tag_start - i));
        
        // Find tag name
        size_t tag_name_start = tag_start + 1;
        if (tag_name_start < html.size() && html[tag_name_start] == '/') {
            tag_name_start++;
        }
        
        const size_t tag_name_end = html.find_first_of("> \t\n", tag_name_start);
        if (tag_name_end == std::string::npos) {
            cleaned.append(html.substr(tag_start, 1));
            ++i;
            continue;
        }
        
        std::string tag_name = html.substr(tag_name_start, tag_name_end - tag_name_start);
        tag_name = to_lower(tag_name);
        
        // Skip script and style tags and their contents
        if (tag_name == "script" || tag_name == "style") {
            const std::string closing_tag = "</" + tag_name + ">";
            const size_t closing_pos = find_ci(html, closing_tag, tag_start);
            if (closing_pos != std::string::npos) {
                i = closing_pos + closing_tag.size();
                continue;
            }
        }
        
        // Find end of this tag
        const size_t tag_end = html.find('>', tag_start);
        if (tag_end == std::string::npos) {
            cleaned.append(html.substr(tag_start, 1));
            ++i;
        } else {
            cleaned.append(html.substr(tag_start, tag_end - tag_start + 1));
            i = tag_end + 1;
        }
    }
    
    return cleaned;
}

std::string plain_text_from_html_fragment(const std::string& html_fragment) {
    // First remove script and style tags
    const std::string cleaned_html = strip_script_style_tags(html_fragment);
    
    std::string out;
    out.reserve(std::min(MAX_FETCH_RESPONSE_BYTES, cleaned_html.size()));

    size_t i = 0;
    while (i < cleaned_html.size()) {
        if (cleaned_html[i] == '<') {
            const size_t tag_end = cleaned_html.find('>', i + 1);
            i = (tag_end == std::string::npos) ? cleaned_html.size() : tag_end + 1;
            out.push_back(' ');
            continue;
        }
        out.push_back(cleaned_html[i]);
        ++i;
    }

    out = decode_html_entities(out);
    return collapse_whitespace(out);
}

bool looks_like_boilerplate(const std::string& text) {
    const std::string lower = to_lower(text);
    static const std::vector<std::string> boilerplate_tokens = {
        "cookie", "privacy policy", "terms of use", "terms of service",
        "all rights reserved", "subscribe", "sign up", "advertisement",
        "newsletter", "accept all", "manage preferences", "follow us",
        "share this article", "related stories", "read more"
    };
    for (const auto& token : boilerplate_tokens) {
        if (lower.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool is_useful_news_paragraph(const std::string& text) {
    if (text.size() < 70 || text.size() > 2200) {
        return false;
    }
    if (looks_like_boilerplate(text)) {
        return false;
    }

    int alpha_count = 0;
    int digit_count = 0;
    for (unsigned char c : text) {
        if (std::isalpha(c)) {
            ++alpha_count;
        } else if (std::isdigit(c)) {
            ++digit_count;
        }
    }
    const int content_count = alpha_count + digit_count;
    if (content_count < static_cast<int>(text.size() * 0.35)) {
        return false;
    }
    return true;
}

std::string extract_primary_title(const std::string& html) {
    auto h1_blocks = collect_tag_inner_blocks(html, "h1", 1);
    if (!h1_blocks.empty()) {
        const std::string h1 = plain_text_from_html_fragment(h1_blocks.front());
        if (!h1.empty()) {
            return h1;
        }
    }
    return "";
}

std::vector<std::string> extract_primary_html_sections(const std::string& html) {
    std::vector<std::string> sections = collect_tag_inner_blocks(html, "article", 3);
    if (!sections.empty()) {
        return sections;
    }

    sections = collect_tag_inner_blocks(html, "main", 3);
    if (!sections.empty()) {
        return sections;
    }

    sections = collect_tag_inner_blocks(html, "body", 1);
    if (!sections.empty()) {
        return sections;
    }

    return {html};
}

std::string build_excerpt_from_text(const std::string& text) {
    std::string excerpt;
    excerpt.reserve(std::min(MAX_ANALYSIS_TEXT_BYTES, text.size()));

    size_t sentence_count = 0;
    size_t start = 0;
    while (start < text.size() && excerpt.size() < MAX_ANALYSIS_TEXT_BYTES) {
        size_t end = text.find_first_of(".!?", start);
        if (end == std::string::npos) {
            end = text.size();
        } else {
            ++end;
        }
        std::string sentence = trim(text.substr(start, end - start));
        start = end;
        if (sentence.size() < 45 || looks_like_boilerplate(sentence)) {
            continue;
        }
        if (!excerpt.empty()) {
            excerpt += " ";
        }
        excerpt += sentence;
        ++sentence_count;
        if (sentence_count >= 10 || excerpt.size() >= 2800) {
            break;
        }
    }

    if (excerpt.empty()) {
        excerpt = text.substr(0, std::min(text.size(), static_cast<size_t>(2800)));
    }

    return trim(excerpt);
}

std::string extract_title_from_html(const std::string& html) {
    const size_t title_start = find_ci(html, "<title");
    if (title_start == std::string::npos) {
        return "";
    }
    const size_t open_end = html.find('>', title_start);
    if (open_end == std::string::npos || open_end + 1 >= html.size()) {
        return "";
    }
    const size_t close_start = find_ci(html, "</title>", open_end + 1);
    if (close_start == std::string::npos || close_start <= open_end + 1) {
        return "";
    }
    const std::string raw_title = html.substr(open_end + 1, close_start - (open_end + 1));
    return collapse_whitespace(decode_html_entities(raw_title));
}

std::string build_article_text_from_url_payload(const std::string& url,
                                                const std::string& payload) {
    std::string title = extract_primary_title(payload);
    if (title.empty()) {
        title = extract_title_from_html(payload);
    }
    if (title.empty()) {
        title = url;
    }

    const auto sections = extract_primary_html_sections(payload);
    std::vector<std::string> paragraphs;
    paragraphs.reserve(16);
    std::unordered_set<std::string> seen;

    for (const auto& section_html : sections) {
        auto p_blocks = collect_tag_inner_blocks(section_html, "p", 80);
        for (const auto& block : p_blocks) {
            const std::string paragraph = plain_text_from_html_fragment(block);
            if (!is_useful_news_paragraph(paragraph)) {
                continue;
            }
            const std::string key = to_lower(paragraph);
            if (!seen.insert(key).second) {
                continue;
            }
            paragraphs.push_back(paragraph);
            if (paragraphs.size() >= 12) {
                break;
            }
        }
        if (paragraphs.size() >= 12) {
            break;
        }
    }

    std::string body;
    if (!paragraphs.empty()) {
        for (size_t i = 0; i < paragraphs.size(); ++i) {
            if (i > 0) {
                body += "\n\n";
            }
            body += paragraphs[i];
            if (body.size() >= MAX_ANALYSIS_TEXT_BYTES) {
                body.resize(MAX_ANALYSIS_TEXT_BYTES);
                break;
            }
        }
        body = trim(body);
    } else {
        const std::string fallback = plain_text_from_html_fragment(sections.empty() ? payload : sections.front());
        body = build_excerpt_from_text(fallback);
    }

    if (body.empty()) {
        return "";
    }

    if (body.rfind(title, 0) == 0 && body.size() > title.size()) {
        body = trim(body.substr(title.size()));
    }
    if (body.empty()) {
        body = "No body text extracted from URL.";
    }
    return title + "\n" + body;
}

std::string read_fd_limited(int fd, size_t limit_bytes, bool& truncated) {
    std::array<char, 4096> buffer{};
    std::string output;
    output.reserve(limit_bytes);
    truncated = false;

    while (true) {
        const ssize_t bytes_read = read(fd, buffer.data(), buffer.size());
        if (bytes_read <= 0) {
            break;
        }
        const size_t remaining = (output.size() < limit_bytes) ? (limit_bytes - output.size()) : 0;
        if (remaining == 0) {
            truncated = true;
            continue;
        }
        const size_t to_append = std::min(static_cast<size_t>(bytes_read), remaining);
        output.append(buffer.data(), to_append);
        if (to_append < static_cast<size_t>(bytes_read)) {
            truncated = true;
        }
    }

    return output;
}

// RAII wrapper for file descriptor cleanup
struct FDGuard {
    int fd;
    explicit FDGuard(int f) : fd(f) {}
    ~FDGuard() {
        if (fd >= 0) {
            close(fd);
        }
    }
    void release() { fd = -1; }
    FDGuard(const FDGuard&) = delete;
    FDGuard& operator=(const FDGuard&) = delete;
};

bool fetch_url_with_retry(const std::string& url, std::string& payload, std::string& error, 
                          const std::string& host, const std::string& resolved_ip, int attempt = 1) {
    const int MAX_ATTEMPTS = 2;
    const int RETRY_DELAY_MS = 1500;
    
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        error = "Fetching: " + url + " | Error: Failed to allocate fetch pipeline";
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        error = "Fetching: " + url + " | Error: Failed to spawn URL fetch process";
        return false;
    }

    // Use FDGuard in parent to ensure cleanup
    FDGuard fd_read_guard(pipe_fds[0]);
    FDGuard fd_write_guard(pipe_fds[1]);

    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        std::string resolve_arg_80 = host + ":80:" + resolved_ip;
        std::string resolve_arg_443 = host + ":443:" + resolved_ip;

        std::vector<std::string> args_storage = {
            "curl",
            "--location",
            "--silent",
            "--show-error",
            "--proto", "=http,https",
            "--proto-redir", "=http,https",
            "--max-redirs", "5",
            "--max-time", "20",
            "--connect-timeout", "8",
            "--compressed",
            "--cookie-jar", "/dev/null",
            "--cookie", "session_type=user",
            "-H", "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
            "-H", "Accept-Language: en-US,en;q=0.9",
            "-H", "Accept-Encoding: gzip, deflate, br",
            "-H", "Cache-Control: no-cache",
            "-H", "Pragma: no-cache",
            "-H", "DNT: 1",
            "-H", "Connection: keep-alive",
            "-H", "Upgrade-Insecure-Requests: 1",
            "-H", "Sec-Fetch-Dest: document",
            "-H", "Sec-Fetch-Mode: navigate",
            "-H", "Sec-Fetch-Site: none",
            "-H", "Sec-Fetch-User: ?1",
            "-H", "Referer: https://www.google.com/",
            "-H", "Origin: https://www.google.com",
            "--user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0",
            "--tlsv1.2",
            "--resolve", resolve_arg_80,
            "--resolve", resolve_arg_443,
            "--url", url
        };

        std::vector<char*> argv;
        argv.reserve(args_storage.size() + 1);
        for (std::string& arg : args_storage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp("curl", argv.data());
        _exit(127);
    }

    fd_write_guard.release();  // Child process will close this
    close(pipe_fds[1]);  // Parent closes write end to trigger EOF on child's stdout
    
    bool truncated = false;
    const std::string output = read_fd_limited(fd_read_guard.fd, MAX_FETCH_RESPONSE_BYTES, truncated);
    // fd_read_guard will close the fd automatically on exit

    int status = 0;
    int exit_code = -1;
    if (waitpid(pid, &status, 0) >= 0 && WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }

    if (exit_code != 0) {
        std::string curl_error = output.empty() ? ("curl failed (exit code " + std::to_string(exit_code) + ")")
                                                 : trim(output);
        
        // Retry on specific errors
        if (attempt < MAX_ATTEMPTS && 
            (curl_error.find("401") != std::string::npos ||
             curl_error.find("429") != std::string::npos ||
             curl_error.find("timeout") != std::string::npos ||
             curl_error.find("Temporary failure") != std::string::npos ||
             curl_error.find("Empty reply") != std::string::npos)) {
            
            usleep(RETRY_DELAY_MS * 1000);
            return fetch_url_with_retry(url, payload, error, host, resolved_ip, attempt + 1);
        }
        
        error = "Fetching: " + url + " | Error: " + curl_error;
        if (attempt > 1) {
            error += " (after " + std::to_string(attempt) + " attempts)";
        }
        return false;
    }

    payload = output;
    if (truncated) {
        payload = payload + " ";
    }
    return !payload.empty();
}

bool fetch_url_payload(const std::string& url, std::string& payload, std::string& error) {
    const std::string host = extract_host_from_url(to_lower(url));
    if (host.empty()) {
        error = "Fetching: " + url + " | Error: Could not extract URL host";
        return false;
    }
    std::string resolved_ip;
    if (!host_resolves_to_public_address(host, resolved_ip)) {
        error = "Fetching: " + url + " | Error: URL host does not resolve to a public address";
        return false;
    }

    return fetch_url_with_retry(url, payload, error, host, resolved_ip);
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string read_file(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        if (in.fail()) {
            continue;  // Failed to read, try next path
        }
        return ss.str();
    }
    return "";
}

std::string resolve_data_file(const std::string& filename) {
    const std::vector<std::string> candidates = {
        "data/" + filename,
        "../data/" + filename,
        "../../data/" + filename
    };
    for (const auto& path : candidates) {
        std::ifstream in(path);
        if (in.is_open()) {
            return path;
        }
    }
    return "";
}

std::string first_line_headline(const std::string& text) {
    std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return "Untitled";
    }
    size_t newline = cleaned.find('\n');
    std::string headline = newline == std::string::npos ? cleaned : cleaned.substr(0, newline);
    headline = trim(headline);
    if (headline.empty()) {
        return "Untitled";
    }
    if (headline.size() > 120) {
        headline = headline.substr(0, 120);
    }
    return headline;
}

std::string body_without_headline(const std::string& text) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return "";
    }

    const size_t newline = cleaned.find('\n');
    if (newline == std::string::npos) {
        return "";
    }

    const std::string remainder = trim(cleaned.substr(newline + 1));
    return remainder.empty() ? trim(cleaned.substr(0, newline)) : remainder;
}

std::string status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 429: return "Too Many Requests";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

void send_response(int fd, int code, const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << code << " " << status_text(code) << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "Cache-Control: no-store\r\n";
    response << "X-Content-Type-Options: nosniff\r\n";
    response << "X-Frame-Options: DENY\r\n";
    response << "Referrer-Policy: no-referrer\r\n";
    response << "Cross-Origin-Resource-Policy: same-origin\r\n";
    response << "Content-Security-Policy: default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'\r\n\r\n";
    response << body;
    (void)send_all(fd, response.str());
}

size_t extract_content_length(const std::string& header_blob) {
    constexpr size_t MAX_CONTENT_LENGTH = 50 * 1024 * 1024;  // 50MB cap
    std::istringstream iss(header_blob);
    std::string line;
    (void)std::getline(iss, line);
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t sep = line.find(':');
        if (sep == std::string::npos) {
            continue;
        }
        std::string key = to_lower(trim(line.substr(0, sep)));
        std::string value = trim(line.substr(sep + 1));
        if (key == "content-length") {
            try {
                size_t len = static_cast<size_t>(std::stoull(value));
                return len > MAX_CONTENT_LENGTH ? MAX_CONTENT_LENGTH : len;
            } catch (const std::exception&) {
                return 0;
            }
        }
    }
    return 0;
}

bool read_http_request(int fd, std::string& raw) {
    raw.clear();
    char buffer[4096];
    const size_t max_size = 2 * 1024 * 1024;

    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        raw.append(buffer, static_cast<size_t>(n));
        if (raw.size() > max_size) {
            return false;
        }
        header_end = raw.find("\r\n\r\n");
    }

    const std::string headers = raw.substr(0, header_end + 4);
    const size_t content_length = extract_content_length(headers);
    const size_t expected_total = header_end + 4 + content_length;
    while (raw.size() < expected_total) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        raw.append(buffer, static_cast<size_t>(n));
        if (raw.size() > max_size) {
            return false;
        }
    }
    return true;
}

bool parse_http_request(const std::string& raw, HttpRequest& request) {
    const size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }

    std::istringstream header_stream(raw.substr(0, header_end));
    std::string request_line;
    if (!std::getline(header_stream, request_line)) {
        return false;
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream request_line_stream(request_line);
    std::string version;
    request_line_stream >> request.method >> request.path >> version;
    if (request.method.empty() || request.path.empty()) {
        return false;
    }
    if (!is_reasonable_http_method(request.method) || !is_reasonable_path(request.path)) {
        return false;
    }

    size_t query_pos = request.path.find('?');
    if (query_pos != std::string::npos) {
        request.path = request.path.substr(0, query_pos);
    }

    std::string line;
    while (std::getline(header_stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t sep = line.find(':');
        if (sep == std::string::npos) {
            continue;
        }
        request.headers[to_lower(trim(line.substr(0, sep)))] = trim(line.substr(sep + 1));
    }

    request.body = raw.substr(header_end + 4);
    if (request.body.size() > MAX_REQUEST_BODY_BYTES) {
        return false;
    }
    return true;
}

void skip_json_whitespace(const std::string& body, size_t& pos) {
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
}

bool parse_json_string_value(const std::string& body, size_t& pos, std::string& out) {
    if (pos >= body.size() || body[pos] != '"') {
        return false;
    }

    ++pos;
    out.clear();
    bool escaped = false;
    constexpr size_t MAX_STRING_LENGTH = 1024 * 1024;  // 1MB max per string

    while (pos < body.size()) {
        if (out.size() > MAX_STRING_LENGTH) {
            return false;  // String too long
        }
        const char c = body[pos++];
        if (escaped) {
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                case '/': out.push_back('/'); break;
                default: out.push_back(c); break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            return true;
        }
        out.push_back(c);
    }

    return false;
}

bool parse_request_json_object(const std::string& body,
                               std::unordered_map<std::string, std::string>& values) {
    values.clear();
    size_t pos = 0;
    constexpr size_t MAX_FIELDS = 1000;  // Limit number of fields
    constexpr size_t MAX_JSON_SIZE = 10 * 1024 * 1024;  // 10MB max JSON
    
    if (body.size() > MAX_JSON_SIZE) {
        return false;  // JSON too large
    }
    
    skip_json_whitespace(body, pos);
    if (pos >= body.size() || body[pos] != '{') {
        return false;
    }
    ++pos;

    while (true) {
        if (values.size() > MAX_FIELDS) {
            return false;  // Too many fields
        }
        skip_json_whitespace(body, pos);
        if (pos >= body.size()) {
            return false;
        }
        if (body[pos] == '}') {
            ++pos;
            skip_json_whitespace(body, pos);
            return pos == body.size();
        }

        std::string key;
        if (!parse_json_string_value(body, pos, key)) {
            return false;
        }

        skip_json_whitespace(body, pos);
        if (pos >= body.size() || body[pos] != ':') {
            return false;
        }
        ++pos;
        skip_json_whitespace(body, pos);

        std::string value;
        if (!parse_json_string_value(body, pos, value)) {
            return false;
        }
        values[key] = value;

        skip_json_whitespace(body, pos);
        if (pos >= body.size()) {
            return false;
        }
        if (body[pos] == ',') {
            ++pos;
            continue;
        }
        if (body[pos] == '}') {
            ++pos;
            skip_json_whitespace(body, pos);
            return pos == body.size();
        }
        return false;
    }
}

std::string job_status_to_string(JobStatus status) {
    switch (status) {
        case JobStatus::QUEUED: return "queued";
        case JobStatus::PROCESSING: return "processing";
        case JobStatus::DONE: return "done";
        case JobStatus::FAILED: return "failed";
        default: return "unknown";
    }
}

struct VerdictDecision {
    std::string label;
    std::string reason;
};

VerdictDecision classify_verdict(const CredibilityResult& result) {
    auto module_value = [&result](const std::string& key) -> double {
        auto it = result.module_scores.find(key);
        return it == result.module_scores.end() ? 50.0 : it->second;
    };

    const double overall = result.overall_score;
    const double deterministic =
        (result.deterministic_score > 0.0) ? result.deterministic_score : result.overall_score;
    const double source = module_value("source_validation");
    const double claim = module_value("claim_verifiability");
    const double phrase = module_value("phrase_indexing");
    const double kmp = module_value("kmp_matching");
    const double rabin = module_value("rabin_karp");
    const double greedy = module_value("greedy_filtering");
    const double preprocessing = module_value("preprocessing");
    const double frequency = module_value("frequency_analysis");

    const bool trusted_source = source >= 75.0;
    const bool unknown_source = source <= 55.0;
    
    const bool low_claim_verifiability = claim < 45.0;
    const bool very_low_claim = claim < 30.0;
    
    const bool detection_flags = kmp < 70.0 || rabin < 70.0 || phrase < 70.0 || frequency < 70.0;

    bool strong_fake_signal =
        deterministic < 40.0 ||
        (greedy < 20.0 && unknown_source && claim < 50.0) ||
        (source < 25.0 && deterministic < 65.0) ||
        (very_low_claim && deterministic < 55.0) ||
        (low_claim_verifiability && unknown_source && deterministic < 55.0 && detection_flags) ||
        (unknown_source && deterministic < 50.0 && claim < 50.0);

    if (result.ml_score >= 0.0 && result.ml_score > 55.0 && deterministic >= 55.0) {
        strong_fake_signal = false;
    }

    const bool suspicious_signal =
        (low_claim_verifiability && unknown_source) ||
        (unknown_source && deterministic < 68.0 && !trusted_source) ||
        (overall < 60.0 && deterministic >= 60.0 && !trusted_source);

    if (strong_fake_signal) {
        return {"Likely Fake", "Strong credibility-risk signals detected"};
    }
    
    if (suspicious_signal && !trusted_source) {
        return {"Needs Verification", "Unverified source with weak attribution"};
    }
    
    (void)preprocessing;
    return {"Likely Original", "Credible source and content structure"};
}

class NewsScopeWebServer {
public:
    explicit NewsScopeWebServer(int port, size_t workers)
        : port_(port), io_pool_(workers), analysis_pool_(workers) {
        jobs_.reserve(4096);
    }

    void run() {
        g_server_fd = create_server_socket();
        if (g_server_fd < 0) {
            std::cerr << "Failed to create server socket on port " << port_
                      << ": " << std::strerror(errno) << "\n";
            return;
        }

        std::cout << "NewsScope web portal started at http://localhost:" << port_ << "\n";
        std::cout << "Press Ctrl+C to stop.\n";

        while (g_running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(g_server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

            if (client_fd < 0) {
                if (errno == EINTR && !g_running.load()) {
                    break;
                }
                continue;
            }

            io_pool_.enqueue([this, client_fd]() { handle_client(client_fd); });
        }

        if (g_server_fd >= 0) {
            close(g_server_fd);
            g_server_fd = -1;
        }
    }

private:
    int port_;
    ThreadPool io_pool_;
    ThreadPool analysis_pool_;
    std::atomic<unsigned long long> next_job_id_{1};
    std::atomic<size_t> in_flight_count_{0};
    std::unordered_map<std::string, JobRecord> jobs_;
    std::mutex jobs_mutex_;

    void prune_finished_jobs_locked() {
        if (jobs_.size() <= MAX_STORED_FINISHED_JOBS) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            const bool is_finished = (it->second.status == JobStatus::DONE || it->second.status == JobStatus::FAILED);
            const bool is_expired = (now - it->second.updated_at) > FINISHED_JOB_TTL;
            if (is_finished && is_expired) {
                it = jobs_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t in_flight_jobs_locked() const {
        return in_flight_count_.load();
    }

    static int create_server_socket_impl(int port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(fd);
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }

        if (listen(fd, 64) < 0) {
            close(fd);
            return -1;
        }

        return fd;
    }

    int create_server_socket() const {
        return create_server_socket_impl(port_);
    }

    std::string load_asset(const std::string& filename, const std::string& fallback) const {
        const std::vector<std::string> paths = {
            "web/" + filename,
            "../web/" + filename,
            "../../web/" + filename
        };
        std::string content = read_file(paths);
        return content.empty() ? fallback : content;
    }

    void handle_client(int client_fd) {
        std::string raw;
        if (!read_http_request(client_fd, raw)) {
            close(client_fd);
            return;
        }

        HttpRequest request;
        if (!parse_http_request(raw, request)) {
            send_response(client_fd, 400, "application/json", "{\"error\":\"Invalid HTTP request\"}");
            close(client_fd);
            return;
        }

        route_request(client_fd, request);
        close(client_fd);
    }

    void route_request(int fd, const HttpRequest& request) {
        static const std::string html_fallback =
            "<!doctype html><html><body><h1>NewsScope</h1><p>Static assets missing.</p></body></html>";
        static const std::string css_fallback = "body{font-family:sans-serif;margin:2rem;}";
        static const std::string js_fallback = "console.log('app.js missing');";

        if (request.method == "GET" && (request.path == "/" || request.path == "/index.html")) {
            send_response(fd, 200, "text/html; charset=utf-8", load_asset("index.html", html_fallback));
            return;
        }

        if (request.method == "GET" && request.path == "/styles.css") {
            send_response(fd, 200, "text/css; charset=utf-8", load_asset("styles.css", css_fallback));
            return;
        }

        if (request.method == "GET" && request.path == "/app.js") {
            send_response(fd, 200, "application/javascript; charset=utf-8", load_asset("app.js", js_fallback));
            return;
        }

        if (request.method == "POST" && request.path == "/api/jobs") {
            handle_submit_job(fd, request);
            return;
        }

        const std::string prefix = "/api/jobs/";
        if (request.method == "GET" && request.path.rfind(prefix, 0) == 0) {
            handle_job_status(fd, request.path.substr(prefix.size()));
            return;
        }

        if (request.path.rfind("/api/", 0) == 0) {
            send_response(fd, 405, "application/json", "{\"error\":\"Unsupported API method\"}");
            return;
        }

        send_response(fd, 404, "application/json", "{\"error\":\"Not found\"}");
    }

    void handle_submit_job(int fd, const HttpRequest& request) {
        const auto content_type_it = request.headers.find("content-type");
        if (content_type_it == request.headers.end()) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Content-Type must be application/json.\"}");
            return;
        }
        
        // Trim and lowercase the Content-Type value for comparison
        std::string content_type_lower = to_lower(trim(content_type_it->second));
        if (content_type_lower.find("application/json") == std::string::npos) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Content-Type must be application/json.\"}");
            return;
        }

        std::unordered_map<std::string, std::string> request_json;
        if (!parse_request_json_object(request.body, request_json)) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Malformed JSON body. Only a flat object with string values is supported.\"}");
            return;
        }

        const std::string text = trim(request_json["text"]);
        const std::string url = trim(request_json["url"]);
        const std::string source = trim(request_json["source"]);

        if (text.empty() && url.empty()) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Missing input. Send JSON with text or url: {\\\"text\\\":\\\"...\\\",\\\"url\\\":\\\"https://...\\\",\\\"source\\\":\\\"...\\\"}\"}");
            return;
        }
        if (text.size() > MAX_ANALYSIS_TEXT_BYTES) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Input text is too large.\"}");
            return;
        }
        if (source.size() > MAX_SOURCE_BYTES) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Source field is too large.\"}");
            return;
        }

        if (text.empty() && !url.empty() && !is_allowed_http_url(url)) {
            send_response(fd, 400, "application/json",
                          "{\"error\":\"Invalid URL. Only public http/https links are supported.\"}");
            return;
        }

        const std::string job_id = std::to_string(next_job_id_.fetch_add(1));
        bool overloaded = false;
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            prune_finished_jobs_locked();
            if (in_flight_jobs_locked() >= MAX_IN_FLIGHT_JOBS) {
                overloaded = true;
            } else {
                jobs_.emplace(job_id, JobRecord{});
            }
        }
        if (overloaded) {
            send_response(fd, 429, "application/json",
                          "{\"error\":\"Server busy. Too many background jobs. Please retry shortly.\"}");
            return;
        }
        in_flight_count_.fetch_add(1);

        analysis_pool_.enqueue([this, job_id, text, url, source]() {
            run_analysis_job(job_id, text, url, source);
        });

        std::ostringstream out;
        out << "{\"job_id\":\"" << job_id << "\",\"status\":\"queued\"}";
        send_response(fd, 202, "application/json", out.str());
    }

    void run_analysis_job(const std::string& job_id,
                          const std::string& text,
                          const std::string& url,
                          const std::string& source) {
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                return;
            }
            it->second.status = JobStatus::PROCESSING;
            it->second.updated_at = std::chrono::steady_clock::now();
        }

        try {
            thread_local ScoringEngine engine;
            thread_local bool initialized = false;
            if (!initialized) {
                engine.initialize(
                    resolve_data_file("sources.csv"),
                    resolve_data_file("suspicious_phrases.txt"),
                    resolve_data_file("negative_terms.csv")
                );
                initialized = true;
            }

            std::string resolved_text = text;
            bool used_url_input = false;
            if (resolved_text.empty() && !url.empty()) {
                std::string payload;
                std::string fetch_error;
                if (!fetch_url_payload(url, payload, fetch_error)) {
                    // Provide more context in error message
                    std::string detailed_error = fetch_error;
                    if (fetch_error.find("401") != std::string::npos || fetch_error.find("Forbidden") != std::string::npos) {
                        detailed_error += " | Note: Site may be protected by anti-bot measures (Cloudflare, etc.)";
                    }
                    if (fetch_error.find("Empty reply") != std::string::npos) {
                        detailed_error += " | Note: Site blocked the connection. Try copying text manually.";
                    }
                    throw std::runtime_error("Unable to fetch article URL: " + detailed_error);
                }
                resolved_text = build_article_text_from_url_payload(url, payload);
                if (resolved_text.empty()) {
                    throw std::runtime_error("Unable to extract readable article text from URL. The fetched HTML may not contain article content. Try copying text manually instead.");
                }
                used_url_input = true;
            }

            if (resolved_text.empty()) {
                throw std::runtime_error("No analyzable text provided");
            }

            // Extract domain from URL if no source provided
            std::string final_source = source;
            if (final_source.empty() && !url.empty()) {
                final_source = extract_domain_from_url(url);
                if (final_source.empty()) {
                    final_source = "Unknown Source";
                }
            }
            if (final_source.empty()) {
                final_source = "Unknown Source";
            }
            
            const std::string headline = first_line_headline(resolved_text);
            const std::string body = body_without_headline(resolved_text);
            const Article article(job_id, headline, body, final_source);
            CredibilityResult result = engine.assess_article(article);
            if (used_url_input) {
                result.explanations.push_back("[Input] URL source: " + url);
                result.explanations.push_back("[Detected] Domain: " + final_source);
            }
            const VerdictDecision verdict = classify_verdict(result);
            result.explanations.push_back("[Verdict] " + verdict.reason);

            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                return;
            }
            it->second.status = JobStatus::DONE;
            it->second.result = result;
            it->second.label = verdict.label;
            it->second.label_reason = verdict.reason;
            it->second.updated_at = std::chrono::steady_clock::now();
            in_flight_count_.fetch_sub(1);
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                return;
            }
            it->second.status = JobStatus::FAILED;
            it->second.error = ex.what();
            it->second.updated_at = std::chrono::steady_clock::now();
            in_flight_count_.fetch_sub(1);
        }
    }

    void handle_job_status(int fd, const std::string& job_id) {
        JobRecord record;
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                send_response(fd, 404, "application/json", "{\"error\":\"Job not found\"}");
                return;
            }
            record = it->second;
        }

        std::ostringstream out;
        out << "{\"job_id\":\"" << job_id << "\",\"status\":\"" << job_status_to_string(record.status) << "\"";

        if (record.status == JobStatus::FAILED) {
            out << ",\"error\":\"" << json_escape(record.error) << "\"}";
            send_response(fd, 200, "application/json", out.str());
            return;
        }

        if (record.status == JobStatus::DONE) {
            out << ",\"label\":\"" << json_escape(record.label) << "\"";
            out << ",\"label_reason\":\"" << json_escape(record.label_reason) << "\"";
            out << ",\"score\":" << record.result.overall_score;
            out << ",\"deterministic_score\":" << record.result.deterministic_score;
            if (record.result.ml_score >= 0.0) {
                out << ",\"ml_score\":" << record.result.ml_score;
            } else {
                out << ",\"ml_score\":null";
            }
            out << ",\"processing_ms\":" << record.result.processing_time.count();

            out << ",\"module_scores\":{";
            bool first = true;
            for (const auto& entry : record.result.module_scores) {
                if (!first) {
                    out << ",";
                }
                first = false;
                out << "\"" << json_escape(entry.first) << "\":" << entry.second;
            }
            out << "}";

            out << ",\"explanations\":[";
            for (size_t i = 0; i < record.result.explanations.size(); ++i) {
                if (i > 0) {
                    out << ",";
                }
                out << "\"" << json_escape(record.result.explanations[i]) << "\"";
            }
            out << "]";
        }

        out << "}";
        send_response(fd, 200, "application/json", out.str());
    }
};

void signal_handler(int) {
    g_running.store(false);
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

}  // namespace

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGCHLD, SIG_IGN);  // Prevent zombie processes

    int port = 8080;
    if (const char* env_port = std::getenv("PORT")) {
        try {
            const int parsed = std::stoi(env_port);
            if (parsed > 0 && parsed <= 65535) {
                port = parsed;
            } else {
                std::cerr << "Ignoring invalid PORT value: " << env_port << "\n";
            }
        } catch (const std::exception&) {
            std::cerr << "Ignoring invalid PORT value: " << env_port << "\n";
        }
    }

    const size_t workers = std::max<size_t>(4, std::thread::hardware_concurrency());
    NewsScopeWebServer server(port, workers);
    server.run();
    return 0;
}
