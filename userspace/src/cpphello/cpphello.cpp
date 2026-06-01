#include <cpp/string.hpp>

#include <stdio.h>
#include <syscall.h>

using ntux::cpp::String;

extern "C" void ntux_user_entry(void) {
    String msg("NTux C++ userspace online");
    msg.append(" (libcpp aktiv)");

    (void)sys_set_text_color(0xFF9ED6FFu);
    puts(msg.c_str());
    puts("Starte mit: run /boot/modules/cpphello.elf");
    sys_exit(0);
}
