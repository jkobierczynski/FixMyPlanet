// SPDX-License-Identifier: GPL-3.0-or-later
#include <clocale>
#include <cmath>

#include "json.h"
#include "session.h"
#include "strutil.h"
#include "testing.h"
#include "timeutil.h"

using namespace fixmyplanet;

int main() {
    // Numbers must not depend on the C locale (decimal comma in many of them).
    for (const char* loc : {"C", "de_DE.UTF-8", "nl_BE.UTF-8", "fr_FR.UTF-8"}) {
        std::setlocale(LC_ALL, loc);  // silently stays put if the locale is not installed
        double v = 0;
        CHECK(parseDouble("3.25", v));
        CHECK_NEAR(v, 3.25, 1e-12);
        CHECK(parseDouble(" -1.5e3 ", v));
        CHECK_NEAR(v, -1500.0, 1e-9);
        CHECK(parseDouble("32768.", v));
        CHECK(parseDouble("1.0D2", v));
        CHECK_NEAR(v, 100.0, 1e-9);
        CHECK(!parseDouble("3,25", v));
        CHECK(!parseDouble("abc", v));
        CHECK(!parseDouble("1.5x", v));
        CHECK(!parseDouble("", v));
        CHECK(!parseDouble("inf", v));
        CHECK(!parseDouble("nan", v));
        CHECK(!parseDouble("-INF", v));
        CHECK(!parseDouble("0x10", v));
        CHECK(!parseDouble("1e999", v));
        CHECK_EQ_STR(formatFixed(1.25, 2), "1.25");
        CHECK_EQ_STR(formatDouble(0.5), "0.5");
        CHECK_EQ_STR(formatTime(julianDate(2026, 1, 10, 22, 3, 30.5), 1), "2026-01-10T22:03:30.5Z");

        Json j;
        std::string err;
        CHECK(Json::parse("{\"a\": 1.5, \"b\": [true, null, \"x\\n\\u00e9\\\"\"], \"c\": {\"d\": -2e-3}}", j, err));
        CHECK_NEAR(j.get("a").number(), 1.5, 1e-12);
        CHECK(j.get("b").isArray() && j.get("b").items().size() == 3);
        CHECK(j.get("b").items()[0].boolean());
        CHECK(j.get("b").items()[1].isNull());
        CHECK_EQ_STR(j.get("b").items()[2].string(), "x\n\xc3\xa9\"");
        CHECK_NEAR(j.get("c").get("d").number(), -0.002, 1e-15);
        CHECK(j.get("missing").isNull());
        // Dump and parse again.
        Json k;
        CHECK(Json::parse(j.dump(2), k, err));
        CHECK_EQ_STR(k.dump(0), j.dump(0));
        CHECK(j.dump(0).find("1.5") != std::string::npos);
    }
    std::setlocale(LC_ALL, "C");

    {
        Json j;
        std::string err;
        CHECK(!Json::parse("", j, err));
        CHECK(!Json::parse("{", j, err));
        CHECK(!Json::parse("{\"a\": }", j, err));
        CHECK(!Json::parse("[1, 2,]", j, err));
        CHECK(!Json::parse("{\"a\": 1} extra", j, err));
        CHECK(!Json::parse("\"unterminated", j, err));
        CHECK(!Json::parse("nul", j, err));
        std::string deep(200, '[');
        CHECK(!Json::parse(deep, j, err));
        CHECK(Json::parse("  [ ]  ", j, err));
        CHECK(Json::parse("\xEF\xBB\xBF{}", j, err));
    }

    {
        Session s;
        s.planet = Planet::Saturn;
        s.system = "III";
        s.customRate = 844.3;
        s.referenceJd = julianDate(2026, 10, 4, 23, 0, 0);
        s.limbDarkening = 0.6;
        s.northUp = true;
        s.refineAlignment = false;
        SessionImage im;
        im.path = "caps/Sat \"A\" \xc3\xa9.tif";
        im.jdUtc = julianDate(2026, 10, 4, 22, 58, 12.5);
        im.channel = "R";
        im.geometry = {321.25, 240.5, 151.75, -172.3, true};
        im.measured = true;
        im.weight = 0.8;
        s.images.push_back(im);
        im.path = "second.tif";
        im.jdUtc = 0;
        im.measured = false;
        s.images.push_back(im);

        Session t;
        std::string err;
        CHECK(Session::fromJson(s.toJson(), t, err));
        CHECK(t.planet == Planet::Saturn);
        CHECK_EQ_STR(t.system, "III");
        CHECK_NEAR(t.customRate, 844.3, 1e-9);
        CHECK_NEAR((t.referenceJd - s.referenceJd) * 86400, 0.0, 0.06);
        CHECK_NEAR(t.limbDarkening, 0.6, 1e-12);
        CHECK(t.northUp && !t.refineAlignment && t.normalizeBrightness);
        CHECK(t.images.size() == 2);
        CHECK_EQ_STR(t.images[0].path, s.images[0].path);
        CHECK_NEAR((t.images[0].jdUtc - s.images[0].jdUtc) * 86400, 0.0, 0.06);
        CHECK_EQ_STR(t.images[0].channel, "R");
        CHECK_NEAR(t.images[0].geometry.cx, 321.25, 1e-9);
        CHECK_NEAR(t.images[0].geometry.cy, 240.5, 1e-9);
        CHECK_NEAR(t.images[0].geometry.radius, 151.75, 1e-9);
        CHECK_NEAR(t.images[0].geometry.northAngle, -172.3, 1e-9);
        CHECK(t.images[0].geometry.mirror && t.images[0].measured);
        CHECK_NEAR(t.images[0].weight, 0.8, 1e-12);
        CHECK(t.images[1].jdUtc == 0 && !t.images[1].measured);

        // Defaults: automatic limb darkening and reference time survive the round trip.
        Session d, d2;
        CHECK(Session::fromJson(d.toJson(), d2, err));
        CHECK(d2.limbDarkening < 0 && d2.referenceJd == 0 && !std::isfinite(d2.customRate));
        DerotateOptions o = t.options();
        CHECK(o.planet == Planet::Saturn && o.system == 0 && o.northUp && !o.refineAlignment);

        CHECK(!Session::fromJson("{}", t, err));
        CHECK(!Session::fromJson("{\"format\": \"fixmyplanet-session\", \"planet\": \"Vulcan\"}", t, err));
        CHECK(!Session::fromJson("{\"format\": \"fixmyplanet-session\", \"version\": 99}", t, err));
        CHECK(!Session::fromJson("{\"format\": \"fixmyplanet-session\", \"images\": [{\"file\": \"a.tif\", \"time\": \"noon\"}]}", t, err));
        CHECK(!Session::fromJson("not json", t, err));
        // Measurements far outside any image are refused (they would stall the derotation).
        const char* head = "{\"format\": \"fixmyplanet-session\", \"images\": [{\"file\": \"a.tif\", \"measured\": true, ";
        CHECK(Session::fromJson(std::string(head) + "\"cx\": 100, \"cy\": 100, \"radius\": 80}]}", t, err));
        CHECK(!Session::fromJson(std::string(head) + "\"cx\": 100, \"cy\": 100, \"radius\": 30000000}]}", t, err));
        CHECK(!Session::fromJson(std::string(head) + "\"cx\": 100, \"cy\": 100, \"radius\": 1}]}", t, err));
        CHECK(!Session::fromJson(std::string(head) + "\"cx\": 1e9, \"cy\": 100, \"radius\": 80}]}", t, err));
        CHECK(!Session::fromJson(std::string(head) + "\"cx\": 100, \"cy\": 100, \"radius\": 80, \"weight\": 0}]}", t, err));
    }

    CHECK_EQ_STR(channelFromFileName("2022-09-26-2130_5-U-R-Jup.tif"), "R");
    CHECK_EQ_STR(channelFromFileName("/x/Jup_213015_B.fits"), "B");
    CHECK_EQ_STR(channelFromFileName("jup-ch4-0001.tif"), "CH4");
    CHECK_EQ_STR(channelFromFileName("saturn_ir_stack.png"), "IR");
    CHECK_EQ_STR(channelFromFileName("jupiter_2026-01-10_22-00-00.tif"), "");
    CHECK_EQ_STR(channelFromFileName("RGB_final.tif"), "");
    CHECK_EQ_STR(channelFromFileName("Great_Red_Spot.tif"), "");

    CHECK_EQ_STR(resolvePath("/data/night1/session.json", "a.tif"), "/data/night1/a.tif");
    CHECK_EQ_STR(resolvePath("/data/night1/session.json", "sub/a.tif"), "/data/night1/sub/a.tif");
    CHECK_EQ_STR(resolvePath("/data/night1/session.json", "/abs/a.tif"), "/abs/a.tif");
    CHECK_EQ_STR(resolvePath("", "a.tif"), "a.tif");

    return testing::finish("test_json_session");
}
