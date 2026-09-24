/*
    Standalone tests for the laser trigger request and reply handling
    (Source/LaserTriggerHttp.h), which talks to LaserDriver's Pi/web_app.py.

        c++ -std=c++17 -I ../Source LaserTriggerHttpTests.cpp -o laser_trigger_tests
        ./laser_trigger_tests
*/

#include "LaserTriggerHttp.h"

#include <cstdio>
#include <string>

namespace
{
int checks = 0;
int failures = 0;

void check (bool ok, const char* what)
{
    checks++;
    if (! ok)
    {
        failures++;
        std::printf ("FAIL: %s\n", what);
    }
}

using LaserTriggerHttp::Reply;

std::string response (const std::string& status, const std::string& body)
{
    return "HTTP/1.1 " + status + "\r\nServer: Werkzeug/3.0.1 Python/3.11.2\r\nContent-Type: application/json\r\n"
         + "Content-Length: " + std::to_string (body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

void testRequest()
{
    check (LaserTriggerHttp::buildRequest ("192.168.17.10", 8080)
               == "POST /api/trigger_gpio HTTP/1.1\r\nHost: 192.168.17.10:8080\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
           "request: POST /api/trigger_gpio with an empty body");
}

void testReplies()
{
    check (LaserTriggerHttp::parseReply (response ("200 OK", "{\"ok\":true,\"path\":\"gpio\"}\n")) == Reply::Fired,
           "compact JSON ok:true -> Fired");
    check (LaserTriggerHttp::parseReply (response ("200 OK", "{\n  \"ok\": true,\n  \"path\": \"gpio\"\n}\n")) == Reply::Fired,
           "pretty JSON ok: true -> Fired");
    check (LaserTriggerHttp::parseReply (response ("200 OK", "{\"ok\":false,\"path\":\"gpio\"}\n")) == Reply::Rejected,
           "ok:false -> Rejected");
    check (LaserTriggerHttp::parseReply ("HTTP/1.0 200 OK\r\n\r\n{\"ok\": true}") == Reply::Fired,
           "HTTP/1.0 status line accepted");
    check (LaserTriggerHttp::parseReply (response ("404 NOT FOUND", "<!doctype html>")) == Reply::HttpError,
           "404 -> HttpError");
    check (LaserTriggerHttp::parseReply (response ("500 INTERNAL SERVER ERROR", "")) == Reply::HttpError,
           "500 -> HttpError");
    check (LaserTriggerHttp::parseReply ("") == Reply::Malformed, "empty (no answer) -> Malformed");
    check (LaserTriggerHttp::parseReply ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n") == Reply::Malformed,
           "truncated before body -> Malformed");
    check (LaserTriggerHttp::parseReply (response ("200 OK", "{\"path\":\"gpio\"}")) == Reply::Malformed,
           "no ok field -> Malformed");
    check (LaserTriggerHttp::parseReply ("SSH-2.0-OpenSSH_9.2\r\n") == Reply::Malformed,
           "not HTTP -> Malformed");
}

void testComplete()
{
    const std::string full = response ("200 OK", "{\"ok\":true,\"path\":\"gpio\"}\n");
    check (LaserTriggerHttp::isComplete (full), "complete once Content-Length bytes of body arrived");
    check (! LaserTriggerHttp::isComplete (full.substr (0, full.size() - 1)), "incomplete one byte short");
    check (! LaserTriggerHttp::isComplete (full.substr (0, full.find ("\r\n\r\n"))), "incomplete before the blank line");
    check (LaserTriggerHttp::isComplete ("HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\n{}"), "header name is case-insensitive");
    check (! LaserTriggerHttp::isComplete ("HTTP/1.1 200 OK\r\n\r\n{\"ok\": true}"), "no Content-Length: wait for the close");
}

void testNumericIPv4()
{
    check (LaserTriggerHttp::isNumericIPv4 ("192.168.17.10"), "accepts a dotted quad");
    check (LaserTriggerHttp::isNumericIPv4 ("255.255.255.255"), "accepts 255s");
    check (! LaserTriggerHttp::isNumericIPv4 (""), "rejects empty");
    check (! LaserTriggerHttp::isNumericIPv4 ("laserhat.local"), "rejects a hostname");
    check (! LaserTriggerHttp::isNumericIPv4 ("192.168.17"), "rejects three parts");
    check (! LaserTriggerHttp::isNumericIPv4 ("192.168.17.10.5"), "rejects five parts");
    check (! LaserTriggerHttp::isNumericIPv4 ("192.168.17.256"), "rejects an octet over 255");
    check (! LaserTriggerHttp::isNumericIPv4 ("192..17.10"), "rejects an empty octet");
    check (! LaserTriggerHttp::isNumericIPv4 ("192.168.17."), "rejects a trailing dot");
    check (! LaserTriggerHttp::isNumericIPv4 (" 192.168.17.10"), "rejects whitespace");
}
} // namespace

int main()
{
    testRequest();
    testReplies();
    testComplete();
    testNumericIPv4();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
