/* Host tests for gp_core.c. MIT, Copyright (c) 2026 Dalsin Limited. */
#include <stdio.h>
#include <string.h>
#include "gp_core.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    gp_settings s, t;
    char buf[512], el[16];
    uint8_t g[16];

    /* the defaults: off, a CD32 pad on the joystick port from any pad */
    gp_defaults(&s);
    CHECK(!s.patch && s.port[1].mode == GP_MODE_CD32 && s.port[1].any && s.port[0].mode == GP_MODE_NONE);
    CHECK(gp_fed_port(&s) == 1);
    CHECK(gp_patch_from_text("1") && gp_patch_from_text(" 1\n") && !gp_patch_from_text("0") && !gp_patch_from_text("") && !gp_patch_from_text(NULL));

    /* ports.prefs round trip, with a GUID */
    for (int i = 0; i < 16; i++) g[i] = (uint8_t)(i * 17);
    s.port[0].mode = GP_MODE_JOYSTICK; s.port[0].any = 0; memcpy(s.port[0].guid, g, 16);
    gp_ports_to_text(&s, buf, sizeof buf);
    CHECK(strstr(buf, "port 1 cd32 any\n") != NULL);
    CHECK(strstr(buf, "port 0 joystick 00112233445566778899aabbccddeeff\n") != NULL);
    gp_defaults(&t);
    gp_ports_from_text(&t, buf);
    CHECK(t.port[0].mode == GP_MODE_JOYSTICK && !t.port[0].any && !memcmp(t.port[0].guid, g, 16));
    CHECK(t.port[1].mode == GP_MODE_CD32 && t.port[1].any);

    /* what the library writes (one line replaced, comments kept), and junk */
    gp_ports_from_text(&t, "# hello\n  port 1 joystick any\nport 7 cd32 any\nnonsense\nport 0 none any\n");
    CHECK(t.port[1].mode == GP_MODE_JOYSTICK && t.port[0].mode == GP_MODE_NONE);
    CHECK(gp_fed_port(&t) == 1);
    gp_ports_from_text(&t, "port 1 none any\nport 0 cd32 nothex\n");
    CHECK(gp_fed_port(&t) == 0 && t.port[0].any);

    /* GUIDs */
    gp_guid_to_hex(g, buf);
    CHECK(!strcmp(buf, "00112233445566778899aabbccddeeff"));
    CHECK(!gp_guid_from_hex("0011", g) && !gp_guid_from_hex("zz112233445566778899aabbccddeeff", g));

    /* inputs as SDL elements */
    gp_input b = { GP_IN_BUTTON, 3, 0, 0 }, h = { GP_IN_HAT, 0, 4, 0 };
    gp_input ax = { GP_IN_AXIS, 1, 1, 0 }, axn = { GP_IN_AXIS, 1, -1, 0 }, trig = { GP_IN_AXIS, 4, 1, -32768 }, trig0 = { GP_IN_AXIS, 5, 1, 0 };
    gp_input_text(&b, 0, el, sizeof el); CHECK(!strcmp(el, "b3"));
    gp_input_text(&h, 11, el, sizeof el); CHECK(!strcmp(el, "h0.4"));
    gp_input_text(&ax, 16, el, sizeof el); CHECK(!strcmp(el, "a1"));
    gp_input_text(&axn, 16, el, sizeof el); CHECK(!strcmp(el, "a1~"));
    gp_input_text(&trig, 19, el, sizeof el); CHECK(!strcmp(el, "a4"));
    gp_input_text(&trig0, 20, el, sizeof el); CHECK(!strcmp(el, "+a5"));
    gp_input_text(&axn, 13, el, sizeof el); CHECK(!strcmp(el, "-a1"));       /* d-pad left on an axis */
    gp_input_text(&b, 15, el, sizeof el); CHECK(!el[0]);                    /* a stick from a button: no */
    gp_input_text(&b, 19, el, sizeof el); CHECK(!strcmp(el, "b3"));         /* a digital trigger */

    /* a whole line, as the wizard makes it */
    gp_input in[GP_TARGETS];
    memset(in, 0, sizeof in);
    in[0] = (gp_input){ GP_IN_BUTTON, 0, 0, 0 };
    in[1] = (gp_input){ GP_IN_BUTTON, 1, 0, 0 };
    in[6] = (gp_input){ GP_IN_BUTTON, 4, 0, 0 };
    in[11] = (gp_input){ GP_IN_HAT, 0, 1, 0 };
    in[15] = (gp_input){ GP_IN_AXIS, 0, 1, 0 };
    int n = gp_build_mapping(g, "Pad, with a comma", in, buf, sizeof buf);
    CHECK(n > 0);
    CHECK(!strcmp(buf, "00112233445566778899aabbccddeeff,Pad with a comma,a:b0,b:b1,start:b4,dpup:h0.1,leftx:a0,platform:AmigaOS 3,"));
    CHECK(gp_mapping_ok(buf));
    CHECK(gp_build_mapping(g, "x", in, buf, 40) == -1);
    CHECK(!gp_mapping_ok("00112233445566778899aabbccddeeff,Name,platform:AmigaOS 3,"));
    CHECK(!gp_mapping_ok("0011,Name,a:b0,"));
    CHECK(!gp_mapping_ok("00112233445566778899aabbccddeeff,Name,a:,"));
    CHECK(gp_mapping_ok("00112233445566778899aabbccddeeff,Name,a:b0"));

    /* the summary */
    gp_mapping_summary("00112233445566778899aabbccddeeff,Pad,a:b0,b:b1,leftx:a0,lefty:a1,platform:AmigaOS 3,", buf, sizeof buf);
    CHECK(!strcmp(buf, "A b0, B b1, Stick a0  (4 inputs)"));
    gp_mapping_summary("bad", buf, sizeof buf);
    CHECK(!strcmp(buf, "(none)"));

    /* what the wizard sees change */
    gp_raw rest, now;
    memset(&rest, 0, sizeof rest);
    rest.axes[4] = -32768;
    now = rest;
    CHECK(gp_raw_change(&rest, &now, 0).kind == GP_IN_NONE);
    now.axes[2] = 9000; CHECK(gp_raw_change(&rest, &now, 0).kind == GP_IN_NONE);   /* a wobble */
    now.axes[4] = 32767;
    gp_input c = gp_raw_change(&rest, &now, 0);
    CHECK(c.kind == GP_IN_AXIS && c.index == 4 && c.value == 1 && c.rest == -32768);
    now.buttons[1] = 1u << 2;                                                    /* button 34 wins over the axis */
    c = gp_raw_change(&rest, &now, 0);
    CHECK(c.kind == GP_IN_BUTTON && c.index == 34);
    c = gp_raw_change(&rest, &now, 1);                                           /* a stick asked for: the axis */
    CHECK(c.kind == GP_IN_AXIS && c.index == 4);
    now = rest; now.hats[1] = 8;
    c = gp_raw_change(&rest, &now, 0);
    CHECK(c.kind == GP_IN_HAT && c.index == 1 && c.value == 8);

    printf(fails ? "%d failed\n" : "gp_core: all passed\n", fails);
    return fails != 0;
}
