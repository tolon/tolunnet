"""W4 part 2: LISTVIEW pages + P4 DNS2/MTU + validation (TNET-110)."""
import io

p = 'src/setup/setup_types.h'
s = io.open(p, encoding='utf-8').read()
old = """    char dns1_str[16];
    char dns2_str[16];
    char host_str[32];"""
new = """    char dns1_str[16];
    char dns2_str[16];
    char mtu_str[8];        /* TNET-110: blank = driver default, 576..1500 */
    char host_str[32];"""
assert old in s
s = s.replace(old, new)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('types ok')

p = 'src/cmds/TolunnetSetup.c'
s = io.open(p, encoding='utf-8').read()

def rep(old, new):
    global s
    assert old in s, 'ANCHOR MISSING: ' + old[:70]
    s = s.replace(old, new)

# ---- new gadget ids ----
rep('#define GID_P5_TEST_BTN     150',
    '#define GID_P5_TEST_BTN     150\n'
    '#define GID_P1_IMPORT_CHK   112\n'
    '#define GID_P2_LIST         122\n'
    '#define GID_P2_TEST_BTN     123\n'
    '#define GID_P3_NETLIST      134\n'
    '#define GID_P3_SSID_STR     135\n'
    '#define GID_P4_MTU_STR      148\n'
    '#define GID_P4_DHCPDNS_CHK  149\n'
    '#define GID_P5_CHECKLIST     152')

# ---- listview label storage ----
rep('static STRPTR g_hw_labels[MAX_DETECTED_HW + 1];',
    '''static STRPTR g_hw_labels[MAX_DETECTED_HW + 1];
static char    g_hw_lines[MAX_DETECTED_HW][96];
static STRPTR g_stack_labels[MAX_DETECTED_STACKS + 1];
static char    g_stack_lines[MAX_DETECTED_STACKS][96];
static STRPTR g_wifi_labels[MAX_WIFI_NETWORKS + 1];
static char    g_wifi_lines[MAX_WIFI_NETWORKS][96];
static STRPTR g_check_labels[8];
static char    g_check_lines[8][96];''')

# ---- sync: new string gadgets ----
rep('''                case GID_P4_DNS1_STR:
                    strncpy(g_ws.dns1_str, (const char *)si->Buffer, sizeof(g_ws.dns1_str) - 1);
                    break;''',
'''                case GID_P4_DNS1_STR:
                    strncpy(g_ws.dns1_str, (const char *)si->Buffer, sizeof(g_ws.dns1_str) - 1);
                    break;
                case GID_P4_DNS2_STR:
                    strncpy(g_ws.dns2_str, (const char *)si->Buffer, sizeof(g_ws.dns2_str) - 1);
                    break;
                case GID_P4_MTU_STR:
                    strncpy(g_ws.mtu_str, (const char *)si->Buffer, sizeof(g_ws.mtu_str) - 1);
                    break;
                case GID_P3_SSID_STR:
                    strncpy(g_ws.wifi_ssid_str, (const char *)si->Buffer, sizeof(g_ws.wifi_ssid_str) - 1);
                    break;''')

# ---- state: wifi_ssid_str field ----
rep('    char        wifi_pass[64];',
    '    char        wifi_pass[64];\n    char        wifi_ssid_str[34];   /* TNET-110: editable SSID (hidden nets) */')

io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('stage 1 ok')
