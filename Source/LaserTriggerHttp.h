#ifndef __LASER_TRIGGER_HTTP_H
#define __LASER_TRIGGER_HTTP_H

#include <cctype>
#include <string>

// The LaserDriver Pi's web API (LaserDriver Pi/web_app.py), which is how the
// laser is driven over the network:
//
//     POST /api/trigger_gpio      (no body)
//     -> 200 {"ok": true, "path": "gpio"}
//
// The endpoint fires the fast trigger (Pi GPIO 24 -> MSPM0 PA19 edge ISR).
// "ok" is false when the broker could not fire, e.g. its GPIO is not open.
//
// Free of JUCE so Tests/ can check the request and the reply parsing.
namespace LaserTriggerHttp
{
constexpr int DEFAULT_PORT = 8080;
constexpr const char* TRIGGER_PATH = "/api/trigger_gpio";

enum class Reply
{
    Fired, // 200 and "ok": true
    Rejected, // 200 but "ok": false -- the Pi is up but did not fire
    HttpError, // any other status
    Malformed // not an HTTP response
};

inline std::string buildRequest (const std::string& host, int port)
{
    return std::string ("POST ") + TRIGGER_PATH + " HTTP/1.1\r\n"
         + "Host: " + host + ":" + std::to_string (port) + "\r\n"
         + "Content-Length: 0\r\n"
         + "Connection: close\r\n"
         + "\r\n";
}

inline Reply parseReply (const std::string& response)
{
    // Status line: "HTTP/1.x 200 ..."
    if (response.compare (0, 5, "HTTP/") != 0)
        return Reply::Malformed;

    const size_t space = response.find (' ');
    if (space == std::string::npos || space + 4 > response.size())
        return Reply::Malformed;

    if (response.compare (space + 1, 3, "200") != 0)
        return Reply::HttpError;

    const size_t bodyStart = response.find ("\r\n\r\n");
    if (bodyStart == std::string::npos)
        return Reply::Malformed;

    // Flask's jsonify may or may not put a space after the colon
    const size_t key = response.find ("\"ok\"", bodyStart);
    if (key == std::string::npos)
        return Reply::Malformed;

    size_t i = key + 4;
    while (i < response.size() && std::isspace ((unsigned char) response[i]))
        i++;
    if (i >= response.size() || response[i] != ':')
        return Reply::Malformed;
    i++;
    while (i < response.size() && std::isspace ((unsigned char) response[i]))
        i++;

    if (response.compare (i, 4, "true") == 0)
        return Reply::Fired;
    if (response.compare (i, 5, "false") == 0)
        return Reply::Rejected;
    return Reply::Malformed;
}

/** True for a dotted-quad IPv4 address, so a trigger never waits on a name lookup. */
inline bool isNumericIPv4 (const std::string& host)
{
    int parts = 0;
    int digits = 0;
    int value = 0;

    for (char c : host)
    {
        if (c >= '0' && c <= '9')
        {
            if (++digits > 3)
                return false;
            value = value * 10 + (c - '0');
            if (value > 255)
                return false;
        }
        else if (c == '.')
        {
            if (digits == 0)
                return false;
            parts++;
            digits = 0;
            value = 0;
        }
        else
        {
            return false;
        }
    }

    return parts == 3 && digits > 0;
}
} // namespace LaserTriggerHttp

#endif
